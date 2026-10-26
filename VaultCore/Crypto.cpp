/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       Crypto.cpp
 * PURPOSE:    Implementation for the vault's BCrypt primitives
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "Crypto.h"

#include <bcrypt.h>

#include <cstring>
#include <vector>

namespace
{
    // Closes the provider on every path out, including the early ones where
    // a property could not be set.
    class AlgorithmProvider
    {
    public:
        AlgorithmProvider(_In_ LPCWSTR Name, _In_ ULONG Flags)
        {
            if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(
                &this->m_Handle,
                Name,
                nullptr,
                Flags)))
            {
                this->m_Handle = nullptr;
            }
        }

        ~AlgorithmProvider()
        {
            if (this->m_Handle)
            {
                ::BCryptCloseAlgorithmProvider(this->m_Handle, 0);
            }
        }

        AlgorithmProvider(AlgorithmProvider const&) = delete;
        AlgorithmProvider& operator=(AlgorithmProvider const&) = delete;

        explicit operator bool() const
        {
            return this->m_Handle != nullptr;
        }

        BCRYPT_ALG_HANDLE Get() const
        {
            return this->m_Handle;
        }

    private:
        BCRYPT_ALG_HANDLE m_Handle = nullptr;
    };

    class HashHandle
    {
    public:
        HashHandle(
            _In_ BCRYPT_ALG_HANDLE Algorithm,
            _In_reads_bytes_opt_(SecretSize) std::uint8_t const* Secret,
            _In_ std::size_t SecretSize)
        {
            if (!BCRYPT_SUCCESS(::BCryptCreateHash(
                Algorithm,
                &this->m_Handle,
                nullptr,
                0,
                const_cast<PUCHAR>(Secret),
                static_cast<ULONG>(SecretSize),
                0)))
            {
                this->m_Handle = nullptr;
            }
        }

        ~HashHandle()
        {
            if (this->m_Handle)
            {
                ::BCryptDestroyHash(this->m_Handle);
            }
        }

        HashHandle(HashHandle const&) = delete;
        HashHandle& operator=(HashHandle const&) = delete;

        explicit operator bool() const
        {
            return this->m_Handle != nullptr;
        }

        BCRYPT_HASH_HANDLE Get() const
        {
            return this->m_Handle;
        }

    private:
        BCRYPT_HASH_HANDLE m_Handle = nullptr;
    };

    class SymmetricKey
    {
    public:
        SymmetricKey(
            _In_ BCRYPT_ALG_HANDLE Algorithm,
            _In_reads_bytes_(KeySize) std::uint8_t const* Key,
            _In_ std::size_t KeySize)
        {
            if (!BCRYPT_SUCCESS(::BCryptGenerateSymmetricKey(
                Algorithm,
                &this->m_Handle,
                nullptr,
                0,
                const_cast<PUCHAR>(Key),
                static_cast<ULONG>(KeySize),
                0)))
            {
                this->m_Handle = nullptr;
            }
        }

        ~SymmetricKey()
        {
            if (this->m_Handle)
            {
                ::BCryptDestroyKey(this->m_Handle);
            }
        }

        SymmetricKey(SymmetricKey const&) = delete;
        SymmetricKey& operator=(SymmetricKey const&) = delete;

        explicit operator bool() const
        {
            return this->m_Handle != nullptr;
        }

        BCRYPT_KEY_HANDLE Get() const
        {
            return this->m_Handle;
        }

    private:
        BCRYPT_KEY_HANDLE m_Handle = nullptr;
    };

    bool AppendAndFinish(
        _In_ BCRYPT_HASH_HANDLE Hash,
        _In_reads_bytes_opt_(Size) std::uint8_t const* Data,
        _In_ std::size_t Size,
        _Out_writes_bytes_all_(NanaZip::Vault::Crypto::Sha256Size)
            std::uint8_t* Output)
    {
        return BCRYPT_SUCCESS(::BCryptHashData(
            Hash,
            const_cast<PUCHAR>(Data),
            static_cast<ULONG>(Size),
            0))
            && BCRYPT_SUCCESS(::BCryptFinishHash(
                Hash,
                Output,
                static_cast<ULONG>(NanaZip::Vault::Crypto::Sha256Size),
                0));
    }

    // Opens the AES provider with GCM selected; the default chaining mode
    // is CBC, which would silently ignore the auth fields.
    bool OpenGcm(
        _Inout_ BCRYPT_ALG_HANDLE& Handle)
    {
        BCRYPT_ALG_HANDLE Algorithm = nullptr;
        if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(
            &Algorithm,
            BCRYPT_AES_ALGORITHM,
            nullptr,
            0)))
        {
            return false;
        }
        if (!BCRYPT_SUCCESS(::BCryptSetProperty(
            Algorithm,
            BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
            static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)),
            0)))
        {
            ::BCryptCloseAlgorithmProvider(Algorithm, 0);
            return false;
        }
        Handle = Algorithm;
        return true;
    }

    // Fills the structure BCrypt reads for an authenticated operation.
    void InitGcm(
        _Out_ BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO& Info,
        _In_reads_bytes_(NanaZip::Vault::Crypto::GcmNonceSize)
            std::uint8_t const* Nonce,
        _Inout_updates_bytes_all_(NanaZip::Vault::Crypto::GcmTagSize)
            std::uint8_t* Tag,
        _In_reads_bytes_opt_(AadSize) std::uint8_t const* Aad,
        _In_ std::size_t AadSize)
    {
        BCRYPT_INIT_AUTH_MODE_INFO(Info);
        Info.pbNonce = const_cast<PUCHAR>(Nonce);
        Info.cbNonce = static_cast<ULONG>(NanaZip::Vault::Crypto::GcmNonceSize);
        Info.pbTag = Tag;
        Info.cbTag = static_cast<ULONG>(NanaZip::Vault::Crypto::GcmTagSize);
        Info.pbAuthData = const_cast<PUCHAR>(Aad);
        Info.cbAuthData = static_cast<ULONG>(AadSize);
    }
}

bool NanaZip::Vault::Crypto::Sha256(
    _In_reads_bytes_opt_(Size) std::uint8_t const* Data,
    _In_ std::size_t Size,
    _Out_writes_bytes_all_(Sha256Size) std::uint8_t* Output)
{
    AlgorithmProvider Algorithm(BCRYPT_SHA256_ALGORITHM, 0);
    if (!Algorithm)
    {
        return false;
    }
    HashHandle Hash(Algorithm.Get(), nullptr, 0);
    if (!Hash)
    {
        return false;
    }
    return AppendAndFinish(Hash.Get(), Data, Size, Output);
}

bool NanaZip::Vault::Crypto::HmacSha256(
    _In_reads_bytes_opt_(KeySize) std::uint8_t const* Key,
    _In_ std::size_t KeySize,
    _In_reads_bytes_opt_(DataSize) std::uint8_t const* Data,
    _In_ std::size_t DataSize,
    _Out_writes_bytes_all_(Sha256Size) std::uint8_t* Output)
{
    // The HMAC flag makes the provider treat the secret given to
    // BCryptCreateHash as the key instead of rejecting it.
    AlgorithmProvider Algorithm(
        BCRYPT_SHA256_ALGORITHM,
        BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!Algorithm)
    {
        return false;
    }
    HashHandle Hash(Algorithm.Get(), Key, KeySize);
    if (!Hash)
    {
        return false;
    }
    return AppendAndFinish(Hash.Get(), Data, DataSize, Output);
}

bool NanaZip::Vault::Crypto::HkdfSha256(
    _In_reads_bytes_opt_(IkmSize) std::uint8_t const* Ikm,
    _In_ std::size_t IkmSize,
    _In_reads_bytes_opt_(SaltSize) std::uint8_t const* Salt,
    _In_ std::size_t SaltSize,
    _In_reads_bytes_opt_(InfoSize) std::uint8_t const* Info,
    _In_ std::size_t InfoSize,
    _Out_writes_bytes_all_(Sha256Size) std::uint8_t* Output)
{
    // RFC 5869 with a one-block output: extract the pseudo-random key, then
    // the first expand block is HMAC(PRK, info || 0x01).
    std::uint8_t Prk[Sha256Size] = {};
    bool Succeeded = HmacSha256(Salt, SaltSize, Ikm, IkmSize, Prk);
    if (Succeeded)
    {
        std::vector<std::uint8_t> Block(InfoSize + 1, 0);
        if (InfoSize > 0)
        {
            std::memcpy(Block.data(), Info, InfoSize);
        }
        Block[InfoSize] = 1;
        Succeeded = HmacSha256(
            Prk,
            sizeof(Prk),
            Block.data(),
            Block.size(),
            Output);
        ::SecureZeroMemory(Block.data(), Block.size());
    }
    ::SecureZeroMemory(Prk, sizeof(Prk));
    return Succeeded;
}

bool NanaZip::Vault::Crypto::AesGcmEncrypt(
    _In_reads_bytes_(AesKeySize) std::uint8_t const* Key,
    _In_reads_bytes_(GcmNonceSize) std::uint8_t const* Nonce,
    _Out_writes_bytes_all_(GcmTagSize) std::uint8_t* Tag,
    _In_reads_bytes_opt_(AadSize) std::uint8_t const* Aad,
    _In_ std::size_t AadSize,
    _In_reads_bytes_opt_(PlaintextSize) std::uint8_t const* Plaintext,
    _In_ std::size_t PlaintextSize,
    _Out_writes_bytes_all_(PlaintextSize) std::uint8_t* Ciphertext)
{
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    if (!OpenGcm(Algorithm))
    {
        return false;
    }
    SymmetricKey Symmetric(Algorithm, Key, AesKeySize);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO Info = {};
    InitGcm(Info, Nonce, Tag, Aad, AadSize);
    ULONG Written = 0;
    bool Succeeded = Symmetric
        && BCRYPT_SUCCESS(::BCryptEncrypt(
            Symmetric.Get(),
            const_cast<PUCHAR>(Plaintext),
            static_cast<ULONG>(PlaintextSize),
            &Info,
            nullptr,
            0,
            Ciphertext,
            static_cast<ULONG>(PlaintextSize),
            &Written,
            0))
        && Written == PlaintextSize;
    ::BCryptCloseAlgorithmProvider(Algorithm, 0);
    return Succeeded;
}

bool NanaZip::Vault::Crypto::AesGcmDecrypt(
    _In_reads_bytes_(AesKeySize) std::uint8_t const* Key,
    _In_reads_bytes_(GcmNonceSize) std::uint8_t const* Nonce,
    _In_reads_bytes_(GcmTagSize) std::uint8_t const* Tag,
    _In_reads_bytes_opt_(AadSize) std::uint8_t const* Aad,
    _In_ std::size_t AadSize,
    _In_reads_bytes_opt_(CiphertextSize) std::uint8_t const* Ciphertext,
    _In_ std::size_t CiphertextSize,
    _Out_writes_bytes_all_(CiphertextSize) std::uint8_t* Plaintext)
{
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    if (!OpenGcm(Algorithm))
    {
        return false;
    }
    SymmetricKey Symmetric(Algorithm, Key, AesKeySize);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO Info = {};
    InitGcm(Info, Nonce, const_cast<PUCHAR>(Tag), Aad, AadSize);
    ULONG Written = 0;
    bool Succeeded = Symmetric
        && BCRYPT_SUCCESS(::BCryptDecrypt(
            Symmetric.Get(),
            const_cast<PUCHAR>(Ciphertext),
            static_cast<ULONG>(CiphertextSize),
            &Info,
            nullptr,
            0,
            Plaintext,
            static_cast<ULONG>(CiphertextSize),
            &Written,
            0))
        && Written == CiphertextSize;
    ::BCryptCloseAlgorithmProvider(Algorithm, 0);
    return Succeeded;
}
