/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       VaultStore.h
 * PURPOSE:    Definition for the vault storage
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_STORE
#define NANAZIP_VAULT_STORE

#include <Windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "SecureBuffer.h"
#include "Settings.h"
#include "VaultKey.h"

namespace NanaZip::Vault
{
    // One entry of the index. The id addresses the encrypted password on
    // disk; the other three fields are index text.
    struct EntryMeta
    {
        std::wstring Id;
        std::wstring ArchiveKey;
        std::wstring Label;
        // The absolute path the entry was saved from. A fourth index field,
        // not a list column, and index text rather than a blob: reading the
        // vault folder names the archive without unlocking anything.
        std::wstring Path;
    };

    // The vault on disk: a plaintext index plus one AES-GCM blob per secret,
    // so one damaged entry does not take the vault down with it.
    class VaultStore
    {
    public:
        // The one secret field every entry has. It is the file name suffix
        // of the blob, so it is part of the on-disk format.
        static constexpr wchar_t const* PasswordField = L"password";

        // Opens, creating if needed. No Hello: the index is plaintext, and
        // the blobs are only useful with the session key.
        static std::optional<VaultStore> Open();

        // The entries in index order. An index that exists but cannot be
        // read shows as empty here; the rewrites below consult TryListEntries
        // instead, because for them empty and unreadable are not the same.
        std::vector<EntryMeta> ListEntries() const;

        // Writes or replaces one field of one entry. A missing id creates a
        // new entry and the generated one is written back into Meta.
        bool UpsertEntry(
            _Inout_ EntryMeta& Meta,
            _In_ std::wstring const& Field,
            _In_ SecureBuffer const& Plaintext,
            _In_ VaultKey const& Key);

        // Replaces the path text of one entry and nothing else: an archive
        // that moved is not a password that changed.
        bool SetEntryPath(
            _In_ std::wstring const& Id,
            _In_ std::wstring const& Path);

        bool DeleteEntry(_In_ std::wstring const& Id);

        // The decrypted secret, or nothing when the blob is missing or its
        // authentication tag does not match the key.
        std::optional<SecureBuffer> GetSecret(
            _In_ std::wstring const& Id,
            _In_ std::wstring const& Field,
            _In_ VaultKey const& Key) const;

        // A new entry id: 128 random bits as hex. Nothing when the entropy
        // source fails, because an all-zero id would collide with the next.
        static std::optional<std::wstring> NewEntryId();

    private:
        std::optional<std::vector<std::uint8_t>> Protect(
            _In_ std::wstring const& Id,
            _In_ std::wstring const& Field,
            _In_ SecureBuffer const& Plaintext,
            _In_ VaultKey const& Key) const;
        std::optional<SecureBuffer> Unprotect(
            _In_ std::wstring const& Id,
            _In_ std::wstring const& Field,
            _In_ std::vector<std::uint8_t> const& Blob,
            _In_ VaultKey const& Key) const;

        // The entries in index order, or nothing when the index is there but
        // could not be read. Only a rewrite needs the difference.
        std::optional<std::vector<EntryMeta>> TryListEntries() const;

        bool WriteIndex(_In_ std::vector<EntryMeta> const& Entries) const;

        static std::wstring Escape(_In_ std::wstring const& Text);
        static std::wstring Unescape(_In_ std::wstring const& Text);
        static std::vector<std::wstring> SplitFields(
            _In_ std::wstring const& Line);

        std::filesystem::path MetaFile() const;
        std::filesystem::path SecretFile(
            _In_ std::wstring const& Id,
            _In_ std::wstring const& Field) const;

        std::filesystem::path m_Root;
    };
}

#endif // !NANAZIP_VAULT_STORE
