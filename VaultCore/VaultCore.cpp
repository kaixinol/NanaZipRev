/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       VaultCore.cpp
 * PURPOSE:    Implementation for the vault core
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "VaultCore.h"

#include <Windows.h>
#include <processthreadsapi.h>
#include <werapi.h>

#include "KeyGate.h"
#include "Keystream.h"
#include "SecureBuffer.h"
#include "Session.h"
#include "Settings.h"
#include "VaultStore.h"

namespace
{
    // The store lists in index order and UpsertEntry rewrites in place, so
    // the last match is the most recently saved password for that archive.
    NanaZip::Vault::EntryMeta const* LastMatch(
        _In_ std::vector<NanaZip::Vault::EntryMeta> const& Entries,
        _In_ std::wstring const& ArchiveKey)
    {
        NanaZip::Vault::EntryMeta const* Found = nullptr;
        for (NanaZip::Vault::EntryMeta const& Meta : Entries)
        {
            if (Meta.ArchiveKey == ArchiveKey)
            {
                Found = &Meta;
            }
        }
        return Found;
    }

}

LONG WINAPI NanaZip::Vault::VaultCore::CrashFilter(
    _In_ PEXCEPTION_POINTERS Information)
{
    UNREFERENCED_PARAMETER(Information);

    // Wiping the key is enough: every SecureBuffer holds XOR noise without
    // it, and a crash filter must not take the lock a buffer list would need.
    NanaZip::Vault::Keystream::EraseFromCrash();

    // Continue to the default handler, which is what produces the report.
    return EXCEPTION_CONTINUE_SEARCH;
}

void NanaZip::Vault::VaultCore::Initialize()
{
    // Keeps the process heap out of what Windows Error Reporting collects.
    ::WerSetFlags(WER_FAULT_REPORTING_FLAG_NOHEAP);

    ::SetUnhandledExceptionFilter(&VaultCore::CrashFilter);

    // Best effort: some flags need privileges we may not have. Child-process
    // creation stays allowed, because opening files needs ShellExecuteEx.
    {
        PROCESS_MITIGATION_ASLR_POLICY Policy = {};
        Policy.EnableForceRelocateImages = TRUE;
        Policy.EnableBottomUpRandomization = TRUE;
        Policy.EnableHighEntropy = TRUE;
        Policy.DisallowStrippedImages = TRUE;
        ::SetProcessMitigationPolicy(
            ProcessASLRPolicy,
            &Policy,
            sizeof(Policy));
    }
    {
        PROCESS_MITIGATION_DEP_POLICY Policy = {};
        Policy.Enable = TRUE;
        ::SetProcessMitigationPolicy(
            ProcessDEPPolicy,
            &Policy,
            sizeof(Policy));
    }
    {
        PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY Policy = {};
        Policy.DisableExtensionPoints = TRUE;
        ::SetProcessMitigationPolicy(
            ProcessExtensionPointDisablePolicy,
            &Policy,
            sizeof(Policy));
    }
    {
        PROCESS_MITIGATION_IMAGE_LOAD_POLICY Policy = {};
        Policy.NoRemoteImages = TRUE;
        Policy.NoLowMandatoryLabelImages = TRUE;
        Policy.PreferSystem32Images = TRUE;
        ::SetProcessMitigationPolicy(
            ProcessImageLoadPolicy,
            &Policy,
            sizeof(Policy));
    }
    // CFG needs no runtime call: the file managers are linked with
    // /guard:cf, so the loader enables it. Export suppression must never
    // ride along; it breaks the WPF based K7Modern.dll stack.
}

bool NanaZip::Vault::VaultCore::IsHelloAvailableOnUiThread()
{
    return NanaZip::Vault::KeyGate::IsAvailableOnUiThread();
}

NanaZip::Vault::Session& NanaZip::Vault::VaultCore::GetSession()
{
    // Attached here, not at start-up: they only matter once a session key
    // can exist, and every path to a key comes through this function.
    static bool Attached = false;
    if (!Attached)
    {
        Attached = true;
        VaultCore::Initialize();
    }

    // Constructed once with the settings stored at first use. The session
    // does not re-read Enabled, so turning the vault on still unlocks it.
    static NanaZip::Vault::Session SharedSession(
        NanaZip::Vault::VaultSettings::Load());
    static bool Started = false;
    if (!Started)
    {
        Started = true;
        SharedSession.Start();
    }
    return SharedSession;
}

std::optional<std::wstring>
NanaZip::Vault::VaultCore::ReadSecret(
    _In_ VaultStore const& Store,
    _In_ std::wstring const& Id,
    _In_ std::wstring const& Field)
{
    // The unlock is the gate on every secret, so it sits beside the read
    // instead of in each caller's memory.
    VaultKey Key;
    if (!NanaZip::Vault::VaultCore::GetSession().CopyKey(Key))
    {
        return std::nullopt;
    }

    std::optional<SecureBuffer> Blob = Store.GetSecret(Id, Field, Key);
    if (!Blob.has_value())
    {
        return std::nullopt;
    }
    SecureBuffer::Reader Reader = Blob->Read();
    if (!Reader.Data() || 0 != Reader.Size() % sizeof(wchar_t))
    {
        return std::nullopt;
    }
    return std::wstring(
        reinterpret_cast<wchar_t const*>(Reader.Data()),
        Reader.Size() / sizeof(wchar_t));
}

std::vector<NanaZip::Vault::VaultPasswordSuggestion>
NanaZip::Vault::VaultCore::QueryPasswordSuggestions(
    _In_ std::wstring const& ArchiveKey)
{
    std::vector<VaultPasswordSuggestion> Result;

    VaultSettings Settings = VaultSettings::Load();
    if (!Settings.Enabled)
    {
        return Result;
    }

    // No session gate: the store index is plaintext, the secret is not.
    std::optional<VaultStore> Store = VaultStore::Open();
    if (!Store.has_value())
    {
        return Result;
    }

    std::vector<EntryMeta> Entries = Store->ListEntries();

    // Exact match on the content fingerprint: a moved or renamed archive
    // still hits, a different one of the same name does not.
    for (EntryMeta const& Meta : Entries)
    {
        if (Meta.ArchiveKey == ArchiveKey)
        {
            VaultPasswordSuggestion Suggestion;
            Suggestion.Label = Meta.Label;
            Suggestion.Id = Meta.Id;
            Result.push_back(std::move(Suggestion));
        }
    }

    return Result;
}

std::vector<NanaZip::Vault::VaultPasswordSuggestion>
NanaZip::Vault::VaultCore::QueryAllPasswordSuggestions()
{
    std::vector<VaultPasswordSuggestion> Result;

    VaultSettings Settings = VaultSettings::Load();
    if (!Settings.Enabled)
    {
        return Result;
    }

    std::optional<VaultStore> Store = VaultStore::Open();
    if (!Store.has_value())
    {
        return Result;
    }

    // The last entry under a label is the one a new save would replace, so
    // it is what the list keeps. Blank labels are not identities: own row.
    std::vector<EntryMeta> Entries = Store->ListEntries();
    for (EntryMeta const& Meta : Entries)
    {
        bool Replaced = false;
        for (VaultPasswordSuggestion& Known : Result)
        {
            if (!Known.Label.empty() && Known.Label == Meta.Label)
            {
                Known.Id = Meta.Id;
                Replaced = true;
                break;
            }
        }
        if (!Replaced)
        {
            VaultPasswordSuggestion Suggestion;
            Suggestion.Label = Meta.Label;
            Suggestion.Id = Meta.Id;
            Result.push_back(std::move(Suggestion));
        }
    }

    return Result;
}

std::optional<std::wstring>
NanaZip::Vault::VaultCore::TryAutoFill(
    _In_ std::wstring const& ArchiveKey,
    _In_ std::wstring const& ArchivePath)
{
    VaultSettings Settings = VaultSettings::Load();
    if (!Settings.Enabled)
    {
        return std::nullopt;
    }

    // Auto-fill is silent by design. A locked session falls through to the
    // dialog, whose button is the gesture that arms the prompt.
    Session& Shared = NanaZip::Vault::VaultCore::GetSession();
    if (!Shared.IsUnlocked())
    {
        return std::nullopt;
    }

    std::optional<VaultStore> Store = VaultStore::Open();
    if (!Store.has_value())
    {
        return std::nullopt;
    }

    std::vector<EntryMeta> Entries = Store->ListEntries();
    EntryMeta const* Match = LastMatch(Entries, ArchiveKey);
    if (Match == nullptr)
    {
        return std::nullopt;
    }

    // A match means the file at ArchivePath is the archive this entry was
    // saved for, so the entry's path follows it: one saved before the path
    // was recorded gets one now, and an archive that moved stops showing
    // where it used to be. Best effort, because the password is what this
    // call owes the caller.
    if (!ArchivePath.empty() && Match->Path != ArchivePath)
    {
        Store->SetEntryPath(Match->Id, ArchivePath);
    }

    return ReadSecret(*Store, Match->Id, VaultStore::PasswordField);
}

bool NanaZip::Vault::VaultCore::SavePassword(
    _In_ std::wstring const& ArchiveKey,
    _In_ std::wstring const& ArchivePath,
    _In_ std::wstring const& Label,
    _In_ std::wstring const& Password,
    _In_opt_ HWND Window)
{
    VaultSettings Settings = VaultSettings::Load();
    if (!Settings.Enabled)
    {
        return false;
    }

    // The index is plaintext, so an existing entry is visible without
    // unlocking. Saving over it would clobber a password the user set in
    // the vault dialog, and prompting for a known archive is noise.
    std::optional<VaultStore> Store = VaultStore::Open();
    if (!Store.has_value())
    {
        return false;
    }

    std::vector<EntryMeta> Entries = Store->ListEntries();
    if (LastMatch(Entries, ArchiveKey) != nullptr)
    {
        return true;
    }

    // An entry sealed from an empty password decrypts to nothing the store
    // can hand back, so this path refuses one as the entry dialog does.
    if (Password.empty())
    {
        return false;
    }

    // A genuine new save. Writing a password is a trust gesture, so Hello
    // gates it; a session already warm from this process skips the prompt.
    Session& Shared = NanaZip::Vault::VaultCore::GetSession();
    VaultKey Key;
    if (!Shared.CopyKey(Key))
    {
        if (NanaZip::Vault::KeyGate::UnlockOnUiThread(Window)
            != NanaZip::Vault::KeyGateStatus::Unlocked)
        {
            return false;
        }
        // The unlock above hands the key to the session, so the copy that
        // follows is what the store call spends.
        if (!Shared.CopyKey(Key))
        {
            return false;
        }
    }

    EntryMeta Meta;
    Meta.ArchiveKey = ArchiveKey;
    Meta.Label = Label;
    Meta.Path = ArchivePath;

    SecureBuffer Buffer = SecureBuffer::FromString(Password);
    // An empty buffer from a non-empty password is a scramble failure;
    // saving it would silently blank the entry.
    if (!Password.empty() && Buffer.Size() == 0)
    {
        return false;
    }
    return Store->UpsertEntry(
        Meta,
        VaultStore::PasswordField,
        Buffer,
        Key);
}

std::optional<std::wstring>
NanaZip::Vault::VaultCore::RevealSuggestionPassword(
    _In_ std::wstring const& Id,
    _In_opt_ HWND Window)
{
    VaultSettings Settings = VaultSettings::Load();
    if (!Settings.Enabled)
    {
        return std::nullopt;
    }

    Session& Shared = NanaZip::Vault::VaultCore::GetSession();
    if (!Shared.IsUnlocked())
    {
        // The button click is the trust gesture, so this is where Hello
        // fires. UnlockOnUiThread keeps the owner disabled while it is up.
        if (NanaZip::Vault::KeyGate::UnlockOnUiThread(Window)
            != NanaZip::Vault::KeyGateStatus::Unlocked)
        {
            return std::nullopt;
        }
    }

    std::optional<VaultStore> Store = VaultStore::Open();
    if (!Store.has_value())
    {
        return std::nullopt;
    }

    return ReadSecret(*Store, Id, VaultStore::PasswordField);
}
