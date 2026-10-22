/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       SecureBuffer.h
 * PURPOSE:    Definition for the vault secure buffer
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_SECURE_BUFFER
#define NANAZIP_VAULT_SECURE_BUFFER

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace NanaZip::Vault
{
    // Pinned, zeroed on release memory for one secret. The resting state is
    // never plaintext: whoever writes the contents leaves them XORed with the
    // process keystream, and a Reader restores them for as long as it lives.
    class SecureBuffer
    {
    public:
        explicit SecureBuffer(_In_ size_t Bytes);
        SecureBuffer(_In_ SecureBuffer const& Other) = delete;
        SecureBuffer& operator=(_In_ SecureBuffer const& Other) = delete;
        SecureBuffer(_In_ SecureBuffer&& Other) noexcept;
        SecureBuffer& operator=(_In_ SecureBuffer&& Other) noexcept;
        ~SecureBuffer();

        // Copies a UTF-16 string. The caller stays responsible for its copy.
        static SecureBuffer FromString(_In_ std::wstring const& Text);

        // Copies raw bytes: the DPAPI output is already a byte string.
        static SecureBuffer FromBytes(
            _In_reads_bytes_(Size) std::uint8_t const* Data,
            _In_ size_t Size);

        // Plaintext window: restored while the guard lives, scrambled again
        // when it dies. Only one window may be open on a buffer at a time.
        class Reader
        {
        public:
            Reader(_In_ Reader&& Other) noexcept;
            ~Reader();

            // Null for an empty buffer.
            std::uint8_t const* Data() const
            {
                return this->m_Data;
            }

            size_t Size() const
            {
                return this->m_Size;
            }

        private:
            friend class SecureBuffer;

            Reader(
                _In_ SecureBuffer& Buffer,
                _In_reads_bytes_opt_(Size) std::uint8_t const* Data,
                _In_ size_t Size);

            SecureBuffer* m_Buffer = nullptr;
            std::uint8_t const* m_Data = nullptr;
            size_t m_Size = 0;
        };

        // Opens a plaintext window. Const because flipping the resting state
        // changes how the secret is stored, not what the buffer holds.
        Reader Read() const;

        size_t Size() const
        {
            return this->m_Size;
        }

    private:
        void Free();

        // XORs the process keystream over the contents in place; its own
        // inverse. Wipes the buffer and returns false when the key is gone,
        // so a secret can never rest as plaintext.
        bool ApplyKeystream();

        std::uint8_t* m_Data = nullptr;
        size_t m_Size = 0;
        std::uint8_t m_Nonce[8] = {};
        bool m_Locked = false;
    };
}

#endif // !NANAZIP_VAULT_SECURE_BUFFER
