/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       ArchiveKey.cpp
 * PURPOSE:    Implementation for the archive content fingerprint
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "ArchiveKey.h"

#include <cstdint>
#include <vector>

namespace
{
    // CryptoAPI rather than BCrypt, so the vault keeps one hash provider.
    bool Sha256(
        _In_reads_bytes_(Size) std::uint8_t const* Data,
        _In_ size_t Size,
        _Out_writes_bytes_all_(32) std::uint8_t* Output)
    {
        HCRYPTPROV Provider = 0;
        HCRYPTHASH Hash = 0;
        if (!::CryptAcquireContextW(
            &Provider,
            nullptr,
            MS_ENH_RSA_AES_PROV_W,
            PROV_RSA_AES,
            CRYPT_VERIFYCONTEXT))
        {
            return false;
        }
        if (!::CryptCreateHash(Provider, CALG_SHA_256, 0, 0, &Hash))
        {
            ::CryptReleaseContext(Provider, 0);
            return false;
        }
        bool Succeeded = ::CryptHashData(
            Hash,
            const_cast<BYTE*>(Data),
            static_cast<DWORD>(Size),
            0);
        if (Succeeded)
        {
            DWORD HashSize = 32;
            Succeeded = (TRUE == ::CryptGetHashParam(
                Hash,
                HP_HASHVAL,
                Output,
                &HashSize,
                0)) && (HashSize == 32);
        }
        ::CryptDestroyHash(Hash);
        ::CryptReleaseContext(Provider, 0);
        return Succeeded;
    }

    bool ReadRange(
        _In_ HANDLE File,
        _In_ std::uint64_t Offset,
        _In_ size_t Count,
        _Out_writes_bytes_all_(Count) std::uint8_t* Buffer)
    {
        LARGE_INTEGER Move;
        Move.QuadPart = static_cast<LONGLONG>(Offset);
        if (!::SetFilePointerEx(File, Move, nullptr, FILE_BEGIN))
        {
            return false;
        }
        DWORD Remaining = static_cast<DWORD>(Count);
        while (Remaining > 0)
        {
            DWORD Read = 0;
            if (!::ReadFile(File, Buffer, Remaining, &Read, nullptr))
            {
                return false;
            }
            if (Read == 0)
            {
                return false;
            }
            Buffer += Read;
            Remaining -= Read;
        }
        return true;
    }

    std::wstring ToHex(_In_reads_bytes_(32) std::uint8_t const* Bytes)
    {
        static wchar_t const Digits[] = L"0123456789abcdef";
        wchar_t Text[65] = {};
        for (size_t Index = 0; Index < 32; ++Index)
        {
            Text[Index * 2] = Digits[Bytes[Index] >> 4];
            Text[Index * 2 + 1] = Digits[Bytes[Index] & 0x0F];
        }
        return std::wstring(Text, 64);
    }
}

std::optional<std::wstring>
NanaZip::Vault::ArchiveKey::Compute(_In_ std::wstring const& FilePath)
{
    HANDLE File = ::CreateFileW(
        FilePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (File == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }

    LARGE_INTEGER Size;
    if (!::GetFileSizeEx(File, &Size))
    {
        ::CloseHandle(File);
        return std::nullopt;
    }

    std::uint8_t Digest[32] = {};
    bool Succeeded = false;

    if (static_cast<std::uint64_t>(Size.QuadPart) <= FullHashThreshold)
    {
        // Small enough to hash whole. The vector is zero-initialized, so a
        // zero-length file hashes the empty input.
        std::vector<std::uint8_t> Buffer(
            static_cast<size_t>(Size.QuadPart), 0);
        if (!Buffer.empty() &&
            !ReadRange(File, 0, Buffer.size(), Buffer.data()))
        {
            ::CloseHandle(File);
            return std::nullopt;
        }
        Succeeded = Sha256(Buffer.data(), Buffer.size(), Digest);
    }
    else
    {
        // Large file: sample head, tail and size. Every supported format keeps
        // its central directory at the tail, so the last window catches edits.
        std::vector<std::uint8_t> Buffer(
            static_cast<size_t>(SampleWindow) * 2 + 8, 0);
        if (!ReadRange(File, 0, SampleWindow, Buffer.data()))
        {
            ::CloseHandle(File);
            return std::nullopt;
        }
        std::uint64_t TailOffset =
            static_cast<std::uint64_t>(Size.QuadPart) - SampleWindow;
        if (!ReadRange(
            File,
            TailOffset,
            SampleWindow,
            Buffer.data() + SampleWindow))
        {
            ::CloseHandle(File);
            return std::nullopt;
        }
        for (int Index = 0; Index < 8; ++Index)
        {
            Buffer[SampleWindow * 2 + Index] =
                static_cast<std::uint8_t>(
                    (static_cast<std::uint64_t>(Size.QuadPart) >>
                        (Index * 8)) & 0xFF);
        }
        Succeeded = Sha256(Buffer.data(), Buffer.size(), Digest);
    }

    ::CloseHandle(File);
    if (!Succeeded)
    {
        return std::nullopt;
    }
    return ToHex(Digest);
}
