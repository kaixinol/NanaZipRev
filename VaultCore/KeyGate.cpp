/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       KeyGate.cpp
 * PURPOSE:    Implementation for the Windows Hello key gate
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "KeyGate.h"

#include <winrt/base.h>
#include <winrt/Windows.Security.Credentials.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

#include <array>
#include <cstring>
#include <TlHelp32.h>
#include <filesystem>
#include <fstream>

#include "Crypto.h"
#include "Keystream.h"
#include "Session.h"
#include "Settings.h"
#include "VaultCore.h"
#include "VaultKey.h"

namespace Foundation = winrt::Windows::Foundation;
namespace Credentials = winrt::Windows::Security::Credentials;
namespace Cryptography = winrt::Windows::Security::Cryptography;
namespace Streams = winrt::Windows::Storage::Streams;

using NanaZip::Vault::KeyGateStatus;

namespace
{
    // The credential lives in the user's Hello container under this name.
    // Opening it by name prompts nobody; only asking it to sign does.
    constexpr wchar_t const* g_CredentialName = L"NanaZipRev.Vault";

    // The challenge and salt are stored in the key file, so a Hello reset
    // that loses the credential takes the key with it.
    constexpr std::size_t g_FieldSize = 32;

    // Purpose strings for HKDF and the key check, not secrets.
    constexpr char g_KekInfo[] = "NanaZipRev.Vault.KEK.v2";
    constexpr char g_KeyCheckLabel[] = "NanaZipRev.Vault.KeyCheck.v1";

    // 8-byte header, then four fixed fields. Nothing in the file is secret;
    // it is what ties one Hello credential to one vault folder.
    constexpr std::size_t g_HeaderSize = 8;
    constexpr std::size_t g_KeyFileSize =
        g_HeaderSize + g_FieldSize * 4;
    constexpr std::uint8_t g_KeyFileVersion = 1;

    // Allow the Windows Hello / Windows Security credential prompt to take the
    // foreground. KeyCredentialManager has no HWND overload, so the prompt is
    // shown by a separate process (never owned by our window); without this the
    // foreground lock we hold via the dialog's initial SetForegroundWindow blocks
    // the prompt and a stray window wins the z-order. The visible prompt is
    // hosted by CredentialUIBroker.exe on modern Windows; the legacy host is
    // credmgruihost.exe. Allow only those, never ASFW_ANY, so an unrelated
    // top-level window (e.g. a terminal) cannot steal the foreground afterwards.
    void AllowCredentialForeground()
    {
        bool Found = false;
        PCWSTR const Hosts[] = { L"CredentialUIBroker.exe", L"credmgruihost.exe" };
        HANDLE Snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (Snapshot != INVALID_HANDLE_VALUE)
        {
            PROCESSENTRY32W Entry = {};
            Entry.dwSize = sizeof(Entry);
            for (BOOL More = ::Process32FirstW(Snapshot, &Entry);
                 More;
                 More = ::Process32NextW(Snapshot, &Entry))
            {
                for (PCWSTR Host : Hosts)
                {
                    if (::lstrcmpiW(Entry.szExeFile, Host) == 0)
                    {
                        ::AllowSetForegroundWindow(Entry.th32ProcessID);
                        Found = true;
                        break;
                    }
                }
            }
            ::CloseHandle(Snapshot);
        }
        // Fallback only if no known host was running: a different SKU may name
        // the host differently. Broader than the targeted grant (lets any
        // process take foreground) but never worse than the prior broken
        // NgcIso-only grant.
        if (!Found)
        {
            ::AllowSetForegroundWindow(ASFW_ANY);
        }
    }

    struct KeyFile
    {
        std::array<std::uint8_t, g_FieldSize> Challenge = {};
        std::array<std::uint8_t, g_FieldSize> Salt = {};
        std::array<std::uint8_t, g_FieldSize> PublicKeyHash = {};
        std::array<std::uint8_t, g_FieldSize> KeyCheck = {};
    };

    // Classic never brings WinRT up, and init_apartment is a no-op when
    // the thread is already STA from the file manager's OleInitialize.
    bool InitApartment()
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
            return true;
        }
        catch (...)
        {
            // A conflicting apartment mode is a refusal, not a crash.
            return false;
        }
    }

    // Pumps messages until the operation completes: the Hello broker needs
    // the UI thread's foreground lock, and get() would block its STA.
    template <typename T>
    T PumpUntilDone(
        _In_ Foundation::IAsyncOperation<T> const& Operation,
        _In_ T Failure)
    {
        while (Operation.Status() == Foundation::AsyncStatus::Started)
        {
            MSG Msg;
            while (::PeekMessageW(&Msg, nullptr, 0, 0, PM_REMOVE))
            {
                // Dispatching WM_QUIT here would consume the one-shot quit
                // flag and strand WinMain2's GetMessage loop forever.
                if (Msg.message == WM_QUIT)
                {
                    ::PostQuitMessage(static_cast<int>(Msg.wParam));
                    continue;
                }
                ::TranslateMessage(&Msg);
                ::DispatchMessageW(&Msg);
            }
            ::Sleep(10);
        }

        try
        {
            return Operation.GetResults();
        }
        catch (...)
        {
            // A prompt that cannot be shown is a refusal like any other.
            return Failure;
        }
    }

    // Re-enables and re-shows the owner window when the wait leaves: without
    // it a throw would strand the owner. The owner is hidden, not merely
    // disabled: a disabled window keeps its z-order and steals focus from the
    // system credential prompt (Windows Hello / Windows Security), forcing the
    // user to click the PIN field. Hiding it lets the prompt take the
    // foreground and focus its edit control on its own.
    class DisabledOwnerGuard
    {
    public:
        DisabledOwnerGuard(HWND Window) :
            m_Window(Window),
            m_WasEnabled(Window ? ::IsWindowEnabled(Window) : FALSE),
            m_WasVisible(Window ? ::IsWindowVisible(Window) : FALSE)
        {
            if (this->m_Window)
            {
                ::EnableWindow(this->m_Window, FALSE);
                ::ShowWindow(this->m_Window, SW_HIDE);
            }
        }

        ~DisabledOwnerGuard()
        {
            if (this->m_Window)
            {
                ::EnableWindow(this->m_Window, this->m_WasEnabled);
                if (this->m_WasVisible)
                {
                    ::ShowWindow(this->m_Window, SW_SHOW);
                }
            }
        }

    private:
        HWND m_Window;
        BOOL m_WasEnabled;
        BOOL m_WasVisible;
    };

    std::filesystem::path KeyFilePath()
    {
        std::filesystem::path Root = NanaZip::Vault::VaultSettings::RootDir();
        if (Root.empty())
        {
            return Root;
        }
        return Root / L"vault.key";
    }

    bool LoadKeyFile(_Out_ KeyFile& File)
    {
        std::ifstream Stream(KeyFilePath(), std::ios::binary);
        if (!Stream)
        {
            return false;
        }
        std::array<std::uint8_t, g_KeyFileSize> Bytes = {};
        Stream.read(
            reinterpret_cast<char*>(Bytes.data()),
            static_cast<std::streamsize>(Bytes.size()));
        if (!Stream
            || Stream.gcount() != static_cast<std::streamsize>(Bytes.size()))
        {
            return false;
        }
        // Anything after the fixed fields means this is not our file.
        if (Stream.peek() != std::ifstream::traits_type::eof())
        {
            return false;
        }
        std::uint8_t const Magic[4] = { 'N', 'Z', 'K', '2' };
        if (0 != std::memcmp(Bytes.data(), Magic, sizeof(Magic))
            || Bytes[4] != g_KeyFileVersion)
        {
            return false;
        }
        std::memcpy(
            File.Challenge.data(),
            Bytes.data() + g_HeaderSize,
            g_FieldSize);
        std::memcpy(
            File.Salt.data(),
            Bytes.data() + g_HeaderSize + g_FieldSize,
            g_FieldSize);
        std::memcpy(
            File.PublicKeyHash.data(),
            Bytes.data() + g_HeaderSize + g_FieldSize * 2,
            g_FieldSize);
        std::memcpy(
            File.KeyCheck.data(),
            Bytes.data() + g_HeaderSize + g_FieldSize * 3,
            g_FieldSize);
        return true;
    }

    bool SaveKeyFile(_In_ KeyFile const& File)
    {
        std::filesystem::path Path = KeyFilePath();
        if (Path.empty())
        {
            return false;
        }
        std::error_code ErrorCode;
        std::filesystem::create_directories(Path.parent_path(), ErrorCode);
        if (ErrorCode)
        {
            return false;
        }

        std::array<std::uint8_t, g_KeyFileSize> Bytes = {};
        std::uint8_t const Header[g_HeaderSize] =
            { 'N', 'Z', 'K', '2', g_KeyFileVersion, 0, 0, 0 };
        std::memcpy(Bytes.data(), Header, sizeof(Header));
        std::memcpy(
            Bytes.data() + g_HeaderSize,
            File.Challenge.data(),
            g_FieldSize);
        std::memcpy(
            Bytes.data() + g_HeaderSize + g_FieldSize,
            File.Salt.data(),
            g_FieldSize);
        std::memcpy(
            Bytes.data() + g_HeaderSize + g_FieldSize * 2,
            File.PublicKeyHash.data(),
            g_FieldSize);
        std::memcpy(
            Bytes.data() + g_HeaderSize + g_FieldSize * 3,
            File.KeyCheck.data(),
            g_FieldSize);

        // Replaced atomically, like the index: a half-written key file would
        // strand entries that are still perfectly readable.
        std::filesystem::path Temporary = Path;
        Temporary += L".tmp";
        {
            std::ofstream Stream(
                Temporary,
                std::ios::binary | std::ios::trunc);
            if (!Stream)
            {
                return false;
            }
            Stream.write(
                reinterpret_cast<char const*>(Bytes.data()),
                static_cast<std::streamsize>(Bytes.size()));
            Stream.flush();
            if (!Stream)
            {
                Stream.close();
                ::DeleteFileW(Temporary.c_str());
                return false;
            }
        }
        if (!::MoveFileExW(
            Temporary.c_str(),
            Path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            ::DeleteFileW(Temporary.c_str());
            return false;
        }
        return true;
    }

    // Whether the store already holds entries. A fresh vault can start over
    // when its key is missing; a used one cannot, and saying so is the whole
    // point of Incompatible and KeyLost.
    bool VaultHasData()
    {
        std::filesystem::path Root = NanaZip::Vault::VaultSettings::RootDir();
        if (Root.empty())
        {
            return false;
        }
        std::error_code ErrorCode;
        auto MetaSize = std::filesystem::file_size(
            Root / L"vault.dat",
            ErrorCode);
        if (!ErrorCode && MetaSize > 0)
        {
            return true;
        }
        for (std::filesystem::directory_iterator It(
                Root / L"secrets",
                ErrorCode), End;
            !ErrorCode && It != End;
            It.increment(ErrorCode))
        {
            std::error_code FileError;
            if (It->is_regular_file(FileError) && !FileError)
            {
                return true;
            }
        }
        return false;
    }

    KeyGateStatus FromCredentialStatus(
        _In_ Credentials::KeyCredentialStatus Status)
    {
        switch (Status)
        {
        case Credentials::KeyCredentialStatus::UserCanceled:
        case Credentials::KeyCredentialStatus::UserPrefersPassword:
            return KeyGateStatus::Declined;
        case Credentials::KeyCredentialStatus::NotFound:
            // Only reachable on a sign: the credential vanished between the
            // open and the prompt.
            return KeyGateStatus::KeyLost;
        default:
            return KeyGateStatus::Failed;
        }
    }

    bool HashPublicKey(
        _In_ Credentials::KeyCredential const& Credential,
        _Out_writes_bytes_all_(g_FieldSize) std::uint8_t* Hash)
    {
        // Whatever encoding the API returns is fine here: what matters is
        // that the same credential always hashes to the same bytes.
        Streams::IBuffer PublicKey = Credential.RetrievePublicKey();
        return NanaZip::Vault::Crypto::Sha256(
            PublicKey.data(),
            PublicKey.Length(),
            Hash);
    }

    bool DeriveKey(
        _In_ Streams::IBuffer const& Signature,
        _In_ KeyFile const& File,
        _Out_ NanaZip::Vault::VaultKey& Key,
        _Out_writes_bytes_all_(g_FieldSize) std::uint8_t* Check)
    {
        if (!NanaZip::Vault::Crypto::HkdfSha256(
            Signature.data(),
            Signature.Length(),
            File.Salt.data(),
            File.Salt.size(),
            reinterpret_cast<std::uint8_t const*>(g_KekInfo),
            sizeof(g_KekInfo) - 1,
            Key.Bytes))
        {
            return false;
        }
        return NanaZip::Vault::Crypto::HmacSha256(
            Key.Bytes,
            NanaZip::Vault::VaultKey::Size,
            reinterpret_cast<std::uint8_t const*>(g_KeyCheckLabel),
            sizeof(g_KeyCheckLabel) - 1,
            Check);
    }
}

Foundation::IAsyncOperation<std::int32_t>
NanaZip::Vault::KeyGate::UnlockAsync()
{
    KeyFile File;
    bool HasKeyFile = LoadKeyFile(File);
    bool HasData = VaultHasData();

    // Entries without their key file have nothing left to unlock them with,
    // and starting a new vault over them would hide the loss.
    if (!HasKeyFile && HasData)
    {
        NanaZip::Vault::VaultSettings::WriteAudit("incompatible");
        co_return static_cast<std::int32_t>(KeyGateStatus::Incompatible);
    }

    Credentials::KeyCredential Credential = nullptr;
    bool Created = false;
    // Allow the credential broker (a separate process) to take the foreground
    // when its prompt appears; otherwise the foreground lock we hold via the
    // dialog's initial SetForegroundWindow blocks it and a stray window wins.
    AllowCredentialForeground();
    Credentials::KeyCredentialRetrievalResult Opened =
        co_await Credentials::KeyCredentialManager::OpenAsync(
            winrt::hstring(g_CredentialName));
    if (Opened.Status() == Credentials::KeyCredentialStatus::Success)
    {
        Credential = Opened.Credential();
    }
    else if (Opened.Status() == Credentials::KeyCredentialStatus::NotFound)
    {
        // No credential: a fresh vault creates one, a used vault lost it.
        if (HasData)
        {
            NanaZip::Vault::VaultSettings::WriteAudit("key-lost");
            co_return static_cast<std::int32_t>(KeyGateStatus::KeyLost);
        }
        AllowCredentialForeground();
        Credentials::KeyCredentialRetrievalResult CreatedResult =
            co_await Credentials::KeyCredentialManager::RequestCreateAsync(
                winrt::hstring(g_CredentialName),
                Credentials::KeyCredentialCreationOption::FailIfExists);
        if (CreatedResult.Status() != Credentials::KeyCredentialStatus::Success)
        {
            KeyGateStatus Status =
                FromCredentialStatus(CreatedResult.Status());
            NanaZip::Vault::VaultSettings::WriteAudit(
                Status == KeyGateStatus::Declined
                    ? "create-declined"
                    : "create-failed");
            co_return static_cast<std::int32_t>(Status);
        }
        Credential = CreatedResult.Credential();
        Created = true;
    }
    else
    {
        KeyGateStatus Status = FromCredentialStatus(Opened.Status());
        NanaZip::Vault::VaultSettings::WriteAudit(
            Status == KeyGateStatus::Declined
                ? "unlock-declined"
                : "unlock-failed");
        co_return static_cast<std::int32_t>(Status);
    }
    if (!Credential)
    {
        co_return static_cast<std::int32_t>(KeyGateStatus::Failed);
    }

    // Compared before signing, so a credential that was replaced is reported
    // as lost without putting a pointless prompt in front of the user.
    std::array<std::uint8_t, g_FieldSize> PublicKeyHash = {};
    if (!HashPublicKey(Credential, PublicKeyHash.data()))
    {
        co_return static_cast<std::int32_t>(KeyGateStatus::Failed);
    }

    bool WriteKeyFile = false;
    if (!HasKeyFile)
    {
        WriteKeyFile = true;
    }
    else if (0 != std::memcmp(
        File.PublicKeyHash.data(),
        PublicKeyHash.data(),
        g_FieldSize))
    {
        if (HasData)
        {
            NanaZip::Vault::VaultSettings::WriteAudit("key-lost");
            co_return static_cast<std::int32_t>(KeyGateStatus::KeyLost);
        }
        // The folder holds no entries, so binding it to the credential that
        // replaced the old one loses nothing.
        WriteKeyFile = true;
    }
    if (WriteKeyFile)
    {
        if (!NanaZip::Vault::Keystream::RandomBytes(
                File.Challenge.data(),
                File.Challenge.size())
            || !NanaZip::Vault::Keystream::RandomBytes(
                File.Salt.data(),
                File.Salt.size()))
        {
            co_return static_cast<std::int32_t>(KeyGateStatus::Failed);
        }
        File.PublicKeyHash = PublicKeyHash;
    }

    Streams::IBuffer Challenge =
        Cryptography::CryptographicBuffer::CreateFromByteArray(
            File.Challenge);
    Credentials::KeyCredentialOperationResult Signed =
        co_await Credential.RequestSignAsync(Challenge);
    if (Signed.Status() != Credentials::KeyCredentialStatus::Success)
    {
        KeyGateStatus Status = FromCredentialStatus(Signed.Status());
        NanaZip::Vault::VaultSettings::WriteAudit(
            Status == KeyGateStatus::Declined
                ? "unlock-declined"
                : "sign-failed");
        co_return static_cast<std::int32_t>(Status);
    }

    NanaZip::Vault::VaultKey Key;
    std::array<std::uint8_t, g_FieldSize> Check = {};
    if (!DeriveKey(Signed.Result(), File, Key, Check.data()))
    {
        co_return static_cast<std::int32_t>(KeyGateStatus::Failed);
    }

    if (WriteKeyFile)
    {
        File.KeyCheck = Check;
        if (!SaveKeyFile(File))
        {
            co_return static_cast<std::int32_t>(KeyGateStatus::Failed);
        }
    }
    else if (0 != std::memcmp(
        File.KeyCheck.data(),
        Check.data(),
        g_FieldSize))
    {
        // The credential signed the same challenge differently, so the key
        // it yields is not the one the blobs were sealed with.
        NanaZip::Vault::VaultSettings::WriteAudit("key-changed");
        co_return static_cast<std::int32_t>(KeyGateStatus::KeyLost);
    }

    if (!NanaZip::Vault::VaultCore::GetSession().Unlock(Key))
    {
        NanaZip::Vault::VaultSettings::WriteAudit("unlock-failed");
        co_return static_cast<std::int32_t>(KeyGateStatus::Failed);
    }
    NanaZip::Vault::VaultSettings::WriteAudit(
        Created ? "unlock-created" : "unlock");
    co_return static_cast<std::int32_t>(KeyGateStatus::Unlocked);
}

KeyGateStatus NanaZip::Vault::KeyGate::UnlockOnUiThread(
    _In_opt_ HWND Window)
{
    if (!InitApartment())
    {
        return KeyGateStatus::Failed;
    }

    // The Hello prompt takes the foreground, but every other window of this
    // thread stays live. On the deferred-save path the owner sits in its own
    // WM_CREATE chain, so disabling gives modality.
    DisabledOwnerGuard OwnerGuard(Window);

    return static_cast<KeyGateStatus>(PumpUntilDone(
        UnlockAsync(),
        static_cast<std::int32_t>(KeyGateStatus::Failed)));
}

bool NanaZip::Vault::KeyGate::IsAvailableOnUiThread()
{
    if (!InitApartment())
    {
        return false;
    }
    return PumpUntilDone(
        Credentials::KeyCredentialManager::IsSupportedAsync(),
        false);
}
