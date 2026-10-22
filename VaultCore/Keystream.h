/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       Keystream.h
 * PURPOSE:    Definition for the process-wide secret keystream
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_KEYSTREAM
#define NANAZIP_VAULT_KEYSTREAM

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace NanaZip::Vault
{
    // The keystream that scrambles the resting state of every SecureBuffer: a
    // 256-bit master key plus a per-buffer nonce feeds SHA-256 in counter
    // mode, so a dump shows XOR noise. The key is in-process, which limits it.
    class Keystream
    {
    public:
        // Generates a fresh master key, on init and again on every unlock.
        // False when the generator failed: a zero key would scramble nothing.
        static bool Install();

        // Wipes the master key, which leaves every scrambled buffer unreadable
        // for good: the intended fail-closed behaviour of locking.
        static void Erase();

        // Wipes without taking the lock. Only the crash filter may call this:
        // it runs on the thread that crashed, which may be the one holding it.
        static void EraseFromCrash();

        static bool IsInstalled();

        // Fills pseudo-random bytes from the system generator, shared by the
        // key, the nonces and the store's ids. False on failure, buffer zeroed.
        static bool RandomBytes(
            _Out_writes_bytes_all_(Size) std::uint8_t* Data,
            _In_ size_t Size);

        // XORs the keystream of Nonce over the buffer in place; the operation
        // is its own inverse. False when no key is installed or a block cannot
        // be produced, so the caller can wipe instead of resting in plaintext.
        static bool Apply(
            _In_reads_bytes_(8) std::uint8_t const* Nonce,
            _In_reads_bytes_(Size) std::uint8_t* Data,
            _In_ size_t Size);

    private:
        // One keystream block for the given nonce and index.
        static bool FillBlock(
            _In_reads_bytes_(8) std::uint8_t const* Nonce,
            _In_ std::uint64_t BlockIndex,
            _Out_writes_bytes_all_(32) std::uint8_t* Output);
    };
}

#endif // !NANAZIP_VAULT_KEYSTREAM
