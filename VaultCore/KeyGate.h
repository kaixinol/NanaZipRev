/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       KeyGate.h
 * PURPOSE:    Definition for the Windows Hello key gate
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_KEY_GATE
#define NANAZIP_VAULT_KEY_GATE

#include <Windows.h>

#include <cstdint>

#include <winrt/Windows.Foundation.h>

namespace NanaZip::Vault
{
    // Every outcome but Unlocked is a refusal the caller reports. Declined
    // is a prompt the user dismissed; KeyLost is a credential that no longer
    // matches; Incompatible is stored entries without a key file.
    enum class KeyGateStatus : std::int32_t
    {
        Unlocked = 0,
        Declined,
        KeyLost,
        Incompatible,
        Failed,
    };

    // Derives the vault key from the Windows Hello credential and unlocks
    // the shared session. The first call creates the credential, so it asks
    // Hello twice: once to create, once to sign.
    class KeyGate
    {
    public:
        // The status as a plain integer, so the coroutine does not need the
        // enum to be a WinRT type.
        static winrt::Windows::Foundation::IAsyncOperation<std::int32_t>
            UnlockAsync();

        // Runs UnlockAsync on the calling UI thread, keeping the owner
        // disabled while the Hello prompt is up.
        static KeyGateStatus UnlockOnUiThread(_In_opt_ HWND Window);

        // Whether this device can create a key credential at all; false is
        // a reason to keep the vault off.
        static bool IsAvailableOnUiThread();
    };
}

#endif // !NANAZIP_VAULT_KEY_GATE
