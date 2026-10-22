/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       Settings.h
 * PURPOSE:    Definition for the vault settings
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_SETTINGS
#define NANAZIP_VAULT_SETTINGS

#include <Windows.h>

#include <filesystem>

namespace NanaZip::Vault
{
    // The user-facing vault settings, under their own key. The timers are
    // generous on purpose: re-arming Hello too soon feels like a fresh sign-in.
    struct VaultSettings
    {
        // Published so the settings dialog validates against the same numbers
        // the loader falls back on; a value outside them is rejected, not
        // clamped.
        static constexpr DWORD MinimumTimeoutMs = 1 * 1000;
        static constexpr DWORD MaximumTimeoutMs = 24 * 60 * 60 * 1000;
        static constexpr DWORD MinimumClipboardClearMs = 1 * 1000;
        static constexpr DWORD MaximumClipboardClearMs = 10 * 60 * 1000;

        // Ships off: enabling it is an explicit trust decision.
        bool Enabled = false;
        // Milliseconds from unlock to the unconditional re-lock.
        DWORD AbsoluteTimeoutMs = 15 * 60 * 1000;
        // Vault-dialog inactivity before the re-lock.
        DWORD IdleTimeoutMs = 5 * 60 * 1000;
        DWORD ClipboardClearMs = 30 * 1000;
        bool Audit = false;
        // Whether the password dialog offers to keep the password by default.
        // The user can uncheck it per entry; the vault exists to stop retyping.
        bool SavePasswordByDefault = true;

        // Reads HKCU\Software\NanaZipRev\Vault. A missing, mistyped or out of
        // range value falls back to the default.
        static VaultSettings Load();

        void Save() const;

        // %LOCALAPPDATA%\NanaZipRev\Vault. Empty when the profile folder
        // cannot be found, in which case the store has nowhere to live.
        static std::filesystem::path RootDir();
    };
}

#endif // !NANAZIP_VAULT_SETTINGS
