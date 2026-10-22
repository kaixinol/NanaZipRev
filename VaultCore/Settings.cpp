/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       Settings.cpp
 * PURPOSE:    Implementation for the vault settings
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "Settings.h"

#include <optional>
#include <KnownFolders.h>
#include <shlobj.h>

// The key the settings live in, a sibling of the file manager settings so
// that resetting the latter does not touch the vault.
static constexpr wchar_t const* g_RootKeyPath = L"Software\\NanaZipRev\\Vault";

// A timer outside these ranges falls back to its default: a hand-edited
// value of zero would lock the vault on every idle moment and a huge one
// would disable the absolute timer altogether. The bounds live on the
// settings struct so that the settings dialog can validate against the very
// same numbers.
static constexpr DWORD g_MinimumTimeoutMs =
    NanaZip::Vault::VaultSettings::MinimumTimeoutMs;
static constexpr DWORD g_MaximumTimeoutMs =
    NanaZip::Vault::VaultSettings::MaximumTimeoutMs;

// A secret must not dwell on the clipboard: a tighter bound.
static constexpr DWORD g_MinimumClipboardClearMs =
    NanaZip::Vault::VaultSettings::MinimumClipboardClearMs;
static constexpr DWORD g_MaximumClipboardClearMs =
    NanaZip::Vault::VaultSettings::MaximumClipboardClearMs;

// Wrong type is rejected by RRF_RT_REG_DWORD; a truncated value
// reports a size that is not DWORD-sized.
static std::optional<DWORD> ReadValue(wchar_t const* Name)
{
    DWORD Data = 0;
    DWORD Size = sizeof(Data);
    if (ERROR_SUCCESS != ::RegGetValueW(
        HKEY_CURRENT_USER,
        g_RootKeyPath,
        Name,
        RRF_RT_REG_DWORD,
        nullptr,
        &Data,
        &Size))
    {
        return std::nullopt;
    }
    if (Size != sizeof(Data))
    {
        return std::nullopt;
    }
    return Data;
}

static DWORD ReadTimeout(
    wchar_t const* Name,
    DWORD DefaultValue,
    DWORD MinimumValue,
    DWORD MaximumValue)
{
    std::optional<DWORD> Value = ::ReadValue(Name);
    if (!Value.has_value())
    {
        return DefaultValue;
    }
    if (Value.value() < MinimumValue || Value.value() > MaximumValue)
    {
        return DefaultValue;
    }
    return Value.value();
}

static bool ReadFlag(wchar_t const* Name, bool DefaultValue)
{
    std::optional<DWORD> Value = ::ReadValue(Name);
    if (!Value.has_value())
    {
        return DefaultValue;
    }
    // Anything but zero is true, which matches how the file manager reads
    // its own boolean options.
    return 0 != Value.value();
}

static void WriteValue(wchar_t const* Name, DWORD Data)
{
    HKEY Key = nullptr;
    if (ERROR_SUCCESS != ::RegCreateKeyExW(
        HKEY_CURRENT_USER,
        g_RootKeyPath,
        0,
        nullptr,
        0,
        KEY_SET_VALUE,
        nullptr,
        &Key,
        nullptr))
    {
        return;
    }
    // A failure just keeps the defaults; the caller cannot do more.
    ::RegSetValueExW(
        Key,
        Name,
        0,
        REG_DWORD,
        reinterpret_cast<BYTE const*>(&Data),
        sizeof(Data));
    ::RegCloseKey(Key);
}

NanaZip::Vault::VaultSettings NanaZip::Vault::VaultSettings::Load()
{
    VaultSettings Settings;
    Settings.Enabled = ::ReadFlag(L"Enabled", Settings.Enabled);
    Settings.AbsoluteTimeoutMs = ReadTimeout(
        L"AbsoluteTimeoutMs",
        Settings.AbsoluteTimeoutMs,
        g_MinimumTimeoutMs,
        g_MaximumTimeoutMs);
    Settings.IdleTimeoutMs = ReadTimeout(
        L"IdleTimeoutMs",
        Settings.IdleTimeoutMs,
        g_MinimumTimeoutMs,
        g_MaximumTimeoutMs);
    Settings.ClipboardClearMs = ::ReadTimeout(
        L"ClipboardClearMs",
        Settings.ClipboardClearMs,
        g_MinimumClipboardClearMs,
        g_MaximumClipboardClearMs);
    Settings.Audit = ::ReadFlag(L"Audit", Settings.Audit);
    Settings.SavePasswordByDefault = ::ReadFlag(
        L"SavePasswordByDefault",
        Settings.SavePasswordByDefault);
    return Settings;
}

void NanaZip::Vault::VaultSettings::Save() const
{
    ::WriteValue(L"Enabled", this->Enabled ? 1 : 0);
    ::WriteValue(L"AbsoluteTimeoutMs", this->AbsoluteTimeoutMs);
    ::WriteValue(L"IdleTimeoutMs", this->IdleTimeoutMs);
    ::WriteValue(L"ClipboardClearMs", this->ClipboardClearMs);
    ::WriteValue(L"Audit", this->Audit ? 1 : 0);
    ::WriteValue(
        L"SavePasswordByDefault",
        this->SavePasswordByDefault ? 1 : 0);
}

std::filesystem::path NanaZip::Vault::VaultSettings::RootDir()
{
    // The known folder follows a redirected or roaming profile, unlike
    // the environment variable, which is only the no-shell fallback.
    PWSTR Path = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(
        FOLDERID_LocalAppData,
        0,
        nullptr,
        &Path)))
    {
        std::filesystem::path Result(Path);
        ::CoTaskMemFree(Path);
        return Result / L"NanaZipRev" / L"Vault";
    }

    DWORD Length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (0 == Length)
    {
        return std::filesystem::path();
    }
    std::wstring LocalAppData;
    LocalAppData.resize(Length);
    Length = ::GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        LocalAppData.data(),
        static_cast<DWORD>(LocalAppData.size()));
    if (0 == Length || Length >= LocalAppData.size())
    {
        return std::filesystem::path();
    }
    LocalAppData.resize(Length);
    return std::filesystem::path(LocalAppData) / L"NanaZipRev" / L"Vault";
}
