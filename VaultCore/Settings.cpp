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

#include <cstdio>
#include <optional>
#include <KnownFolders.h>
#include <shlobj.h>

// The key the settings live in, a sibling of the file manager settings so
// that resetting the latter does not touch the vault.
static constexpr wchar_t const* g_RootKeyPath = L"Software\\NanaZipRev\\Vault";

// Out-of-range timers fall back to the default; the bounds live on
// VaultSettings so the dialog validates against the same numbers.
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

// Lock timers also honor zero: never re-lock, and the key dies
// with the process. Any other out-of-range value takes the default.
static DWORD ReadLockTimeout(
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
    if (Value.value() == 0)
    {
        return 0;
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
    Settings.AbsoluteTimeoutMs = ::ReadLockTimeout(
        L"AbsoluteTimeoutMs",
        Settings.AbsoluteTimeoutMs,
        g_MinimumTimeoutMs,
        g_MaximumTimeoutMs);
    Settings.IdleTimeoutMs = ::ReadLockTimeout(
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

void NanaZip::Vault::VaultSettings::WriteAudit(_In_ char const* Event)
{
    if (!::ReadFlag(L"Audit", false))
    {
        return;
    }
    std::filesystem::path Root = RootDir();
    if (Root.empty())
    {
        return;
    }
    std::error_code ErrorCode;
    std::filesystem::create_directories(Root, ErrorCode);
    if (ErrorCode)
    {
        return;
    }

    SYSTEMTIME Now;
    ::GetSystemTime(&Now);
    char Line[128] = {};
    // The event tokens are ASCII, so the log is readable in any editor.
    int Length = ::sprintf_s(
        Line,
        "%04u-%02u-%02uT%02u:%02u:%02uZ %s\r\n",
        Now.wYear,
        Now.wMonth,
        Now.wDay,
        Now.wHour,
        Now.wMinute,
        Now.wSecond,
        Event);
    if (Length <= 0)
    {
        return;
    }

    HANDLE File = ::CreateFileW(
        (Root / L"audit.log").c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (File == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD Written = 0;
    ::WriteFile(
        File,
        Line,
        static_cast<DWORD>(Length),
        &Written,
        nullptr);
    ::CloseHandle(File);
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
