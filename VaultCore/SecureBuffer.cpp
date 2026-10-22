/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       SecureBuffer.cpp
 * PURPOSE:    Implementation for the vault secure buffer
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "SecureBuffer.h"

#include "Keystream.h"

#include <new>
#include <cstring>
#include <werapi.h>

NanaZip::Vault::SecureBuffer::SecureBuffer(_In_ size_t Bytes)
    : m_Size(Bytes)
{
    if (Bytes == 0)
    {
        return;
    }
    this->m_Data = static_cast<std::uint8_t*>(
        ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, Bytes));
    if (!this->m_Data)
    {
        this->m_Size = 0;
        throw std::bad_alloc();
    }
    // VirtualLock is best effort: it keeps the secret out of the page file,
    // but the vault survives when the working set cannot grow.
    if (::VirtualLock(this->m_Data, Bytes))
    {
        this->m_Locked = true;
    }
    // Keeps the block out of Windows Error Reporting dumps. Undone on
    // release, so a long-lived vault does not grow the exclusion list.
    ::WerRegisterExcludedMemoryBlock(
        this->m_Data,
        static_cast<DWORD>(Bytes));
    // A secret must never rest in plaintext, so a keystream is installed on
    // demand when the vault is locked. If the generator fails the allocation
    // is released instead: resting unscrambled is what this type prevents.
    if (!::NanaZip::Vault::Keystream::IsInstalled())
    {
        if (!::NanaZip::Vault::Keystream::Install())
        {
            Free();
            return;
        }
    }
    ::NanaZip::Vault::Keystream::RandomBytes(
        this->m_Nonce,
        sizeof(this->m_Nonce));
}

NanaZip::Vault::SecureBuffer::SecureBuffer(_In_ SecureBuffer&& Other) noexcept
    : m_Data(Other.m_Data)
    , m_Size(Other.m_Size)
    , m_Locked(Other.m_Locked)
{
    std::memcpy(
        this->m_Nonce,
        Other.m_Nonce,
        sizeof(this->m_Nonce));
    Other.m_Data = nullptr;
    Other.m_Size = 0;
    Other.m_Locked = false;
}

NanaZip::Vault::SecureBuffer&
NanaZip::Vault::SecureBuffer::operator=(_In_ SecureBuffer&& Other) noexcept
{
    if (this != &Other)
    {
        this->Free();
        this->m_Data = Other.m_Data;
        this->m_Size = Other.m_Size;
        this->m_Locked = Other.m_Locked;
        std::memcpy(this->m_Nonce, Other.m_Nonce, sizeof(this->m_Nonce));
        Other.m_Data = nullptr;
        Other.m_Size = 0;
        Other.m_Locked = false;
    }
    return *this;
}

NanaZip::Vault::SecureBuffer::~SecureBuffer()
{
    this->Free();
}

NanaZip::Vault::SecureBuffer
NanaZip::Vault::SecureBuffer::FromString(_In_ std::wstring const& Text)
{
    SecureBuffer Buffer(Text.size() * sizeof(wchar_t));
    if (Buffer.m_Data && !Text.empty())
    {
        std::memcpy(Buffer.m_Data, Text.data(), Buffer.m_Size);
        Buffer.ApplyKeystream();
    }
    return Buffer;
}

NanaZip::Vault::SecureBuffer NanaZip::Vault::SecureBuffer::FromBytes(
    _In_reads_bytes_(Size) std::uint8_t const* Data,
    _In_ size_t Size)
{
    SecureBuffer Buffer(Size);
    if (Buffer.m_Data && Data && Size > 0)
    {
        std::memcpy(Buffer.m_Data, Data, Size);
        Buffer.ApplyKeystream();
    }
    return Buffer;
}

NanaZip::Vault::SecureBuffer::Reader NanaZip::Vault::SecureBuffer::Read() const
{
    // The scramble state is not what the buffer holds, so const may flip it.
    SecureBuffer* This = const_cast<SecureBuffer*>(this);
    This->ApplyKeystream();
    return Reader(*This, This->m_Data, This->m_Size);
}

bool NanaZip::Vault::SecureBuffer::ApplyKeystream()
{
    if (!this->m_Data || this->m_Size == 0)
    {
        return true;
    }
    if (!::NanaZip::Vault::Keystream::Apply(
        this->m_Nonce,
        this->m_Data,
        this->m_Size))
    {
        // Without the key the bytes are still plaintext, so they must go.
        this->Free();
        return false;
    }
    return true;
}

void NanaZip::Vault::SecureBuffer::Free()
{
    if (this->m_Data)
    {
        ::WerUnregisterExcludedMemoryBlock(this->m_Data);
        ::SecureZeroMemory(this->m_Data, this->m_Size);
        if (this->m_Locked)
        {
            ::VirtualUnlock(this->m_Data, this->m_Size);
            this->m_Locked = false;
        }
        ::HeapFree(::GetProcessHeap(), 0, this->m_Data);
        this->m_Data = nullptr;
        this->m_Size = 0;
    }
}

NanaZip::Vault::SecureBuffer::Reader::Reader(
    _In_ SecureBuffer& Buffer,
    _In_reads_bytes_opt_(Size) std::uint8_t const* Data,
    _In_ size_t Size)
    : m_Buffer(&Buffer)
    , m_Data(Data)
    , m_Size(Size)
{
}

NanaZip::Vault::SecureBuffer::Reader::Reader(_In_ Reader&& Other) noexcept
    : m_Buffer(Other.m_Buffer)
    , m_Data(Other.m_Data)
    , m_Size(Other.m_Size)
{
    Other.m_Buffer = nullptr;
    Other.m_Data = nullptr;
    Other.m_Size = 0;
}

NanaZip::Vault::SecureBuffer::Reader::~Reader()
{
    if (this->m_Buffer)
    {
        this->m_Buffer->ApplyKeystream();
        this->m_Buffer = nullptr;
    }
}
