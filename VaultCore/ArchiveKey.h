/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       ArchiveKey.h
 * PURPOSE:    Content fingerprint for archive password matching
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_ARCHIVE_KEY
#define NANAZIP_VAULT_ARCHIVE_KEY

#include <Windows.h>

#include <optional>
#include <string>

namespace NanaZip::Vault
{
    // The fingerprint that ties a saved password to one archive: a hash of
    // the content, not the path, so a move or a rename keeps the match alive.
    class ArchiveKey
    {
    public:
        // Files up to this many bytes are hashed in full. Above it the first
        // and last SampleWindow bytes plus the size feed the hash: the archive
        // central directory lives at the tail, so any change rewrites it.
        static constexpr std::uint64_t FullHashThreshold = 16ULL * 1024 * 1024;
        static constexpr std::uint64_t SampleWindow = 4ULL * 1024 * 1024;

        // The lowercase hex SHA-256 fingerprint, or nothing when the file
        // cannot be opened or the hash cannot be produced.
        static std::optional<std::wstring> Compute(
            _In_ std::wstring const& FilePath);
    };
}

#endif // !NANAZIP_VAULT_ARCHIVE_KEY
