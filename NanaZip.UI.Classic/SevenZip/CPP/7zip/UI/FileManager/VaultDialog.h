/*
 * PROJECT:    NanaZip
 * FILE:       VaultDialog.h
 * PURPOSE:    Definition for Vault Dialog
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_FILEMANAGER_VAULTDIALOG
#define NANAZIP_FILEMANAGER_VAULTDIALOG

#if (defined(__cplusplus) && __cplusplus >= 201703L)
#elif (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#else
#error "[Mile] You should use a C++ compiler with the C++17 standard."
#endif

#include <Windows.h>

namespace NanaZip::FileManager::VaultDialog
{
    // Opens the vault, asking to enable it first while the setting is off.
    // The dialog's unlock coroutine outlives this call.
    void Show(
        _In_opt_ HWND ParentWindowHandle);

    // The one enable path: warning, Hello probe, then the setting. False
    // leaves the vault off.
    bool RequestEnable(
        _In_opt_ HWND ParentWindowHandle);
}

#endif // NANAZIP_FILEMANAGER_VAULTDIALOG
