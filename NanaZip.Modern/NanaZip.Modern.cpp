/*
 * PROJECT:    NanaZip.Modern
 * FILE:       NanaZip.Modern.cpp
 * PURPOSE:    Implementation for NanaZip Modern Experience
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: MouriNaruto (Kenji.Mouri@outlook.com)
 */

#include "pch.h"

#include "NanaZip.Modern.h"

#include <Mile.Helpers.h>
#include <Mile.Xaml.h>

#include "App.h"
#include "SponsorPage.h"
#include "AboutPage.h"
#include "InformationPage.h"
#include "ProgressPage.h"
#include "CopyLocationPage.h"

#pragma comment(lib, "comctl32.lib")

#include <winrt/Windows.ApplicationModel.Resources.Core.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

#include <winreg.h>
#include <dwmapi.h>
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "dwmapi.lib")

#include <mutex>
#include <map>
#include <vector>

namespace winrt
{
    using Windows::ApplicationModel::Resources::Core::ResourceCandidate;
    using Windows::ApplicationModel::Resources::Core::ResourceContext;
    using Windows::ApplicationModel::Resources::Core::ResourceManager;
    using Windows::ApplicationModel::Resources::Core::ResourceMap;
    using Windows::Globalization::Language;
}

namespace
{
    static winrt::ResourceMap GetMainResourceMap()
    {
        static winrt::ResourceMap CachedResult = ([]() -> winrt::ResourceMap
        {
            try
            {
                return winrt::ResourceManager::Current().MainResourceMap();
            }
            catch (...)
            {
                // Do nothing.
            }
            return nullptr;
        }());

        return CachedResult;
    }

    static std::mutex g_CachedLanguageStringResourcesMutex;
    static std::map<UINT32, winrt::hstring> g_CachedLanguageStringResources;
}

EXTERN_C LPCWSTR WINAPI K7ModernGetLegacyStringResource(
    _In_ UINT32 ResourceId)
{
    {
        std::lock_guard Lock(g_CachedLanguageStringResourcesMutex);
        auto Iterator = g_CachedLanguageStringResources.find(ResourceId);
        if (g_CachedLanguageStringResources.end() != Iterator)
        {
            return Iterator->second.c_str();
        }
    }

    static winrt::ResourceMap LegacyResourceMap = ([]() -> winrt::ResourceMap
    {
        winrt::ResourceMap MainResourceMap = ::GetMainResourceMap();
        if (MainResourceMap)
        {
            return MainResourceMap.GetSubtree(L"Legacy");
        }
        return nullptr;
    }());
    if (!LegacyResourceMap)
    {
        return nullptr;
    }

    winrt::hstring ResourceName = L"Resource" + winrt::to_hstring(ResourceId);
    if (!LegacyResourceMap.HasKey(ResourceName))
    {
        return nullptr;
    }

    winrt::hstring Content = LegacyResourceMap.Lookup(
        ResourceName).Candidates().GetAt(0).ValueAsString();
    std::lock_guard Lock(g_CachedLanguageStringResourcesMutex);
    auto Iterator = g_CachedLanguageStringResources.emplace(
        ResourceId,
        std::move(Content));
    return Iterator.first->second.c_str();
}

namespace
{
    struct LanguageEntry
    {
        winrt::hstring Tag;
        winrt::hstring Name;
    };

    static std::vector<LanguageEntry> const& GetLanguageEntries()
    {
        static std::vector<LanguageEntry> const CachedResult =
            ([]() -> std::vector<LanguageEntry>
            {
                std::vector<LanguageEntry> Result;

                winrt::ResourceMap MainResourceMap = ::GetMainResourceMap();
                if (!MainResourceMap)
                {
                    return Result;
                }

                try
                {
                    winrt::ResourceMap LegacyResourceMap =
                        MainResourceMap.GetSubtree(L"Legacy");
                    if (!LegacyResourceMap)
                    {
                        return Result;
                    }

                    // Resource 2200 is the name of the entry which follows the
                    // system language settings, so all the provided languages
                    // have it, and its candidates are the provided languages.
                    for (winrt::ResourceCandidate const& Candidate
                        : LegacyResourceMap.Lookup(
                            L"Resource2200").Candidates())
                    {
                        winrt::hstring Tag =
                            Candidate.GetQualifierValue(L"Language");
                        if (Tag.empty())
                        {
                            continue;
                        }

                        winrt::hstring Name;
                        try
                        {
                            Name = winrt::Language(Tag).NativeName();
                        }
                        catch (...)
                        {
                            // Do nothing.
                        }
                        if (Name.empty())
                        {
                            // Fall back to the language tag when the platform
                            // does not know the language.
                            Name = Tag;
                        }

                        LanguageEntry Entry;
                        Entry.Tag = Tag;
                        Entry.Name = Name;
                        Result.push_back(Entry);
                    }
                }
                catch (...)
                {
                    // Do nothing.
                }

                return Result;
            }());

        return CachedResult;
    }
}

EXTERN_C LPCWSTR WINAPI K7ModernGetLanguageTag(
    _In_ UINT32 Index)
{
    // The index 0 follows the system language settings, so it has no language
    // tag.
    if (0 == Index)
    {
        return L"";
    }

    std::vector<LanguageEntry> const& Entries = ::GetLanguageEntries();
    if (Index > Entries.size())
    {
        return nullptr;
    }

    return Entries[Index - 1].Tag.c_str();
}

EXTERN_C LPCWSTR WINAPI K7ModernGetLanguageName(
    _In_ UINT32 Index)
{
    // The index 0 follows the system language settings.
    if (0 == Index)
    {
        LPCWSTR SystemName = ::K7ModernGetLegacyStringResource(2200);
        return SystemName ? SystemName : L"System";
    }

    std::vector<LanguageEntry> const& Entries = ::GetLanguageEntries();
    if (Index > Entries.size())
    {
        return nullptr;
    }

    return Entries[Index - 1].Name.c_str();
}

EXTERN_C HRESULT WINAPI K7ModernSetLanguageOverride(
    _In_opt_ LPCWSTR LanguageTag)
{
    try
    {
        if (LanguageTag && *LanguageTag)
        {
            winrt::ResourceContext::SetGlobalQualifierValue(
                L"Language",
                winrt::hstring(LanguageTag));
        }
        else
        {
            winrt::ResourceContext::ResetGlobalQualifierValues();
        }
    }
    catch (...)
    {
        return winrt::to_hresult();
    }

    // The cached string resources are resolved with the previous language.
    std::lock_guard Lock(g_CachedLanguageStringResourcesMutex);
    g_CachedLanguageStringResources.clear();

    return S_OK;
}

namespace
{
    // The theme mode is exposed by the File Manager settings and is stored as
    // a REG_DWORD under HKCU\Software\NanaZip\FM\ThemeMode. This library does
    // not link against K7User, so the value is read here instead of through
    // K7UserReadThemeMode and the meaning of each number has to be kept in
    // sync with K7_USER_THEME_MODE by hand.
    enum class ThemeMode : DWORD
    {
        System = 0,
        Light = 1,
        Dark = 2
    };

    static ThemeMode ReadThemeMode()
    {
        DWORD Value = 0;
        DWORD ValueSize = sizeof(Value);

        HKEY KeyHandle = nullptr;
        if (ERROR_SUCCESS == ::RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\NanaZip\\FM",
            0,
            KEY_READ,
            &KeyHandle))
        {
            if (ERROR_SUCCESS != ::RegQueryValueExW(
                KeyHandle,
                L"ThemeMode",
                nullptr,
                nullptr,
                reinterpret_cast<LPBYTE>(&Value),
                &ValueSize))
            {
                Value = 0;
            }
            ::RegCloseKey(KeyHandle);
        }

        // A hand edited registry must not select an undefined theme.
        if (Value > static_cast<DWORD>(ThemeMode::Dark))
        {
            Value = static_cast<DWORD>(ThemeMode::System);
        }
        return static_cast<ThemeMode>(Value);
    }

    static bool ShouldUseDarkMode()
    {
        // The immersive color policy state is refreshed by K7User before this
        // is called, so it already reflects the current policy of the system.
        const bool SystemShouldUseDarkMode =
            ::MileShouldAppsUseDarkMode() &&
            !::MileShouldAppsUseHighContrastMode();

        switch (::ReadThemeMode())
        {
        case ThemeMode::Light:
            return false;
        case ThemeMode::Dark:
            return true;
        default:
            return SystemShouldUseDarkMode;
        }
    }

    // Tells the window which hosts the island how it is painted, which is a
    // layer of its own next to the XAML visual tree. Mile decides it from
    // ActualTheme when the content is set and does not look at it again, so a
    // window whose RequestedTheme was just changed keeps the values the system
    // had at that moment: the immersive dark mode policy of the window and the
    // background color it is painted with. Both have to be set again here.
    //
    // The notification is raised as well, because it is what forwards the
    // change to the CoreWindow compatibility window, which is where the
    // runtime theme switch of a XAML island is actually applied. It cannot do
    // the work on its own, though: its handler reads ActualTheme back from the
    // content and only repaints when the content already has a parent, which
    // a freshly created top level window does not have yet.
    static void RefreshHostWindowTheme(
        _In_ HWND WindowHandle,
        _In_ bool UseDarkMode)
    {
        if (S_OK == ::MileEnableImmersiveDarkModeForWindow(
            WindowHandle,
            UseDarkMode))
        {
            const MARGINS Margins = { -1 };
            ::DwmExtendFrameIntoClientArea(WindowHandle, &Margins);
        }
        else if (::SetPropW(
            WindowHandle,
            L"BackgroundFallbackColor",
            reinterpret_cast<HANDLE>(
                static_cast<ULONG_PTR>(::MileGetDefaultBackgroundColorValue(
                    UseDarkMode)))))
        {
            ::InvalidateRect(WindowHandle, nullptr, TRUE);
        }

        ::SendMessageW(
            WindowHandle,
            WM_SETTINGCHANGE,
            0,
            reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
    }
}

namespace
{
    static void ApplyXamlTheme(_In_ HWND WindowHandle)
    {
        winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource XamlSource =
            nullptr;
        // Mile.Xaml stores the island source on the window it hosts, which is
        // how the rest of this library reaches the islands it already owns.
        winrt::copy_from_abi(
            XamlSource,
            ::GetPropW(WindowHandle, L"XamlWindowSource"));
        if (!XamlSource)
        {
            return;
        }

        winrt::Windows::UI::Xaml::FrameworkElement RootElement = nullptr;
        try
        {
            RootElement = XamlSource.Content().try_as<
                winrt::Windows::UI::Xaml::FrameworkElement>();
        }
        catch (...)
        {
            return;
        }
        if (!RootElement)
        {
            return;
        }

        // FrameworkElement.RequestedTheme takes an ElementTheme, which
        // cannot be implicitly converted from an ApplicationTheme.
        const bool UseDarkMode = ::ShouldUseDarkMode();
        const winrt::Windows::UI::Xaml::ElementTheme Theme =
            (UseDarkMode
                ? winrt::Windows::UI::Xaml::ElementTheme::Dark
                : winrt::Windows::UI::Xaml::ElementTheme::Light);
        if (RootElement.RequestedTheme() != Theme)
        {
            RootElement.RequestedTheme(Theme);
        }

        // The host window has to be notified either way. It is not only the
        // host window theme which is stale then: the theme of a newly created
        // island is the default one again, so the values Mile took from it
        // when the content was set describe the system and not the selection.
        ::RefreshHostWindowTheme(WindowHandle, UseDarkMode);
    }

    static BOOL CALLBACK ApplyXamlThemeToChild(
        _In_ HWND ChildWindowHandle,
        _In_ LPARAM lParam)
    {
        UNREFERENCED_PARAMETER(lParam);

        try
        {
            ::ApplyXamlTheme(ChildWindowHandle);
        }
        catch (...)
        {
            // One island which cannot be themed must not stop the others.
        }

        return TRUE;
    }
}

EXTERN_C HRESULT WINAPI K7ModernRefreshTheme(
    _In_opt_ HWND WindowHandle)
{
    if (!WindowHandle)
    {
        return E_INVALIDARG;
    }

    // The islands are child windows of the window passed in, so the theme is
    // applied to the window itself and to every descendant. The address bar,
    // the main window tool bar and the status bar are all hosted this way, and
    // a newly created island starts with the default theme again, so this has
    // to run over the whole tree rather than over one window.
    try
    {
        ::ApplyXamlTheme(WindowHandle);

        ::EnumChildWindows(
            WindowHandle,
            ::ApplyXamlThemeToChild,
            0);
    }
    catch (...)
    {
        return winrt::to_hresult();
    }

    return S_OK;
}

namespace
{
    static winrt::NanaZip::Modern::App g_AppInstance = nullptr;
}

EXTERN_C BOOL WINAPI K7ModernAvailable()
{
    return nullptr != g_AppInstance;
}

EXTERN_C HRESULT WINAPI K7ModernInitialize()
{
    if (g_AppInstance)
    {
        return S_OK;
    }
    if (!::GetMainResourceMap())
    {
        // NanaZip.Modern requires resources.pri to get XAML resources.
        return E_NOINTERFACE;
    }
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        using Implementation = winrt::NanaZip::Modern::implementation::App;
        g_AppInstance = winrt::make<Implementation>();
    }
    catch (...)
    {
        return winrt::to_hresult();
    }
    return S_OK;
}

EXTERN_C HRESULT WINAPI K7ModernUninitialize()
{
    if (!g_AppInstance)
    {
        return S_OK;
    }
    try
    {
        g_AppInstance.Close();
        g_AppInstance = nullptr;
        winrt::uninit_apartment();
    }
    catch (...)
    {
        return winrt::to_hresult();
    }
    return S_OK;
}

namespace winrt
{
    using Windows::UI::Xaml::Hosting::DesktopWindowXamlSource;
}

namespace
{
    HWND K7ModernCreateXamlWindow(
        _In_opt_ HWND ParentWindowHandle,
        _In_ DWORD ExtendedWindowStyle,
        _In_ DWORD WindowStyle)
    {
        HWND WindowHandle = ::CreateWindowExW(
            ExtendedWindowStyle,
            L"Mile.Xaml.ContentWindow",
            nullptr,
            WindowStyle,
            CW_USEDEFAULT,
            0,
            CW_USEDEFAULT,
            0,
            ParentWindowHandle,
            nullptr,
            nullptr,
            nullptr);
        if (!WindowHandle)
        {
            return nullptr;
        }
        if (!::SetWindowSubclass(
            WindowHandle,
            [](
                _In_ HWND hWnd,
                _In_ UINT uMsg,
                _In_ WPARAM wParam,
                _In_ LPARAM lParam,
                _In_ UINT_PTR uIdSubclass,
                _In_ DWORD_PTR dwRefData) -> LRESULT
        {
            UNREFERENCED_PARAMETER(uIdSubclass);
            UNREFERENCED_PARAMETER(dwRefData);

            switch (uMsg)
            {
            case WM_CLOSE:
            {
                HWND ParentWindow = ::GetWindow(hWnd, GW_OWNER);
                if (ParentWindow)
                {
                    ::EnableWindow(ParentWindow, TRUE);
                }
                break;
            }
            default:
                break;
            }

            return ::DefSubclassProc(
                hWnd,
                uMsg,
                wParam,
                lParam);
        },
            0,
            0))
        {
            ::DestroyWindow(WindowHandle);
            return nullptr;
        }
        return WindowHandle;
    }

    HWND K7ModernCreateXamlDialog(
        _In_opt_ HWND ParentWindowHandle)
    {
        HWND WindowHandle = ::K7ModernCreateXamlWindow(
            ParentWindowHandle,
            WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME,
            WS_CAPTION | WS_SYSMENU);

        MILE_WINDOW_SYSTEM_BACKDROP_TYPE SystemBackdropType =
            MILE_WINDOW_SYSTEM_BACKDROP_TYPE_AUTO;
        if (S_OK == ::MileGetWindowSystemBackdropTypeAttribute(
            WindowHandle,
            &SystemBackdropType))
        {
            if (MILE_WINDOW_SYSTEM_BACKDROP_TYPE_AUTO != SystemBackdropType &&
                MILE_WINDOW_SYSTEM_BACKDROP_TYPE_NONE != SystemBackdropType)
            {
                const COLORREF IgnoreAccentColor = static_cast<COLORREF>(-2);
                ::MileSetWindowCaptionColorAttribute(
                    WindowHandle,
                    IgnoreAccentColor);
            }
        }

        return WindowHandle;
    }

    int K7ModernShowXamlWindow(
        _In_opt_ HWND WindowHandle,
        _In_ int Width,
        _In_ int Height,
        _In_ HWND ParentWindowHandle)
    {
        if (!WindowHandle)
        {
            return -1;
        }

        UINT DpiValue = ::GetDpiForWindow(WindowHandle);

        int ScaledWidth = ::MulDiv(Width, DpiValue, USER_DEFAULT_SCREEN_DPI);
        int ScaledHeight = ::MulDiv(Height, DpiValue, USER_DEFAULT_SCREEN_DPI);

        RECT ParentRect = {};
        if (ParentWindowHandle)
        {
            ::GetWindowRect(ParentWindowHandle, &ParentRect);
        }
        else
        {
            HMONITOR MonitorHandle = ::MonitorFromWindow(
                WindowHandle,
                MONITOR_DEFAULTTONEAREST);
            if (MonitorHandle)
            {
                MONITORINFO MonitorInfo;
                MonitorInfo.cbSize = sizeof(MONITORINFO);
                if (::GetMonitorInfoW(MonitorHandle, &MonitorInfo))
                {
                    ParentRect = MonitorInfo.rcWork;
                }
            }
        }

        int ParentWidth = ParentRect.right - ParentRect.left;
        int ParentHeight = ParentRect.bottom - ParentRect.top;

        ::SetWindowPos(
            WindowHandle,
            nullptr,
            ParentRect.left + ((ParentWidth - ScaledWidth) / 2),
            ParentRect.top + ((ParentHeight - ScaledHeight) / 2),
            ScaledWidth,
            ScaledHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // A window which hosts XAML on its own is not among the islands of
        // another window, so the refresh which walks those islands does not
        // reach it. It is themed here instead, right before it is shown,
        // because freshly set XAML content carries the default theme again,
        // which is not necessarily the theme that was selected.
        ::ApplyXamlTheme(WindowHandle);

        ::ShowWindow(WindowHandle, SW_SHOW);
        ::UpdateWindow(WindowHandle);

        return ::MileXamlContentWindowDefaultMessageLoop();
    }

    int K7ModernShowXamlDialog(
        _In_opt_ HWND WindowHandle,
        _In_ int Width,
        _In_ int Height,
        _In_ LPVOID Content,
        _In_ HWND ParentWindowHandle)
    {
        if (!WindowHandle)
        {
            return -1;
        }

        ::MileAllowNonClientDefaultDrawingForWindow(WindowHandle, FALSE);

        HMENU MenuHandle = ::GetSystemMenu(WindowHandle, FALSE);
        if (MenuHandle)
        {
            ::RemoveMenu(MenuHandle, 0, MF_SEPARATOR);
            ::RemoveMenu(MenuHandle, SC_RESTORE, MF_BYCOMMAND);
            ::RemoveMenu(MenuHandle, SC_SIZE, MF_BYCOMMAND);
            ::RemoveMenu(MenuHandle, SC_MINIMIZE, MF_BYCOMMAND);
            ::RemoveMenu(MenuHandle, SC_MAXIMIZE, MF_BYCOMMAND);
        }

        if (ParentWindowHandle)
        {
            ::EnableWindow(ParentWindowHandle, FALSE);
        }

        if (FAILED(::MileXamlSetXamlContentForContentWindow(
            WindowHandle,
            Content)))
        {
            ::DestroyWindow(WindowHandle);
            return -1;
        }

        int Result = ::K7ModernShowXamlWindow(
            WindowHandle,
            Width,
            Height,
            ParentWindowHandle);

        return Result;
    }

    winrt::DesktopWindowXamlSource K7ModernGetDesktopWindowXamlSource(
        _In_ HWND WindowHandle)
    {
        winrt::DesktopWindowXamlSource XamlSource = nullptr;
        winrt::copy_from_abi(
            XamlSource,
            ::GetPropW(WindowHandle, L"XamlWindowSource"));
        return XamlSource;
    }
}

EXTERN_C INT WINAPI K7ModernShowSponsorDialog(
    _In_opt_ HWND ParentWindowHandle)
{
    HWND WindowHandle = ::K7ModernCreateXamlDialog(ParentWindowHandle);
    if (!WindowHandle)
    {
        return -1;
    }

    using Interface =
        winrt::NanaZip::Modern::SponsorPage;
    using Implementation =
        winrt::NanaZip::Modern::implementation::SponsorPage;

    Interface Window = winrt::make<Implementation>(WindowHandle);

    int Result = ::K7ModernShowXamlDialog(
        WindowHandle,
        460,
        320,
        winrt::get_abi(Window),
        ParentWindowHandle);

    return Result;
}

EXTERN_C INT WINAPI K7ModernShowAboutDialog(
    _In_opt_ HWND ParentWindowHandle,
    _In_opt_ LPCWSTR ExtendedMessage)
{
    HWND WindowHandle = ::K7ModernCreateXamlDialog(ParentWindowHandle);
    if (!WindowHandle)
    {
        return -1;
    }

    using Interface =
        winrt::NanaZip::Modern::AboutPage;
    using Implementation =
        winrt::NanaZip::Modern::implementation::AboutPage;

    Interface Window = winrt::make<Implementation>(
        WindowHandle,
        ExtendedMessage);

    int Result = ::K7ModernShowXamlDialog(
        WindowHandle,
        480,
        320,
        winrt::get_abi(Window),
        ParentWindowHandle);

    return Result;
}

EXTERN_C INT WINAPI K7ModernShowInformationDialog(
    _In_opt_ HWND ParentWindowHandle,
    _In_opt_ LPCWSTR Title,
    _In_opt_ LPCWSTR Content)
{
    HWND WindowHandle = ::K7ModernCreateXamlDialog(ParentWindowHandle);
    if (!WindowHandle)
    {
        return -1;
    }

    using Interface =
        winrt::NanaZip::Modern::InformationPage;
    using Implementation =
        winrt::NanaZip::Modern::implementation::InformationPage;

    Interface Window = winrt::make<Implementation>(
        WindowHandle,
        Title,
        Content);

    int Result = ::K7ModernShowXamlDialog(
        WindowHandle,
        560,
        560,
        winrt::get_abi(Window),
        ParentWindowHandle);

    return Result;
}

EXTERN_C VOID WINAPI K7ModernUpdateProgressWindowStatus(
    _In_ HWND WindowHandle,
    _In_ PK7_PROGRESS_WINDOW_STATUS Status)
{
    if (!WindowHandle || !Status)
    {
        return;
    }

    winrt::DesktopWindowXamlSource XamlSource =
        ::K7ModernGetDesktopWindowXamlSource(WindowHandle);
    if (!XamlSource)
    {
        return;
    }

    using Interface =
        winrt::NanaZip::Modern::ProgressPage;
    using Implementation =
        winrt::NanaZip::Modern::implementation::ProgressPage;
    Interface InstanceObject = XamlSource.Content().as<Interface>();
    if (!InstanceObject)
    {
        return;
    }
    winrt::get_self<Implementation>(InstanceObject)->UpdateStatus(Status);
}

EXTERN_C VOID WINAPI K7ModernSetProgressWindowPausedMode(
    _In_ HWND WindowHandle,
    _In_ BOOL Paused)
{
    if (!WindowHandle)
    {
        return;
    }

    winrt::DesktopWindowXamlSource XamlSource =
        ::K7ModernGetDesktopWindowXamlSource(WindowHandle);
    if (!XamlSource)
    {
        return;
    }

    using Interface =
        winrt::NanaZip::Modern::ProgressPage;
    using Implementation =
        winrt::NanaZip::Modern::implementation::ProgressPage;
    Interface InstanceObject = XamlSource.Content().as<Interface>();
    if (!InstanceObject)
    {
        return;
    }
    winrt::get_self<Implementation>(InstanceObject)->SetPausedMode(Paused);
}

EXTERN_C INT WINAPI K7ModernShowProgressWindow(
    _In_opt_ HWND ParentWindowHandle,
    _In_opt_ LPCWSTR Title,
    _In_ BOOL ShowCompressionInformation,
    _In_ SUBCLASSPROC WindowSubclassHandler,
    _In_ LPVOID WindowSubclassContext)
{
    HWND WindowHandle = ::K7ModernCreateXamlWindow(
        ParentWindowHandle,
        WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME,
        WS_OVERLAPPEDWINDOW);
    if (!WindowHandle)
    {
        return -1;
    }

    ::MileAllowNonClientDefaultDrawingForWindow(WindowHandle, FALSE);

    using Interface =
        winrt::NanaZip::Modern::ProgressPage;
    using Implementation =
        winrt::NanaZip::Modern::implementation::ProgressPage;

    Interface Window = winrt::make<Implementation>(
        WindowHandle,
        Title,
        ShowCompressionInformation);

    if (FAILED(::MileXamlSetXamlContentForContentWindow(
        WindowHandle,
        winrt::get_abi(Window))))
    {
        ::DestroyWindow(WindowHandle);
        return -1;
    }

    if (WindowSubclassHandler)
    {
        if (!::SetWindowSubclass(
            WindowHandle,
            WindowSubclassHandler,
            1,
            reinterpret_cast<DWORD_PTR>(WindowSubclassContext)))
        {
            ::DestroyWindow(WindowHandle);
            return -1;
        }
    }

    int Result = ::K7ModernShowXamlWindow(
        WindowHandle,
        600,
        360,
        ParentWindowHandle);

    return Result;
}

EXTERN_C INT WINAPI K7ModernShowCopyLocationDialog(
    _In_opt_ HWND ParentWindowHandle,
    _In_opt_ LPCWSTR Title,
    _In_opt_ LPCWSTR Subtitle,
    _In_opt_ LPCWSTR AdditionalInformation,
    _In_opt_ LPCWSTR InitialPath,
    _In_ BOOL ShowExtractAll,
    _In_ SUBCLASSPROC WindowSubclassHandler,
    _In_ LPVOID WindowSubclassContext)
{
    HWND WindowHandle = ::K7ModernCreateXamlDialog(ParentWindowHandle);
    if (!WindowHandle)
    {
        return -1;
    }

    using Interface =
        winrt::NanaZip::Modern::CopyLocationPage;
    using Implementation =
        winrt::NanaZip::Modern::implementation::CopyLocationPage;

    Interface Window = winrt::make<Implementation>(
        WindowHandle,
        Title,
        Subtitle,
        AdditionalInformation,
        InitialPath,
        ShowExtractAll);

    if (WindowSubclassHandler)
    {
        if (!::SetWindowSubclass(
            WindowHandle,
            WindowSubclassHandler,
            1,
            reinterpret_cast<DWORD_PTR>(WindowSubclassContext)))
        {
            ::DestroyWindow(WindowHandle);
            return -1;
        }
    }

    int Result = ::K7ModernShowXamlDialog(
        WindowHandle,
        600,
        400,
        winrt::get_abi(Window),
        ParentWindowHandle);

    return Result;
}

EXTERN_C LPCWSTR WINAPI K7ModernGetCopyLocationDialogPath(
    _In_ HWND WindowHandle)
{
    if (!WindowHandle)
    {
        return nullptr;
    }

    winrt::DesktopWindowXamlSource XamlSource =
        ::K7ModernGetDesktopWindowXamlSource(WindowHandle);
    if (!XamlSource)
    {
        return nullptr;
    }

    using Interface =
        winrt::NanaZip::Modern::CopyLocationPage;
    using Implementation =
        winrt::NanaZip::Modern::implementation::CopyLocationPage;
    Interface InstanceObject = XamlSource.Content().as<Interface>();
    if (!InstanceObject)
    {
        return nullptr;
    }
    return winrt::get_self<Implementation>(InstanceObject)->GetPath();
}
