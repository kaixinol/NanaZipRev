/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       HelloGate.h
 * PURPOSE:    Definition for the Windows Hello gate
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_HELLO_GATE
#define NANAZIP_VAULT_HELLO_GATE

#include <Windows.h>

#include <string>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.UI.h>

namespace NanaZip::Vault
{
    // The Windows Hello gate. Every outcome but Verified is a refusal: a
    // cancelled prompt, a timeout and a missing device never read as success.
    class HelloGate
    {
    public:
        // Whether this device can verify the user at all; false is a reason
        // not to offer the vault. Throws only when the API is missing.
        static winrt::Windows::Foundation::IAsyncOperation<bool>
            IsAvailableAsync();

        // Prompts on the given window. Throws when the prompt cannot be shown
        // at all, which is a different thing from a prompt the user refused.
        static winrt::Windows::Foundation::IAsyncOperation<bool> VerifyAsync(
            _In_ HWND Window,
            _In_ std::wstring const& Message);
    };
}

#endif // !NANAZIP_VAULT_HELLO_GATE
