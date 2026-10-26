/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       VaultKey.h
 * PURPOSE:    Definition for a working copy of the vault data key
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_KEY
#define NANAZIP_VAULT_KEY

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace NanaZip::Vault
{
    // A plaintext copy that lives only while one store call runs. The
    // session keeps its own copy scrambled with the keystream.
    struct VaultKey
    {
        static constexpr std::size_t Size = 32;

        std::uint8_t Bytes[Size] = {};

        VaultKey()
        {
            this->Lock();
        }
        VaultKey(VaultKey const& Other)
        {
            std::memcpy(this->Bytes, Other.Bytes, Size);
            this->Lock();
        }
        VaultKey& operator=(VaultKey const& Other)
        {
            if (this != &Other)
            {
                std::memcpy(this->Bytes, Other.Bytes, Size);
            }
            return *this;
        }
        ~VaultKey()
        {
            ::SecureZeroMemory(this->Bytes, sizeof(this->Bytes));
            ::VirtualUnlock(this->Bytes, sizeof(this->Bytes));
        }

    private:
        // Keeps the key out of the page file. Best effort, like the buffer:
        // a working set that cannot grow is not a reason to fail a save.
        void Lock()
        {
            ::VirtualLock(this->Bytes, sizeof(this->Bytes));
        }
    };
}

#endif // !NANAZIP_VAULT_KEY
