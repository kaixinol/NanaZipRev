/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       Crypto.h
 * PURPOSE:    Definition for the vault's BCrypt primitives
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_CRYPTO
#define NANAZIP_VAULT_CRYPTO

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace NanaZip::Vault::Crypto
{
    // Shared by the key gate and the store, so the blob format is written
    // against one set of numbers.
    inline constexpr std::size_t Sha256Size = 32;
    inline constexpr std::size_t AesKeySize = 32;
    inline constexpr std::size_t GcmNonceSize = 12;
    inline constexpr std::size_t GcmTagSize = 16;

    bool Sha256(
        _In_reads_bytes_opt_(Size) std::uint8_t const* Data,
        _In_ std::size_t Size,
        _Out_writes_bytes_all_(Sha256Size) std::uint8_t* Output);

    bool HmacSha256(
        _In_reads_bytes_opt_(KeySize) std::uint8_t const* Key,
        _In_ std::size_t KeySize,
        _In_reads_bytes_opt_(DataSize) std::uint8_t const* Data,
        _In_ std::size_t DataSize,
        _Out_writes_bytes_all_(Sha256Size) std::uint8_t* Output);

    // RFC 5869. The output is exactly one hash block, which the vault's
    // single key needs; a longer output would take a counter loop.
    bool HkdfSha256(
        _In_reads_bytes_opt_(IkmSize) std::uint8_t const* Ikm,
        _In_ std::size_t IkmSize,
        _In_reads_bytes_opt_(SaltSize) std::uint8_t const* Salt,
        _In_ std::size_t SaltSize,
        _In_reads_bytes_opt_(InfoSize) std::uint8_t const* Info,
        _In_ std::size_t InfoSize,
        _Out_writes_bytes_all_(Sha256Size) std::uint8_t* Output);

    bool AesGcmEncrypt(
        _In_reads_bytes_(AesKeySize) std::uint8_t const* Key,
        _In_reads_bytes_(GcmNonceSize) std::uint8_t const* Nonce,
        _Out_writes_bytes_all_(GcmTagSize) std::uint8_t* Tag,
        _In_reads_bytes_opt_(AadSize) std::uint8_t const* Aad,
        _In_ std::size_t AadSize,
        _In_reads_bytes_opt_(PlaintextSize) std::uint8_t const* Plaintext,
        _In_ std::size_t PlaintextSize,
        _Out_writes_bytes_all_(PlaintextSize) std::uint8_t* Ciphertext);

    // False on a tag mismatch: the caller must treat its output buffer as
    // undefined, because a failed decrypt says nothing about its contents.
    bool AesGcmDecrypt(
        _In_reads_bytes_(AesKeySize) std::uint8_t const* Key,
        _In_reads_bytes_(GcmNonceSize) std::uint8_t const* Nonce,
        _In_reads_bytes_(GcmTagSize) std::uint8_t const* Tag,
        _In_reads_bytes_opt_(AadSize) std::uint8_t const* Aad,
        _In_ std::size_t AadSize,
        _In_reads_bytes_opt_(CiphertextSize) std::uint8_t const* Ciphertext,
        _In_ std::size_t CiphertextSize,
        _Out_writes_bytes_all_(CiphertextSize) std::uint8_t* Plaintext);
}

#endif // !NANAZIP_VAULT_CRYPTO
