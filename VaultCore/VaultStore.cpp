/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       VaultStore.cpp
 * PURPOSE:    Implementation for the vault storage
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "VaultStore.h"

#include "Keystream.h"

#include <algorithm>
#include <cctype>
#include <dpapi.h>
#include <fstream>
#include <iterator>
#include <system_error>

namespace
{
    // The index is UTF-8 text: a std::wofstream converts through the locale
    // and turns every non-ASCII title into question marks on another machine.
    std::string ToUtf8(_In_ std::wstring const& Text)
    {
        if (Text.empty())
        {
            return std::string();
        }
        int Size = ::WideCharToMultiByte(
            CP_UTF8,
            0,
            Text.data(),
            static_cast<int>(Text.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (Size <= 0)
        {
            return std::string();
        }
        std::string Result(static_cast<size_t>(Size), '\0');
        ::WideCharToMultiByte(
            CP_UTF8,
            0,
            Text.data(),
            static_cast<int>(Text.size()),
            &Result[0],
            Size,
            nullptr,
            nullptr);
        return Result;
    }

    std::wstring FromUtf8(_In_ std::string const& Bytes)
    {
        if (Bytes.empty())
        {
            return std::wstring();
        }
        int Size = ::MultiByteToWideChar(
            CP_UTF8,
            0,
            Bytes.data(),
            static_cast<int>(Bytes.size()),
            nullptr,
            0);
        if (Size <= 0)
        {
            return std::wstring();
        }
        std::wstring Result(static_cast<size_t>(Size), L'\0');
        ::MultiByteToWideChar(
            CP_UTF8,
            0,
            Bytes.data(),
            static_cast<int>(Bytes.size()),
            &Result[0],
            Size);
        return Result;
    }

    // The id is the file-name part of every secret path, so only the exact
    // shape NewEntryId writes is accepted.
    bool IsValidEntryId(_In_ std::wstring const& Id)
    {
        if (Id.size() != 32)
        {
            return false;
        }
        for (wchar_t Character : Id)
        {
            if (!((Character >= L'0' && Character <= L'9')
                || (Character >= L'a' && Character <= L'f')))
            {
                return false;
            }
        }
        return true;
    }

    // The blob field the path used to live in, kept only so deleting an
    // entry also clears what an older version wrote next to it.
    constexpr wchar_t const* LegacyPathField = L"path";
}

std::optional<NanaZip::Vault::VaultStore> NanaZip::Vault::VaultStore::Open()
{
    VaultSettings Settings = VaultSettings::Load();
    VaultStore Store;
    Store.m_Root = VaultSettings::RootDir();
    if (Store.m_Root.empty())
    {
        // Without a profile folder the relative path below would create a
        // secrets directory beside whatever the process was started in, and
        // the store would look healthy while its index goes nowhere stable.
        return std::nullopt;
    }
    Store.m_Audit = Settings.Audit;
    std::error_code ErrorCode;
    std::filesystem::create_directories(Store.m_Root / L"secrets", ErrorCode);
    if (ErrorCode)
    {
        return std::nullopt;
    }
    return Store;
}

std::filesystem::path NanaZip::Vault::VaultStore::MetaFile() const
{
    return this->m_Root / L"vault.dat";
}

std::filesystem::path NanaZip::Vault::VaultStore::SecretFile(
    _In_ std::wstring const& Id,
    _In_ std::wstring const& Field) const
{
    if (!IsValidEntryId(Id))
    {
        return {};
    }
    return this->m_Root / L"secrets" / (Id + L"." + Field + L".bin");
}

std::optional<std::wstring> NanaZip::Vault::VaultStore::NewEntryId()
{
    std::uint8_t Bytes[16] = {};
    if (!NanaZip::Vault::Keystream::RandomBytes(Bytes, sizeof(Bytes)))
    {
        return std::nullopt;
    }
    static wchar_t const Digits[] = L"0123456789abcdef";
    wchar_t Text[33] = {};
    for (size_t Index = 0; Index < sizeof(Bytes); ++Index)
    {
        Text[Index * 2] = Digits[Bytes[Index] >> 4];
        Text[Index * 2 + 1] = Digits[Bytes[Index] & 0x0F];
    }
    return Text;
}

// Zeroes and frees a DPAPI output blob on every path out, because the copy
// into a SecureBuffer can throw and leaked plaintext is not an option.
class LocalBlobGuard
{
public:
    explicit LocalBlobGuard(DATA_BLOB* Blob) :
        m_Blob(Blob)
    {
    }

    ~LocalBlobGuard()
    {
        ::SecureZeroMemory(this->m_Blob->pbData, this->m_Blob->cbData);
        ::LocalFree(this->m_Blob->pbData);
    }

private:
    DATA_BLOB* m_Blob;
};

// ---- DPAPI ----

std::optional<std::vector<std::uint8_t>>
NanaZip::Vault::VaultStore::Protect(_In_ SecureBuffer const& Plaintext) const
{
    SecureBuffer::Reader Reader = Plaintext.Read();
    DATA_BLOB In;
    In.cbData = static_cast<DWORD>(Reader.Size());
    In.pbData = const_cast<BYTE*>(
        reinterpret_cast<BYTE const*>(Reader.Data()));
    DATA_BLOB Out = {};
    DWORD Flags = CRYPTPROTECT_UI_FORBIDDEN;
    if (this->m_Audit)
    {
        Flags |= CRYPTPROTECT_AUDIT;
    }
    if (!::CryptProtectData(
        &In,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        Flags,
        &Out))
    {
        return std::nullopt;
    }
    std::vector<std::uint8_t> Blob(Out.pbData, Out.pbData + Out.cbData);
    ::LocalFree(Out.pbData);
    return Blob;
}

std::optional<NanaZip::Vault::SecureBuffer>
NanaZip::Vault::VaultStore::Unprotect(
    _In_ std::vector<std::uint8_t> const& Blob) const
{
    DATA_BLOB In;
    In.cbData = static_cast<DWORD>(Blob.size());
    In.pbData = const_cast<BYTE*>(Blob.data());
    DATA_BLOB Out = {};
    if (!::CryptUnprotectData(
        &In,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        CRYPTPROTECT_UI_FORBIDDEN,
        &Out))
    {
        // The Windows password was reset, or another account wrote the blob.
        return std::nullopt;
    }
    // The plaintext inside the DPAPI output is dead once the SecureBuffer
    // holds its own scrambled copy; the guard frees it on every path out.
    LocalBlobGuard Guard(&Out);
    return SecureBuffer::FromBytes(Out.pbData, Out.cbData);
}

// ---- index ----

std::wstring NanaZip::Vault::VaultStore::Escape(_In_ std::wstring const& Text)
{
    std::wstring Result;
    for (wchar_t Character : Text)
    {
        if (Character == L'\\')
        {
            Result += L"\\\\";
        }
        else if (Character == L'|')
        {
            Result += L"\\|";
        }
        else if (Character == L'\n')
        {
            Result += L"\\n";
        }
        else
        {
            Result += Character;
        }
    }
    return Result;
}

std::wstring NanaZip::Vault::VaultStore::Unescape(
    _In_ std::wstring const& Text)
{
    // Only the escapes Escape produces are recognized; a hand-edited line
    // with a stray backslash loses it.
    std::wstring Result;
    for (size_t Index = 0; Index < Text.size(); ++Index)
    {
        if (Text[Index] == L'\\' && Index + 1 < Text.size())
        {
            wchar_t Next = Text[++Index];
            if (Next == L'\\')
            {
                Result += L'\\';
            }
            else if (Next == L'|')
            {
                Result += L'|';
            }
            else if (Next == L'n')
            {
                Result += L'\n';
            }
        }
        else
        {
            Result += Text[Index];
        }
    }
    return Result;
}

std::vector<std::wstring> NanaZip::Vault::VaultStore::SplitFields(
    _In_ std::wstring const& Line)
{
    // Splits on unescaped pipes and leaves the escapes for Unescape.
    std::vector<std::wstring> Parts;
    std::wstring Current;
    for (size_t Index = 0; Index < Line.size(); ++Index)
    {
        if (Line[Index] == L'\\' && Index + 1 < Line.size())
        {
            Current += Line[Index];
            Current += Line[++Index];
        }
        else if (Line[Index] == L'|')
        {
            Parts.push_back(Current);
            Current.clear();
        }
        else
        {
            Current += Line[Index];
        }
    }
    Parts.push_back(Current);
    return Parts;
}

std::optional<std::vector<NanaZip::Vault::EntryMeta>>
NanaZip::Vault::VaultStore::TryListEntries() const
{
    std::vector<EntryMeta> Entries;
    std::error_code ErrorCode;
    if (!std::filesystem::exists(this->MetaFile(), ErrorCode))
    {
        // No index yet is the state before the first save, not a failure.
        return Entries;
    }
    std::ifstream File(this->MetaFile(), std::ios::binary);
    if (!File)
    {
        // There is an index and this process cannot get at it. An empty list
        // here would let the next rewrite replace the whole file with the one
        // entry it was about to add, orphaning every secret blob left out.
        return std::nullopt;
    }
    std::string Bytes(
        (std::istreambuf_iterator<char>(File)),
        std::istreambuf_iterator<char>());
    if (File.bad())
    {
        // A read that failed partway leaves the last line cut in half, and
        // the parser below drops such a line without a trace.
        return std::nullopt;
    }
    std::wstring Text = FromUtf8(Bytes);

    size_t Start = 0;
    while (Start <= Text.size())
    {
        size_t End = Text.find(L'\n', Start);
        std::wstring Line = Text.substr(
            Start,
            End == std::wstring::npos ? End : End - Start);
        if (!Line.empty() && Line.back() == L'\r')
        {
            Line.pop_back();
        }
        if (!Line.empty())
        {
            std::vector<std::wstring> Parts = SplitFields(Line);
            if (Parts.size() >= 3)
            {
                EntryMeta Entry;
                Entry.Id = Unescape(Parts[0]);
                if (IsValidEntryId(Entry.Id))
                {
                    Entry.ArchiveKey = Unescape(Parts[1]);
                    Entry.Label = Unescape(Parts[2]);
                    // Three fields is the shape written before Path existed.
                    // It reads with no path rather than dropping the entry.
                    if (Parts.size() >= 4)
                    {
                        Entry.Path = Unescape(Parts[3]);
                    }
                    Entries.push_back(std::move(Entry));
                }
            }
        }
        if (End == std::wstring::npos)
        {
            break;
        }
        Start = End + 1;
    }
    return Entries;
}

std::vector<NanaZip::Vault::EntryMeta>
NanaZip::Vault::VaultStore::ListEntries() const
{
    std::optional<std::vector<EntryMeta>> Listed = this->TryListEntries();
    if (!Listed)
    {
        return std::vector<EntryMeta>();
    }
    return std::move(*Listed);
}

// Serializes index rewrites between processes: Classic and Modern each run
// their own file manager over the same store. The handle dies with the
// process, so a crash cannot leave the store locked forever.

// Every caller sits on a UI thread, so the wait is bounded rather than
// infinite. An index rewrite is a few milliseconds of file traffic; a holder
// still here after this long is stuck, not busy, and is not worth freezing
// this process for.
static constexpr DWORD g_IndexWaitMs = 2000;

class HeldIndexMutex
{
public:
    HeldIndexMutex() :
        m_Handle(::CreateMutexW(
            nullptr,
            FALSE,
            L"Local\\NanaZipRev.Vault.Index")),
        m_Held(false)
    {
        if (this->m_Handle)
        {
            // WAIT_ABANDONED is still a successful wait: the atomic replace
            // below keeps the index readable after a holder died mid-write.
            DWORD Result = ::WaitForSingleObject(
                this->m_Handle,
                g_IndexWaitMs);
            this->m_Held = Result == WAIT_OBJECT_0 || Result == WAIT_ABANDONED;
        }
    }

    ~HeldIndexMutex()
    {
        if (this->m_Handle)
        {
            // A timed-out wait never owned the mutex, and releasing one
            // owned by the other process would hand it our lock state.
            if (this->m_Held)
            {
                ::ReleaseMutex(this->m_Handle);
            }
            ::CloseHandle(this->m_Handle);
        }
    }

    // False when the mutex could not be created or could not be taken in
    // time: the caller fails the write rather than race another file manager
    // into the same temporary file.
    bool Held() const
    {
        return this->m_Held;
    }

private:
    HANDLE m_Handle;
    bool m_Held;
};

bool NanaZip::Vault::VaultStore::WriteIndex(
    _In_ std::vector<EntryMeta> const& Entries) const
{
    // Replaced atomically: a half-written vault.dat would orphan the secret
    // blobs it stopped short of, so the lines land in a temporary file first.
    std::filesystem::path Temporary = this->MetaFile();
    Temporary += L".tmp";
    std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
    if (!File)
    {
        return false;
    }
    for (EntryMeta const& Entry : Entries)
    {
        std::string Line = ToUtf8(Escape(Entry.Id));
        Line += '|';
        Line += ToUtf8(Escape(Entry.ArchiveKey));
        Line += '|';
        Line += ToUtf8(Escape(Entry.Label));
        Line += '|';
        Line += ToUtf8(Escape(Entry.Path));
        Line += "\r\n";
        File.write(Line.data(), static_cast<std::streamsize>(Line.size()));
        if (!File)
        {
            File.close();
            ::DeleteFileW(Temporary.c_str());
            return false;
        }
    }
    File.flush();
    if (!File)
    {
        File.close();
        ::DeleteFileW(Temporary.c_str());
        return false;
    }
    File.close();
    // The stream flush only reached the OS cache. Flushing the raw handle
    // pushes the bytes as far as the move below pushes the name.
    HANDLE Raw = ::CreateFileW(
        Temporary.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (Raw != INVALID_HANDLE_VALUE)
    {
        ::FlushFileBuffers(Raw);
        ::CloseHandle(Raw);
    }
    if (!::MoveFileExW(
        Temporary.c_str(),
        this->MetaFile().c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        ::DeleteFileW(Temporary.c_str());
        return false;
    }
    return true;
}

bool NanaZip::Vault::VaultStore::UpsertEntry(
    _Inout_ EntryMeta& Meta,
    _In_ std::wstring const& Field,
    _In_ SecureBuffer const& Plaintext)
{
    // One read-modify-write of the index: the mutex keeps a second file
    // manager from landing its own entry between the list and the write.
    HeldIndexMutex Lock;
    if (!Lock.Held())
    {
        return false;
    }

    // A missing id means a new entry, and the generated one goes back into
    // Meta so the next field of the same entry does not start a second one.
    if (Meta.Id.empty())
    {
        std::optional<std::wstring> Generated = NewEntryId();
        if (!Generated.has_value())
        {
            // A fallback id would collide with the next save and replace it.
            return false;
        }
        Meta.Id = *Generated;
    }
    std::optional<std::vector<std::uint8_t>> Blob =
        this->Protect(Plaintext);
    if (!Blob)
    {
        return false;
    }
    {
        std::ofstream File(
            this->SecretFile(Meta.Id, Field),
            std::ios::binary | std::ios::trunc);
        if (!File)
        {
            return false;
        }
        File.write(
            reinterpret_cast<char const*>(Blob->data()),
            static_cast<std::streamsize>(Blob->size()));
        if (!File)
        {
            return false;
        }
    }

    // Rewritten whole: an upsert that only touched the secret file would
    // leave the key or label of a renamed entry behind on disk.
    std::optional<std::vector<EntryMeta>> Listed = this->TryListEntries();
    if (!Listed)
    {
        return false;
    }
    std::vector<EntryMeta>& Entries = *Listed;
    bool Found = false;
    for (EntryMeta& Entry : Entries)
    {
        if (Entry.Id == Meta.Id)
        {
            Entry = Meta;
            Found = true;
            break;
        }
    }
    if (!Found)
    {
        Entries.push_back(Meta);
    }
    return this->WriteIndex(Entries);
}

bool NanaZip::Vault::VaultStore::SetEntryPath(
    _In_ std::wstring const& Id,
    _In_ std::wstring const& Path)
{
    // The same read-modify-write as an upsert, less the secret: a path is
    // index text, so no blob is written and no password can be blanked.
    HeldIndexMutex Lock;
    if (!Lock.Held())
    {
        return false;
    }
    std::optional<std::vector<EntryMeta>> Listed = this->TryListEntries();
    if (!Listed)
    {
        return false;
    }
    std::vector<EntryMeta>& Entries = *Listed;
    bool Found = false;
    for (EntryMeta& Entry : Entries)
    {
        if (Entry.Id == Id)
        {
            Entry.Path = Path;
            Found = true;
            break;
        }
    }
    if (!Found)
    {
        return false;
    }
    return this->WriteIndex(Entries);
}

std::optional<NanaZip::Vault::SecureBuffer>
NanaZip::Vault::VaultStore::GetSecret(
    _In_ std::wstring const& Id,
    _In_ std::wstring const& Field) const
{
    std::ifstream File(this->SecretFile(Id, Field), std::ios::binary);
    if (!File)
    {
        return std::nullopt;
    }
    std::vector<std::uint8_t> Blob(
        (std::istreambuf_iterator<char>(File)),
        std::istreambuf_iterator<char>());
    if (Blob.empty())
    {
        return std::nullopt;
    }
    return this->Unprotect(Blob);
}

bool NanaZip::Vault::VaultStore::DeleteEntry(_In_ std::wstring const& Id)
{
    // The same read-modify-write shape as the upsert, and the same refusal.
    HeldIndexMutex Lock;
    if (!Lock.Held())
    {
        return false;
    }
    std::optional<std::vector<EntryMeta>> Listed = this->TryListEntries();
    if (!Listed)
    {
        return false;
    }
    std::vector<EntryMeta>& Entries = *Listed;
    std::vector<EntryMeta> Kept;
    bool Removed = false;
    for (EntryMeta const& Entry : Entries)
    {
        if (Entry.Id == Id)
        {
            Removed = true;
        }
        else
        {
            Kept.push_back(Entry);
        }
    }
    if (!Removed)
    {
        return false;
    }
    if (!this->WriteIndex(Kept))
    {
        return false;
    }
    std::error_code ErrorCode;
    std::filesystem::remove(
        this->SecretFile(Id, PasswordField),
        ErrorCode);
    std::filesystem::remove(
        this->SecretFile(Id, LegacyPathField),
        ErrorCode);
    return true;
}
