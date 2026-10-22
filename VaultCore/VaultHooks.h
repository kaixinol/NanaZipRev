// VaultHooks.h
#pragma once

#include <Windows.h>

#include <string>
#include <utility>
#include <vector>

// Optional vault-password hooks: the FM layer registers them at startup and
// leaves them nullptr otherwise, so nothing below the UI layer needs a VaultCore
// include or a link dependency. Each takes the archive path and fingerprints
// the content itself.
//
// These typedefs and the matching globals live in exactly one place (here) and
// are mirrored by a single definition site in ExtractCallback.cpp, so a signature
// change only ever touches this file instead of every UI-tree consumer.
using FNVaultHasMatchFn =
    void (*)(const std::wstring& ArchiveKey, bool& HasMatch);
using FNVaultAutoFillFn =
    bool (*)(const std::wstring& ArchiveKey, std::wstring& Out);
using FNVaultListFn =
    std::vector<std::pair<std::wstring, std::wstring>> (*)(void);
using FNVaultRevealFn =
    bool (*)(const std::wstring& EntryId, HWND Window, std::wstring& Out);
using FNVaultSaveFn =
    bool (*)(const std::wstring& ArchiveKey,
             const std::wstring& Label,
             const std::wstring& Password,
             HWND Window);

extern FNVaultHasMatchFn g_VaultHasMatchHook;
extern FNVaultAutoFillFn g_VaultAutoFillHook;
extern FNVaultListFn g_VaultListHook;
extern FNVaultRevealFn g_VaultRevealHook;
extern FNVaultSaveFn g_VaultSaveHook;
