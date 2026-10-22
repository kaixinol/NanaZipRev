/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       VaultCore.h
 * PURPOSE:    Definition for the vault core
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_CORE
#define NANAZIP_VAULT_CORE

#include <Windows.h>

#include <optional>
#include <string>
#include <vector>

#include "Session.h"
#include "VaultStore.h"

namespace NanaZip::Vault
{
    // One ready-to-offer password entry. It carries no secret: the index is
    // plaintext, so listing entries never needs an unlocked session.
    struct VaultPasswordSuggestion
    {
        std::wstring Label;
        std::wstring Id;
    };

    // Entry points for the archive password vault. The process-wide crash
    // and mitigation setup attaches on first use, not at start-up.
    class VaultCore
    {
    public:
        // Entries whose content fingerprint matches ArchiveKey. A locked
        // session still lists them: the index is plaintext, the secret is not.
        static std::vector<VaultPasswordSuggestion> QueryPasswordSuggestions(
            _In_ std::wstring const& ArchiveKey);

        // The decrypted password for ArchiveKey with no prompt at all, and
        // only while the session is already unlocked.
        static std::optional<std::wstring> TryAutoFill(
            _In_ std::wstring const& ArchiveKey);

        // Stores a password under ArchiveKey, along with the absolute path
        // the save came from. ConsentMessage is what the Hello prompt shows;
        // this library holds no resources of its own.
        static bool SavePassword(
            _In_ std::wstring const& ArchiveKey,
            _In_ std::wstring const& ArchivePath,
            _In_ std::wstring const& Label,
            _In_ std::wstring const& Password,
            _In_ std::wstring const& ConsentMessage,
            _In_opt_ HWND Window);

        // Decrypts one entry, prompting Hello first when the session is
        // locked. Nothing when the vault is off, refused or unreadable.
        static std::optional<std::wstring>
        RevealSuggestionPassword(
            _In_ std::wstring const& Id,
            _In_ std::wstring const& ConsentMessage,
            _In_opt_ HWND Window);

        // The one session every caller shares, so dialog and query agree.
        static Session& GetSession();

        // One secret field of one entry, as a wide string, and only while the
        // session is unlocked. Nothing when the vault is locked, the blob is
        // missing, it belongs to another account, or it is not whole chars.
        static std::optional<std::wstring> ReadSecret(
            _In_ VaultStore const& Store,
            _In_ std::wstring const& Id,
            _In_ std::wstring const& Field);

    private:
        // Attaches the crash and mitigation settings, once, from GetSession.
        static void Initialize();

        // The unhandled exception filter; nothing else reaches it.
        static LONG WINAPI CrashFilter(_In_ PEXCEPTION_POINTERS Information);
    };
}

#endif // !NANAZIP_VAULT_CORE
