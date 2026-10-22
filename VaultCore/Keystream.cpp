/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       Keystream.cpp
 * PURPOSE:    Implementation for the process-wide secret keystream
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "Keystream.h"

#include <array>
#include <cstring>
#include <mutex>
#include <ntsecapi.h>
#include <werapi.h>

 // The key cannot live in a SecureBuffer: scrambling would need the very
 // keystream the buffer holds. Pinned with VirtualLock, kept out of error
 // reports, wiped by hand.
static std::mutex g_Mutex;
static std::array<std::uint8_t, 32> g_Master = {};
static bool g_Installed = false;
static bool g_Locked = false;
static bool g_Excluded = false;

static constexpr std::size_t g_KeySize = 32;
static constexpr std::size_t g_BlockSize = 32;

bool NanaZip::Vault::Keystream::RandomBytes(
    _Out_writes_bytes_all_(Size) std::uint8_t* Data,
    _In_ size_t Size)
{
    // A failed generator leaves the buffer zeroed so callers fail closed.
    BOOLEAN Generated = ::RtlGenRandom(Data, static_cast<ULONG>(Size));
    if (Generated == FALSE)
    {
        ::SecureZeroMemory(Data, Size);
    }
    return Generated != FALSE;
}

bool NanaZip::Vault::Keystream::FillBlock(
    _In_reads_bytes_(8) std::uint8_t const* Nonce,
    _In_ std::uint64_t BlockIndex,
    _Out_writes_bytes_all_(g_BlockSize) std::uint8_t* Output)
{
    // SHA-256(master || nonce || block index): the index rides with the
    // nonce so no two buffers share a block. Caller holds the mutex.
    std::array<std::uint8_t, g_KeySize + 8 + 8> Input = {};
    std::memcpy(Input.data(), g_Master.data(), g_KeySize);
    std::memcpy(Input.data() + g_KeySize, Nonce, 8);
    for (int Index = 0; Index < 8; ++Index)
    {
        Input[g_KeySize + 8 + Index] = static_cast<std::uint8_t>(
            BlockIndex >> (8 * Index));
    }

    // CryptoAPI handles are ULONG_PTR, so 0 and not nullptr.
    HCRYPTPROV Provider = 0;
    HCRYPTHASH Hash = 0;
    DWORD HashSize = static_cast<DWORD>(g_BlockSize);
    bool Succeeded =
        ::CryptAcquireContextW(
            &Provider,
            nullptr,
            MS_ENH_RSA_AES_PROV_W,
            PROV_RSA_AES,
            CRYPT_VERIFYCONTEXT)
        && ::CryptCreateHash(Provider, CALG_SHA_256, 0, 0, &Hash)
        && ::CryptHashData(
            Hash,
            reinterpret_cast<BYTE const*>(Input.data()),
            static_cast<DWORD>(Input.size()),
            0)
        // HP_HASHVAL: in, the buffer size; out, the bytes written (32).
        && (TRUE == ::CryptGetHashParam(
            Hash,
            HP_HASHVAL,
            reinterpret_cast<BYTE*>(Output),
            &HashSize,
            0))
        && (HashSize == g_BlockSize);
    if (Hash)
    {
        ::CryptDestroyHash(Hash);
    }
    if (Provider)
    {
        ::CryptReleaseContext(Provider, 0);
    }
    // The input block carries the master key itself: key bytes left on the
    // stack here would scatter across frames in a way nothing else in this
    // file allows.
    ::SecureZeroMemory(Input.data(), Input.size());
    return Succeeded;
}

bool NanaZip::Vault::Keystream::Install()
{
    std::lock_guard<std::mutex> Guard(g_Mutex);
    if (!RandomBytes(g_Master.data(), g_Master.size()))
    {
        // The generator zeroed the buffer, so a key installed earlier is gone
        // with it. Saying so is what the caller needs: an installed flag over
        // a zero key would scramble with a keystream anyone can recompute.
        g_Installed = false;
        return false;
    }
    // VirtualLock is best effort: it keeps the key out of the page file, but
    // the process survives when the working set cannot grow.
    g_Locked = ::VirtualLock(g_Master.data(), g_Master.size()) != FALSE;
    // A fail-fast exit skips the filter that wipes the key, so hand the range
    // to Windows Error Reporting as well. The array is static storage, so the
    // one registration out of the 512 the API allows is never undone.
    if (!g_Excluded)
    {
        g_Excluded = SUCCEEDED(::WerRegisterExcludedMemoryBlock(
            g_Master.data(),
            static_cast<DWORD>(g_Master.size())));
    }
    g_Installed = true;
    return true;
}

void NanaZip::Vault::Keystream::Erase()
{
    std::lock_guard<std::mutex> Guard(g_Mutex);
    // Wiping the key is what destroys every scrambled buffer at once: the
    // contents stay XOR noise without it, so there is nothing left to zero
    // buffer by buffer.
    ::SecureZeroMemory(g_Master.data(), g_Master.size());
    if (g_Locked)
    {
        ::VirtualUnlock(g_Master.data(), g_Master.size());
        g_Locked = false;
    }
    g_Installed = false;
}

bool NanaZip::Vault::Keystream::IsInstalled()
{
    std::lock_guard<std::mutex> Guard(g_Mutex);
    return g_Installed;
}

void NanaZip::Vault::Keystream::EraseFromCrash()
{
    // No lock on purpose, see the header. The kernel serializes the unlock
    // against any lock in flight, so every race left here is a benign one.
    ::SecureZeroMemory(g_Master.data(), g_Master.size());
    if (g_Locked)
    {
        ::VirtualUnlock(g_Master.data(), g_Master.size());
        g_Locked = false;
    }
    g_Installed = false;
}

bool NanaZip::Vault::Keystream::Apply(
    _In_reads_bytes_(8) std::uint8_t const* Nonce,
    _In_reads_bytes_(Size) std::uint8_t* Data,
    _In_ size_t Size)
{
    if (!Data || Size == 0)
    {
        return true;
    }

    std::lock_guard<std::mutex> Guard(g_Mutex);
    if (!g_Installed)
    {
        return false;
    }

    std::array<std::uint8_t, g_BlockSize> Block = {};
    std::uint8_t* Cursor = Data;
    size_t Remaining = Size;
    std::uint64_t BlockIndex = 0;
    while (Remaining > 0)
    {
        if (!FillBlock(Nonce, BlockIndex, Block.data()))
        {
            // A keystream that cannot be produced must not leave the buffer
            // half scrambled, because the untouched half would stay
            // plaintext. Wiping the whole buffer keeps the failure closed.
            ::SecureZeroMemory(Data, Size);
            return false;
        }
        size_t Chunk = Remaining < g_BlockSize ? Remaining : g_BlockSize;
        for (size_t Index = 0; Index < Chunk; ++Index)
        {
            Cursor[Index] ^= Block[Index];
        }
        Cursor += Chunk;
        Remaining -= Chunk;
        ++BlockIndex;
    }
    ::SecureZeroMemory(Block.data(), Block.size());
    return true;
}
