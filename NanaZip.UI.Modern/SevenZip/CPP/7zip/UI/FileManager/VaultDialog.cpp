/*
 * PROJECT:    NanaZip
 * FILE:       VaultDialog.cpp
 * PURPOSE:    Implementation for Vault Dialog
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "StdAfx.h"

#include <CommCtrl.h>

#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <winrt/base.h>

#include "VaultDialog.h"
#include "../../../Windows/Control/Dialog.h"
#include "../../../Windows/Control/ListView.h"

#include "FormatUtils.h"
#include "LangUtils.h"
#include "resourceGui.h"
#include "VaultDialogRes.h"

#include <KeyGate.h>
#include <Session.h>
#include <Settings.h>
#include <VaultKey.h>
#include <VaultStore.h>
#include <VaultCore.h>

using namespace NWindows;
using namespace NControl;

extern bool g_LVN_ITEMACTIVATE_Support;

// Defined at the end of the file, past the dialog class it needs.
static void ShowDialog(_In_opt_ HWND ParentWindowHandle);

namespace
{
    // One title for every message box here; intentionally not localized.
    constexpr const wchar_t* MessageTitle = L"NanaZip Rev";

    std::wstring WideFromUString(_In_ const UString& Text)
    {
        return std::wstring(Text.Ptr(), Text.Len());
    }

    UString UStringFromWide(_In_ const std::wstring& Text)
    {
        return UString(Text.c_str());
    }

    NanaZip::Vault::SecureBuffer BufferFromWide(
        _In_ const std::wstring& Text)
    {
        return NanaZip::Vault::SecureBuffer::FromString(Text);
    }

    void WipeWideString(_Inout_ std::wstring& Text)
    {
        if (!Text.empty())
        {
            ::SecureZeroMemory(
                Text.data(),
                Text.size() * sizeof(wchar_t));
            Text.clear();
        }
    }

    // The Windows clipboard history and cloud sync read these; the last one
    // is the older monitor opt-out.
    const UINT g_CanIncludeInClipboardHistory =
        ::RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    const UINT g_CanUploadToCloudClipboard =
        ::RegisterClipboardFormatW(L"CanUploadToCloudClipboard");
    const UINT g_ExcludeFromMonitorProcessing =
        ::RegisterClipboardFormatW(
            L"ExcludeClipboardContentFromMonitorProcessing");

    void SetClipboardDword(UINT Format, DWORD Value)
    {
        if (Format == 0)
        {
            return;
        }
        HGLOBAL Handle = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(Value));
        if (!Handle)
        {
            return;
        }
        void* Data = ::GlobalLock(Handle);
        if (!Data)
        {
            ::GlobalFree(Handle);
            return;
        }
        std::memcpy(Data, &Value, sizeof(Value));
        ::GlobalUnlock(Handle);
        if (!::SetClipboardData(Format, Handle))
        {
            ::GlobalFree(Handle);
        }
    }

    bool CopyToClipboard(_In_ const std::wstring& Text)
    {
        bool Placed = false;
        if (::OpenClipboard(nullptr))
        {
            if (::EmptyClipboard())
            {
                size_t Size = (Text.size() + 1) * sizeof(wchar_t);
                HGLOBAL Handle = ::GlobalAlloc(GMEM_MOVEABLE, Size);
                if (Handle)
                {
                    void* Data = ::GlobalLock(Handle);
                    if (Data)
                    {
                        std::memcpy(Data, Text.c_str(), Size);
                        ::GlobalUnlock(Handle);
                    }
                    if (Data && ::SetClipboardData(CF_UNICODETEXT, Handle))
                    {
                        // The clipboard owns the handle once the set lands.
                        Placed = true;
                        SetClipboardDword(g_CanIncludeInClipboardHistory, 0);
                        SetClipboardDword(g_CanUploadToCloudClipboard, 0);
                        SetClipboardDword(g_ExcludeFromMonitorProcessing, 1);
                    }
                    else
                    {
                        ::GlobalFree(Handle);
                    }
                }
            }
            ::CloseClipboard();
        }
        return Placed;
    }

    std::optional<std::wstring> ReadSecret(
        _In_ const NanaZip::Vault::VaultStore& Store,
        _In_ const std::wstring& Id,
        _In_ const std::wstring& Field)
    {
        return NanaZip::Vault::VaultCore::ReadSecret(Store, Id, Field);
    }

    // The same input counts as activity in every dialog here, so a nested
    // one cannot let a warm vault time out while the user works in it.
    bool IsActivityMessage(_In_ UINT Message)
    {
        switch (Message)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_KEYDOWN:
        case WM_COMMAND:
        case WM_NOTIFY:
            return true;
        }
        return false;
    }

    // Process-wide: the clipboard wipe and the lock timers outlive this.
    NanaZip::Vault::Session& GetSession()
    {
        return NanaZip::Vault::VaultCore::GetSession();
    }

    bool IsVaultEnabled()
    {
        return NanaZip::Vault::VaultSettings::Load().Enabled;
    }

    // Re-opened on every dialog so a failed root create is not permanent.
    std::optional<NanaZip::Vault::VaultStore>& GetStore()
    {
        static std::optional<NanaZip::Vault::VaultStore> Store;
        return Store;
    }

    // The one string a row has room for: the path the password was saved
    // from, or the content fingerprint when no path was recorded. The
    // label is a column of its own, so repeating it here would waste the
    // width without saying anything.
    std::wstring EntryDisplayText(
        _In_ const NanaZip::Vault::EntryMeta& Meta)
    {
        if (!Meta.Path.empty())
        {
            return Meta.Path;
        }
        if (!Meta.ArchiveKey.empty())
        {
            return Meta.ArchiveKey;
        }
        return Meta.Label;
    }

    // The tip has room for what the row cannot hold: the path whole, and
    // the fingerprint that ties the entry to the archive's content. An
    // entry whose only name is the fingerprint does not repeat it.
    std::wstring EntryTooltipText(
        _In_ const NanaZip::Vault::EntryMeta& Meta)
    {
        std::wstring Text = EntryDisplayText(Meta);
        if (Meta.ArchiveKey.empty() || Text == Meta.ArchiveKey)
        {
            return Text;
        }
        // A literal, not IDS_PROP_SHA256: the file manager projects never
        // compile PropertyName.rc, so that id resolves to nothing here.
        Text += L"\r\nSHA-256 ";
        Text += Meta.ArchiveKey;
        return Text;
    }

    bool g_ShowInProgress = false;

    struct ShowGuard
    {
        ~ShowGuard()
        {
            g_ShowInProgress = false;
        }
    };

    bool ConfirmEnable(_In_opt_ HWND ParentWindowHandle)
    {
        UString Title = LangString(IDS_VAULT_ENABLE_TITLE);
        UString Warning = LangString(IDS_VAULT_ENABLE_WARNING);

        TASKDIALOGCONFIG Config = {};
        Config.cbSize = sizeof(TASKDIALOGCONFIG);
        Config.hwndParent = ParentWindowHandle;
        Config.hInstance = ::GetModuleHandleW(nullptr);
        Config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        Config.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
        Config.pszMainIcon = TD_WARNING_ICON;
        Config.pszWindowTitle = Title;
        Config.pszMainInstruction = Title;
        Config.pszContent = Warning;
        // The safe answer is the default on a dialog that weakens a boundary.
        Config.nDefaultButton = IDNO;

        int Button = 0;
        if (FAILED(::TaskDialogIndirect(
            &Config,
            &Button,
            nullptr,
            nullptr)))
        {
            return false;
        }
        return IDYES == Button;
    }

    void ShowMessage(
        _In_opt_ HWND ParentWindowHandle,
        _In_ UINT StringID,
        _In_ UINT Type)
    {
        UString Text;
        LangString(StringID, Text);
        ::MessageBoxW(ParentWindowHandle, Text, MessageTitle, Type);
    }
}

// ES_PASSWORD only masks the glyphs: WM_GETTEXT and EM_GETLINE still return
// the real text to any process. Drop them when InSendMessage says they came
// from another thread; the dialog's own calls run in the window procedure.
static LRESULT CALLBACK VaultPasswordEditSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);
    if (::InSendMessage())
    {
        switch (uMsg)
        {
        case WM_GETTEXT:
        case WM_GETTEXTLENGTH:
        case EM_GETLINE:
        case EM_GETLINECOUNT:
        case EM_GETTHUMB:
            return 0;
        }
    }
    return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static const UInt32 kVaultEntryLangIDs[] =
{
    IDT_VAULT_ENTRY_ARCHIVE,
    IDT_VAULT_ENTRY_LABEL,
    IDT_VAULT_ENTRY_PASSWORD,
    IDX_VAULT_ENTRY_SHOW
};

// Edits one entry. Password arrives revealed.
class CVaultEntryDialog : public NControl::CModalDialog
{
public:
    NanaZip::Vault::EntryMeta Meta;
    std::wstring Password;

    INT_PTR Create(_In_ HWND ParentWindowHandle)
    {
        return CModalDialog::Create(IDD_VAULT_ENTRY, ParentWindowHandle);
    }

private:
    virtual bool OnInit();
    virtual bool OnDestroy();
    virtual bool OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
    virtual void OnOK();
    virtual bool OnButtonClicked(int buttonID, HWND buttonHWND);
};

bool CVaultEntryDialog::OnInit()
{
    LangSetWindowText(*this, IDD_VAULT_ENTRY);
    LangSetDlgItems(*this, kVaultEntryLangIDs, ARRAY_SIZE(kVaultEntryLangIDs));
    // The stored path is what names the entry to the user; the fingerprint
    // behind it is the matching key and reads as noise.
    SetItemText(
        IDE_VAULT_ENTRY_ARCHIVE,
        EntryDisplayText(Meta).c_str());
    // A typed key matches nothing, so the field stays read-only.
    ::SendMessageW(
        GetItem(IDE_VAULT_ENTRY_ARCHIVE), EM_SETREADONLY, TRUE, 0);
    SetItemText(IDE_VAULT_ENTRY_LABEL, Meta.Label.c_str());
    SetItemText(IDE_VAULT_ENTRY_PASSWORD, Password.c_str());
    ::SetWindowSubclass(GetItem(IDE_VAULT_ENTRY_PASSWORD),
        VaultPasswordEditSubclassProc, 1, 0);
    GetSession().AddNotifyWindow((HWND)*this);
    return CModalDialog::OnInit();
}

bool CVaultEntryDialog::OnDestroy()
{
    GetSession().RemoveNotifyWindow((HWND)*this);
    ::RemoveWindowSubclass(GetItem(IDE_VAULT_ENTRY_PASSWORD),
        VaultPasswordEditSubclassProc, 1);
    return CModalDialog::OnDestroy();
}

bool CVaultEntryDialog::OnMessage(
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (NanaZip::Vault::Session::NotifyMessage == message)
    {
        if (NanaZip::Vault::Session::NotifyLocked == wParam)
        {
            // The dialog is showing a revealed password; a lock closes it
            // and the caller wipes Password after Create returns.
            ::SetWindowTextW(GetItem(IDE_VAULT_ENTRY_PASSWORD), L"");
            WipeWideString(Password);
            End(IDCANCEL);
        }
        return true;
    }
    if (IsActivityMessage(message))
    {
        GetSession().OnActivity();
    }
    return CModalDialog::OnMessage(message, wParam, lParam);
}

void CVaultEntryDialog::OnOK()
{
    // The archive box is display only: a fingerprint is what a match is
    // computed from, and a typed one would match nothing.
    {
        UString Text;
        GetItemText(IDE_VAULT_ENTRY_LABEL, Text);
        Meta.Label = WideFromUString(Text);
    }
    {
        UString Text;
        GetItemText(IDE_VAULT_ENTRY_PASSWORD, Text);
        if (Text.IsEmpty())
        {
            // An entry sealed from an empty password decrypts to nothing the
            // store can hand back, so it could never be read or edited again.
            ShowMessage((HWND)*this, IDS_VAULT_PASSWORD_EMPTY, MB_ICONWARNING);
            return;
        }
        Password = WideFromUString(Text);
    }

    CModalDialog::OnOK();
}

bool CVaultEntryDialog::OnButtonClicked(int buttonID, HWND buttonHWND)
{
    if (IDX_VAULT_ENTRY_SHOW == buttonID)
    {
        // EM_SETPASSWORDCHAR does not always invalidate the edit, so the old
        // characters stay on screen until the next repaint.
        bool Show = IsButtonCheckedBool(IDX_VAULT_ENTRY_SHOW);
        HWND Edit = GetItem(IDE_VAULT_ENTRY_PASSWORD);
        ::SendMessageW(Edit, EM_SETPASSWORDCHAR, Show ? 0 : L'*', 0);
        ::InvalidateRect(Edit, nullptr, TRUE);
        return true;
    }
    return CModalDialog::OnButtonClicked(buttonID, buttonHWND);
}

static const UInt32 kVaultSettingsLangIDs[] =
{
    IDX_VAULT_SETTINGS_ENABLED,
    IDT_VAULT_SETTINGS_ABSOLUTE,
    IDT_VAULT_SETTINGS_IDLE,
    IDT_VAULT_SETTINGS_CLIPBOARD,
    IDX_VAULT_SETTINGS_AUDIT,
    IDT_VAULT_SETTINGS_KEEP_UNLOCKED,
    IDT_VAULT_SETTINGS_NEXT_UNLOCK
};

// Whether the vault is on and the three timers that guard it, shown in
// seconds because that is the unit the bounds are written in.
class CVaultSettingsDialog : public NControl::CModalDialog
{
public:
    // Set when the user turned the vault off, so the caller can react.
    bool Disabled = false;

    INT_PTR Create(_In_opt_ HWND ParentWindowHandle)
    {
        return CModalDialog::Create(IDD_VAULT_SETTINGS, ParentWindowHandle);
    }

private:
    virtual bool OnInit();
    virtual bool OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
    virtual void OnOK();

    // Reads one edit box as seconds and converts to milliseconds. False when
    // the text is not a number or falls outside the vault's own bounds.
    bool ReadSeconds(
        unsigned EditID,
        DWORD MinimumMs,
        DWORD MaximumMs,
        DWORD& ResultMs);
};

// Spelled out because the UI works in seconds and the struct in milliseconds.
static constexpr DWORD g_MillisecondsPerSecond = 1000;

bool CVaultSettingsDialog::OnInit()
{
    LangSetWindowText(*this, IDD_VAULT_SETTINGS);
    LangSetDlgItems(
        *this,
        kVaultSettingsLangIDs,
        ARRAY_SIZE(kVaultSettingsLangIDs));

    NanaZip::Vault::VaultSettings Settings =
        NanaZip::Vault::VaultSettings::Load();

    CheckButton(IDX_VAULT_SETTINGS_ENABLED, Settings.Enabled);
    CheckButton(IDX_VAULT_SETTINGS_AUDIT, Settings.Audit);

    struct Field
    {
        unsigned EditID;
        unsigned SpinID;
        DWORD Milliseconds;
        DWORD MinimumMs;
        DWORD MaximumMs;
    };
    const Field Fields[] =
    {
        {
            IDE_VAULT_SETTINGS_ABSOLUTE,
            IDC_VAULT_SETTINGS_ABSOLUTE,
            Settings.AbsoluteTimeoutMs,
            // Zero keeps the vault unlocked until the process exits, so both
            // lock timers start their spin there; the clipboard one does not.
            0,
            NanaZip::Vault::VaultSettings::MaximumTimeoutMs
        },
        {
            IDE_VAULT_SETTINGS_IDLE,
            IDC_VAULT_SETTINGS_IDLE,
            Settings.IdleTimeoutMs,
            0,
            NanaZip::Vault::VaultSettings::MaximumTimeoutMs
        },
        {
            IDE_VAULT_SETTINGS_CLIPBOARD,
            IDC_VAULT_SETTINGS_CLIPBOARD,
            Settings.ClipboardClearMs,
            NanaZip::Vault::VaultSettings::MinimumClipboardClearMs,
            NanaZip::Vault::VaultSettings::MaximumClipboardClearMs
        }
    };

    for (const Field& Field : Fields)
    {
        // Same bounds as OnOK, so the arrows cannot walk the value out.
        SendItemMessage(
            Field.SpinID,
            UDM_SETRANGE32,
            static_cast<WPARAM>(Field.MinimumMs / g_MillisecondsPerSecond),
            static_cast<LPARAM>(Field.MaximumMs / g_MillisecondsPerSecond));
        SetItemInt(
            Field.EditID,
            Field.Milliseconds / g_MillisecondsPerSecond,
            false);
    }

    return CModalDialog::OnInit();
}

bool CVaultSettingsDialog::OnMessage(
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (IsActivityMessage(message))
    {
        GetSession().OnActivity();
    }
    return CModalDialog::OnMessage(message, wParam, lParam);
}

bool CVaultSettingsDialog::ReadSeconds(
    unsigned EditID,
    DWORD MinimumMs,
    DWORD MaximumMs,
    DWORD& ResultMs)
{
    UINT Seconds = 0;
    if (!GetItemInt(EditID, false, Seconds))
    {
        return false;
    }
    // Widened before the multiply: a typed value near UINT_MAX would
    // otherwise wrap into the accepted range.
    UINT64 Milliseconds =
        static_cast<UINT64>(Seconds) * g_MillisecondsPerSecond;
    if (Milliseconds < MinimumMs || Milliseconds > MaximumMs)
    {
        return false;
    }
    ResultMs = static_cast<DWORD>(Milliseconds);
    return true;
}

void CVaultSettingsDialog::OnOK()
{
    // Loaded, not default-built: the dialog edits some fields, and the
    // rest keep what the user already chose.
    NanaZip::Vault::VaultSettings Settings =
        NanaZip::Vault::VaultSettings::Load();

    DWORD AbsoluteMs = 0;
    DWORD IdleMs = 0;
    DWORD ClipboardMs = 0;
    if (!ReadSeconds(
            IDE_VAULT_SETTINGS_ABSOLUTE,
            0,
            NanaZip::Vault::VaultSettings::MaximumTimeoutMs,
            AbsoluteMs)
        || !ReadSeconds(
            IDE_VAULT_SETTINGS_IDLE,
            0,
            NanaZip::Vault::VaultSettings::MaximumTimeoutMs,
            IdleMs)
        || !ReadSeconds(
            IDE_VAULT_SETTINGS_CLIPBOARD,
            NanaZip::Vault::VaultSettings::MinimumClipboardClearMs,
            NanaZip::Vault::VaultSettings::MaximumClipboardClearMs,
            ClipboardMs))
    {
        // Nothing is written when one field is out of range.
        ::MessageBoxW(
            (HWND)*this,
            LangString(IDS_VAULT_SETTINGS_INVALID),
            MessageTitle,
            MB_ICONWARNING);
        return;
    }

    // Turning the vault on here takes the same warning and Hello probe as
    // every other entry point; a refusal leaves the box and the vault off.
    bool Enabled = IsButtonCheckedBool(IDX_VAULT_SETTINGS_ENABLED);
    if (Enabled && !Settings.Enabled)
    {
        if (!NanaZip::FileManager::VaultDialog::RequestEnable((HWND)*this))
        {
            CheckButton(IDX_VAULT_SETTINGS_ENABLED, false);
            Enabled = false;
        }
    }

    Settings.AbsoluteTimeoutMs = AbsoluteMs;
    Settings.IdleTimeoutMs = IdleMs;
    Settings.ClipboardClearMs = ClipboardMs;
    Settings.Audit = IsButtonCheckedBool(IDX_VAULT_SETTINGS_AUDIT);
    Settings.Enabled = Enabled;

    Disabled = !Enabled;
    Settings.Save();

    CModalDialog::OnOK();
}

static const UInt32 kVaultLangIDs[] =
{
    IDB_VAULT_UNLOCK,
    IDB_VAULT_EDIT,
    IDB_VAULT_DELETE,
    IDB_VAULT_COPY,
    IDB_VAULT_SETTINGS
};

// The vault window. Hello fires once, on unlock; every use inside that
// session is covered by it until a timer locks the vault again.
class CVaultDialog : public NControl::CModalDialog
{
public:
    CVaultDialog() = default;

    INT_PTR Create(_In_opt_ HWND ParentWindowHandle)
    {
        return CModalDialog::Create(IDD_VAULT, ParentWindowHandle);
    }

private:
    NControl::CListView m_List;
    std::vector<NanaZip::Vault::EntryMeta> m_Entries;
    // A tracking tooltip of our own: the list's info tip only offers the
    // first column, and the path has to be readable from any of them.
    HWND m_Tooltip = nullptr;
    TOOLINFOW m_TooltipInfo = {};
    std::wstring m_TooltipText;
    int m_TooltipRow = -1;
    bool m_TrackingLeave = false;
    // Shared with the coroutine, which must not touch `this` after OnDestroy.
    struct UnlockState
    {
        bool Alive = false;
        bool InFlight = false;
    };
    std::shared_ptr<UnlockState> m_UnlockState;

    virtual bool OnInit();
    virtual bool OnDestroy();
    virtual bool OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
    virtual bool OnNotify(UINT controlID, LPNMHDR header);
    virtual bool OnButtonClicked(int buttonID, HWND buttonHWND);

    static LRESULT CALLBACK ListSubclassProc(
        HWND Window,
        UINT Message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR SubclassId,
        DWORD_PTR RefData);

    void CreateTooltip();
    void UpdateTooltip(_In_ UINT Message);
    void HideTooltip();
    void TrackLeave();

    void RefreshList();
    void UpdateState();
    void SetStatus(_In_ const UString& Text);

    int GetSelectedEntryIndex() const;

    // A coroutine, because a Hello prompt must not block the thread that owns
    // the dialog, and the dialog is modal to the prompt.
    winrt::fire_and_forget PromptUnlock();

    void SaveEntry(_In_ const CVaultEntryDialog& Source);
    void DeleteSelectedEntry();
    void CopySelectedPassword();
    void EditSelectedEntry();
    void EditSettings();
};

void CVaultDialog::RefreshList()
{
    // The rows are about to be rebuilt; a tip pointing at one of them would
    // name an entry that is no longer there.
    HideTooltip();
    if (!GetSession().IsUnlocked())
    {
        // A locked vault shows nothing; the list returns with the unlock.
        m_Entries.clear();
        m_List.DeleteAllItems();
        return;
    }

    // Keep the selection across the rebuild so the action buttons do not go
    // grey after a delete or a save.
    int Previous = -1;
    int PreviousRow = m_List.GetNextSelectedItem(-1);
    if (PreviousRow >= 0)
    {
        LPARAM Param = 0;
        if (m_List.GetItemParam(PreviousRow, Param))
        {
            Previous = static_cast<int>(Param);
        }
    }

    m_Entries = GetStore()->ListEntries();

    m_List.SetRedraw(false);
    m_List.DeleteAllItems();
    LVITEMW Item = {};
    for (size_t Index = 0; Index < m_Entries.size(); ++Index)
    {
        const NanaZip::Vault::EntryMeta& Meta = m_Entries[Index];
        // The path is what tells two entries apart; the fingerprint is the
        // matching key and says nothing a person can read.
        const std::wstring Shown = EntryDisplayText(Meta);
        Item.mask = LVIF_TEXT | LVIF_PARAM;
        Item.iItem = static_cast<int>(Index);
        Item.iSubItem = 0;
        Item.lParam = static_cast<LPARAM>(Index);
        Item.pszText = const_cast<LPWSTR>(Shown.c_str());
        m_List.InsertItem(&Item);
        m_List.SetSubItem(
            static_cast<unsigned>(Index),
            1,
            Meta.Label.c_str());
    }
    if (Previous >= static_cast<int>(m_Entries.size()))
    {
        Previous = static_cast<int>(m_Entries.size()) - 1;
    }
    if (Previous >= 0)
    {
        m_List.SetItemState(
            Previous,
            LVIS_SELECTED | LVIS_FOCUSED,
            LVIS_SELECTED | LVIS_FOCUSED);
    }
    m_List.SetRedraw(true);
    m_List.InvalidateRect(nullptr, true);
}

void CVaultDialog::UpdateState()
{
    // Another instance can turn the vault off while this window is open.
    bool Enabled = IsVaultEnabled();
    if (!Enabled && GetSession().IsUnlocked())
    {
        GetSession().Lock();
    }
    bool Unlocked = Enabled && GetSession().IsUnlocked();
    int Selected = GetSelectedEntryIndex();
    bool HasSelection = Unlocked && Selected >= 0;

    EnableItem(IDL_VAULT_LIST, Unlocked);
    EnableItem(IDB_VAULT_EDIT, HasSelection);
    EnableItem(IDB_VAULT_DELETE, HasSelection);
    EnableItem(IDB_VAULT_COPY, HasSelection);
    EnableItem(IDB_VAULT_UNLOCK, Enabled);
    // One button both ways; a separate Lock would sit next to Close.
    SetItemText(
        IDB_VAULT_UNLOCK,
        LangString(Unlocked ? IDS_VAULT_LOCK : IDS_VAULT_UNLOCK));

    if (!Enabled)
    {
        SetStatus(LangString(IDS_VAULT_DISABLED));
    }
    else if (Unlocked)
    {
        UString Count;
        Count = std::to_wstring(m_Entries.size()).c_str();
        SetStatus(MyFormatNew(IDS_VAULT_ENTRY_COUNT, Count));
    }
    else
    {
        SetStatus(LangString(IDS_VAULT_LOCKED));
    }
}

void CVaultDialog::SetStatus(_In_ const UString& Text)
{
    SetItemText(IDT_VAULT_STATUS, Text);
}

int CVaultDialog::GetSelectedEntryIndex() const
{
    int Index = m_List.GetNextSelectedItem(-1);
    if (Index < 0)
    {
        return -1;
    }
    LPARAM Param = 0;
    if (!m_List.GetItemParam(Index, Param))
    {
        return -1;
    }
    if (Param < 0 || static_cast<size_t>(Param) >= m_Entries.size())
    {
        return -1;
    }
    return static_cast<int>(Param);
}

bool CVaultDialog::OnInit()
{
    LangSetWindowText(*this, IDD_VAULT);
    LangSetDlgItems(*this, kVaultLangIDs, ARRAY_SIZE(kVaultLangIDs));

    m_List.Attach(GetItem(IDL_VAULT_LIST));
    m_List.SetUnicodeFormat();
    m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);

    m_List.InsertColumn(0, LangString(IDS_VAULT_COLUMN_ARCHIVE), 100);
    m_List.InsertColumn(1, LangString(IDS_VAULT_COLUMN_LABEL), 100);
    // A list view autosizes only against content, so measure a throwaway row.
    m_List.InsertItem(0, L"AAAAAAAAAAAAAAAA");
    m_List.SetSubItem(0, 1, L"AAAAAAAAAAAAAAAA");
    for (int Column = 0; Column < 2; ++Column)
    {
        m_List.SetColumnWidthAuto(Column);
    }
    m_List.DeleteAllItems();

    CreateTooltip();

    m_UnlockState = std::make_shared<UnlockState>();
    m_UnlockState->Alive = true;
    GetSession().AddNotifyWindow((HWND)*this);

    // Populate before deciding to prompt: a session already warm from an
    // earlier save must show its entries, not a lock/unlock round trip.
    RefreshList();

    // Prompt at once: an empty list until a button is found reads as broken.
    if (!GetSession().IsUnlocked())
    {
        PromptUnlock();
    }
    else
    {
        // Already warm: pre-select row 0 so the action buttons light up.
        if (m_List.GetItemCount() > 0)
        {
            m_List.SetItemState(
                0,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
        }
        UpdateState();
    }

    return CModalDialog::OnInit();
}

bool CVaultDialog::OnDestroy()
{
    ::RemoveWindowSubclass((HWND)m_List, ListSubclassProc, 1);
    if (m_Tooltip)
    {
        ::DestroyWindow(m_Tooltip);
        m_Tooltip = nullptr;
    }
    if (m_UnlockState)
    {
        m_UnlockState->Alive = false;
    }
    GetSession().RemoveNotifyWindow((HWND)*this);
    return CModalDialog::OnDestroy();
}

void CVaultDialog::CreateTooltip()
{
    m_Tooltip = ::CreateWindowExW(
        0,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        (HWND)*this,
        nullptr,
        ::GetModuleHandleW(nullptr),
        nullptr);
    if (!m_Tooltip)
    {
        return;
    }
    m_TooltipInfo.cbSize = sizeof(m_TooltipInfo);
    // Tracked rather than anchored to a rectangle: the text follows the row
    // under the cursor, so nothing has to be kept in step when the list
    // scrolls or its columns are resized.
    m_TooltipInfo.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    m_TooltipInfo.hwnd = (HWND)*this;
    m_TooltipInfo.uId = 1;
    m_TooltipInfo.lpszText = const_cast<LPWSTR>(L"");
    ::SendMessageW(m_Tooltip, TTM_ADDTOOLW, 0, (LPARAM)&m_TooltipInfo);
    ::SetWindowSubclass((HWND)m_List, ListSubclassProc, 1, (DWORD_PTR)this);
}

void CVaultDialog::TrackLeave()
{
    if (m_TrackingLeave)
    {
        return;
    }
    TRACKMOUSEEVENT Track = {};
    Track.cbSize = sizeof(Track);
    Track.dwFlags = TME_LEAVE;
    Track.hwndTrack = (HWND)m_List;
    m_TrackingLeave = (TRUE == ::TrackMouseEvent(&Track));
}

void CVaultDialog::UpdateTooltip(_In_ UINT Message)
{
    // The pointer left the list, or the click, the wheel and the scroll bar
    // all move the rows out from under a tip that is already up.
    if (Message != WM_MOUSEMOVE)
    {
        HideTooltip();
        return;
    }
    if (!m_Tooltip)
    {
        return;
    }
    TrackLeave();

    POINT Screen = {};
    ::GetCursorPos(&Screen);
    POINT Client = Screen;
    ::ScreenToClient((HWND)m_List, &Client);
    LVHITTESTINFO Hit = {};
    Hit.pt = Client;
    int Row = m_List.HitTest(&Hit);
    // Empty space below the last row, or a vault that locked while the
    // pointer sat still, leaves nothing to say.
    if (Row < 0 || static_cast<size_t>(Row) >= m_Entries.size())
    {
        HideTooltip();
        return;
    }
    if (Row == m_TooltipRow)
    {
        return;
    }

    // One string built per hovered row: a tooltip for every entry would
    // hold every path in the vault at once.
    std::wstring Text =
        EntryTooltipText(m_Entries[static_cast<size_t>(Row)]);
    if (Text.empty())
    {
        HideTooltip();
        return;
    }
    HideTooltip();
    m_TooltipText = std::move(Text);
    m_TooltipInfo.lpszText = m_TooltipText.data();
    ::SendMessageW(m_Tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&m_TooltipInfo);
    ::SendMessageW(
        m_Tooltip,
        TTM_TRACKPOSITION,
        0,
        MAKELONG(Screen.x + 16, Screen.y + 20));
    ::SendMessageW(
        m_Tooltip,
        TTM_TRACKACTIVATE,
        TRUE,
        (LPARAM)&m_TooltipInfo);
    m_TooltipRow = Row;
}

void CVaultDialog::HideTooltip()
{
    if (m_TooltipRow >= 0 && m_Tooltip)
    {
        ::SendMessageW(
            m_Tooltip,
            TTM_TRACKACTIVATE,
            FALSE,
            (LPARAM)&m_TooltipInfo);
    }
    m_TooltipRow = -1;
    if (!m_TooltipText.empty())
    {
        WipeWideString(m_TooltipText);
        m_TooltipInfo.lpszText = const_cast<LPWSTR>(L"");
    }
}

LRESULT CALLBACK CVaultDialog::ListSubclassProc(
    HWND Window,
    UINT Message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR SubclassId,
    DWORD_PTR RefData)
{
    UNREFERENCED_PARAMETER(SubclassId);
    switch (Message)
    {
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
    case WM_KEYDOWN:
    {
        // Only the tip is ours; the list keeps its own behaviour for the rest.
        CVaultDialog* Dialog = reinterpret_cast<CVaultDialog*>(RefData);
        if (Dialog != nullptr)
        {
            Dialog->UpdateTooltip(Message);
        }
        break;
    }
    }
    return ::DefSubclassProc(Window, Message, wParam, lParam);
}

bool CVaultDialog::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (NanaZip::Vault::Session::NotifyMessage == message)
    {
        if (NanaZip::Vault::Session::NotifyClipboardCleared == wParam)
        {
            SetStatus(LangString(IDS_VAULT_CLIPBOARD_CLEARED));
        }
        else if (NanaZip::Vault::Session::NotifyClipboardClearFailed == wParam)
        {
            SetStatus(LangString(IDS_VAULT_CLIPBOARD_CLEAR_FAILED));
        }
        else
        {
            RefreshList();
            UpdateState();
        }
        return true;
    }
    if (IsActivityMessage(message))
    {
        GetSession().OnActivity();
    }
    return CModalDialog::OnMessage(message, wParam, lParam);
}

bool CVaultDialog::OnNotify(UINT /* controlID */, LPNMHDR Header)
{
    if (Header->hwndFrom != m_List)
    {
        return false;
    }
    switch (Header->code)
    {
    case LVN_ITEMACTIVATE:
        if (g_LVN_ITEMACTIVATE_Support)
        {
            CopySelectedPassword();
        }
        break;
    case NM_DBLCLK:
        if (!g_LVN_ITEMACTIVATE_Support)
        {
            CopySelectedPassword();
        }
        break;
    case LVN_ITEMCHANGED:
        if (LPNMLISTVIEW(Header)->uChanged & LVIF_STATE)
        {
            UpdateState();
        }
        break;
    }
    return false;
}

bool CVaultDialog::OnButtonClicked(int buttonID, HWND buttonHWND)
{
    switch (buttonID)
    {
    case IDB_VAULT_UNLOCK:
        if (GetSession().IsUnlocked())
        {
            GetSession().Lock();
            RefreshList();
            UpdateState();
        }
        else
        {
            PromptUnlock();
        }
        break;
    case IDB_VAULT_EDIT:
        EditSelectedEntry();
        break;
    case IDB_VAULT_DELETE:
        DeleteSelectedEntry();
        break;
    case IDB_VAULT_COPY:
        CopySelectedPassword();
        break;
    case IDB_VAULT_SETTINGS:
        EditSettings();
        break;
    default:
        return CModalDialog::OnButtonClicked(buttonID, buttonHWND);
    }
    return true;
}

winrt::fire_and_forget CVaultDialog::PromptUnlock()
{
    std::shared_ptr<UnlockState> State = m_UnlockState;
    if (!State || State->InFlight)
    {
        co_return;
    }
    State->InFlight = true;
    struct InFlightGuard
    {
        std::shared_ptr<UnlockState> State;
        ~InFlightGuard()
        {
            State->InFlight = false;
        }
    } Guard{ State };

    if (!IsVaultEnabled())
    {
        UpdateState();
        co_return;
    }

    // The key file, the Hello prompt and the session unlock all happen
    // inside; the status says how far it got.
    NanaZip::Vault::KeyGateStatus Status =
        NanaZip::Vault::KeyGateStatus::Failed;
    try
    {
        Status = static_cast<NanaZip::Vault::KeyGateStatus>(
            co_await NanaZip::Vault::KeyGate::UnlockAsync());
    }
    catch (...)
    {
        // A prompt that cannot be shown is a refusal like any other.
        Status = NanaZip::Vault::KeyGateStatus::Failed;
    }

    // The dialog may have closed while the prompt was up, so `this` is only
    // touched after the token says the object is still alive.
    if (!State->Alive)
    {
        co_return;
    }
    if (!IsVaultEnabled())
    {
        UpdateState();
        co_return;
    }
    switch (Status)
    {
    case NanaZip::Vault::KeyGateStatus::Unlocked:
        break;
    case NanaZip::Vault::KeyGateStatus::Declined:
        SetStatus(LangString(IDS_VAULT_HELLO_FAILED));
        co_return;
    case NanaZip::Vault::KeyGateStatus::KeyLost:
        SetStatus(LangString(IDS_VAULT_KEY_LOST));
        co_return;
    case NanaZip::Vault::KeyGateStatus::Incompatible:
        SetStatus(LangString(IDS_VAULT_INCOMPATIBLE));
        co_return;
    default:
        SetStatus(LangString(IDS_VAULT_UNLOCK_FAILED));
        co_return;
    }
    RefreshList();
    // Pre-select row 0 so the action buttons light up right after the unlock.
    if (m_List.GetItemCount() > 0)
    {
        m_List.SetItemState(
            0,
            LVIS_SELECTED | LVIS_FOCUSED,
            LVIS_SELECTED | LVIS_FOCUSED);
    }
    UpdateState();
}

void CVaultDialog::CopySelectedPassword()
{
    if (!IsVaultEnabled())
    {
        UpdateState();
        return;
    }
    if (!GetSession().IsUnlocked())
    {
        return;
    }
    int Selected = GetSelectedEntryIndex();
    if (Selected < 0)
    {
        return;
    }
    const NanaZip::Vault::EntryMeta& Meta = m_Entries[Selected];
    std::optional<std::wstring> Secret = ReadSecret(
        *GetStore(),
        Meta.Id,
        NanaZip::Vault::VaultStore::PasswordField);
    if (!Secret.has_value())
    {
        // Missing, damaged, or sealed with a key that no longer matches.
        SetStatus(LangString(IDS_VAULT_DECRYPT_FAILED));
        return;
    }

    // Copy first, then wipe this buffer: the other order would put the zeroed
    // text on the clipboard and still report success.
    bool Placed = CopyToClipboard(Secret.value());
    ::SecureZeroMemory(
        Secret->data(),
        Secret->size() * sizeof(wchar_t));
    // Arm the wipe only when the copy landed: pointing it at the clipboard
    // after a failure would clear what the user already had there.
    if (Placed)
    {
        GetSession().ScheduleClipboardClear();
        // Reading an entry is use, so the idle timer restarts here.
        GetSession().OnActivity();
        // The archive key reads like a hash and a label can be blank, so the
        // notice names no entry and just closes the sentence.
        SetStatus(LangString(IDS_VAULT_COPIED));
    }
    else
    {
        SetStatus(LangString(IDS_VAULT_COPY_FAILED));
    }
}

void CVaultDialog::EditSettings()
{
    CVaultSettingsDialog Dialog;
    if (Dialog.Create((HWND)*this) != IDOK)
    {
        // Cancelled, or a value was rejected and nothing was written.
        return;
    }

    // The running session still holds the old numbers.
    GetSession().Reconfigure(NanaZip::Vault::VaultSettings::Load());

    if (!Dialog.Disabled)
    {
        return;
    }

    // Turning the vault off is not deferred to the next launch: the key is
    // erased while the dialog is up, and the window closes with it.
    GetSession().Lock();
    RefreshList();
    UpdateState();
    OnClose();
}

void CVaultDialog::EditSelectedEntry()
{
    if (!IsVaultEnabled())
    {
        UpdateState();
        return;
    }
    if (!GetSession().IsUnlocked())
    {
        return;
    }
    int Selected = GetSelectedEntryIndex();
    if (Selected < 0)
    {
        return;
    }
    // The entry dialog pumps messages and a lock inside it reassigns
    // m_Entries, so the row is copied out by value first.
    NanaZip::Vault::EntryMeta Meta = m_Entries[Selected];

    // Editing reveals: whatever comes back is written whole, so an empty
    // password is a choice, not a request to keep the old one.
    std::optional<std::wstring> Password = ReadSecret(
        *GetStore(),
        Meta.Id,
        NanaZip::Vault::VaultStore::PasswordField);
    if (!Password.has_value())
    {
        SetStatus(LangString(IDS_VAULT_DECRYPT_FAILED));
        return;
    }

    CVaultEntryDialog Dialog;
    Dialog.Meta = Meta;
    Dialog.Password = Password.value();
    if (IDOK == Dialog.Create((HWND)*this))
    {
        SaveEntry(Dialog);
    }
    // The entry dialog is closed by now, but the plaintext still sits in
    // these two stack objects until the scope ends.
    WipeWideString(Dialog.Password);
    WipeWideString(Password.value());
}

void CVaultDialog::SaveEntry(_In_ const CVaultEntryDialog& Source)
{
    // The entry dialog pumps messages, so a lock or an outside disable may
    // have landed while it was up.
    if (!IsVaultEnabled())
    {
        ShowMessage((HWND)*this, IDS_VAULT_DISABLED, MB_ICONWARNING);
        RefreshList();
        UpdateState();
        return;
    }
    // The key is copied before the buffer is built: a lock can land between
    // any two statements, and a save without it would seal nothing.
    NanaZip::Vault::VaultKey Key;
    if (!GetSession().CopyKey(Key))
    {
        ShowMessage((HWND)*this, IDS_VAULT_LOCKED, MB_ICONWARNING);
        RefreshList();
        UpdateState();
        return;
    }
    // A local copy, because UpsertEntry writes the store's id back into it.
    NanaZip::Vault::EntryMeta Meta = Source.Meta;
    NanaZip::Vault::SecureBuffer Buffer = BufferFromWide(Source.Password);
    // An empty buffer from a non-empty password is a scramble failure;
    // saving it would silently blank the entry.
    if (!Source.Password.empty() && Buffer.Size() == 0)
    {
        ShowMessage((HWND)*this, IDS_VAULT_SAVE_FAILED, MB_ICONERROR);
        return;
    }
    if (!GetStore()->UpsertEntry(
        Meta,
        NanaZip::Vault::VaultStore::PasswordField,
        Buffer,
        Key))
    {
        ::MessageBoxW(
            (HWND)*this,
            LangString(IDS_VAULT_SAVE_FAILED),
            MessageTitle,
            MB_ICONERROR);
        return;
    }

    RefreshList();
    UpdateState();
}

void CVaultDialog::DeleteSelectedEntry()
{
    int Selected = GetSelectedEntryIndex();
    if (Selected < 0)
    {
        return;
    }
    // The confirm box pumps messages and a lock inside it reassigns
    // m_Entries, so the row is copied out by value first.
    const NanaZip::Vault::EntryMeta Entry = m_Entries[Selected];
    // The label is the name on the row, but the edit dialog lets it be
    // cleared, so the fingerprint is the fallback.
    UString Question = MyFormatNew(
        IDS_VAULT_DELETE_CONFIRM,
        UStringFromWide(
            Entry.Label.empty() ? Entry.ArchiveKey : Entry.Label));
    // The safe answer is the default one: a delete cannot be undone.
    if (IDYES != ::MessageBoxW(
        (HWND)*this,
        Question,
        MessageTitle,
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2))
    {
        return;
    }
    // The confirmation pumps messages as well, and a lock or disable leaves
    // no row to delete, so the answer no longer applies to what is on screen.
    if (!IsVaultEnabled())
    {
        ShowMessage((HWND)*this, IDS_VAULT_DISABLED, MB_ICONWARNING);
        RefreshList();
        UpdateState();
        return;
    }
    if (!GetSession().IsUnlocked())
    {
        ShowMessage((HWND)*this, IDS_VAULT_LOCKED, MB_ICONWARNING);
        RefreshList();
        UpdateState();
        return;
    }
    bool Deleted = GetStore()->DeleteEntry(Entry.Id);
    RefreshList();
    UpdateState();
    if (!Deleted)
    {
        SetStatus(LangString(IDS_VAULT_DELETE_FAILED));
    }
}

void NanaZip::FileManager::VaultDialog::Show(
    _In_opt_ HWND ParentWindowHandle)
{
    if (g_ShowInProgress)
    {
        return;
    }
    g_ShowInProgress = true;
    ShowDialog(ParentWindowHandle);
}

bool NanaZip::FileManager::VaultDialog::RequestEnable(
    _In_opt_ HWND ParentWindowHandle)
{
    if (!ConfirmEnable(ParentWindowHandle))
    {
        return false;
    }
    // Probed before the setting is written, so a machine without Hello
    // cannot end up with a vault it can never open.
    if (!NanaZip::Vault::VaultCore::IsHelloAvailableOnUiThread())
    {
        ShowMessage(
            ParentWindowHandle,
            IDS_VAULT_HELLO_UNAVAILABLE,
            MB_ICONWARNING);
        return false;
    }

    // Enabling is where the key is born: the credential is created and the
    // key file written here, so the vault opens without another gesture.
    NanaZip::Vault::KeyGateStatus Status =
        NanaZip::Vault::KeyGate::UnlockOnUiThread(ParentWindowHandle);
    if (Status != NanaZip::Vault::KeyGateStatus::Unlocked)
    {
        // A dismissed prompt is an answer, not a failure to report.
        if (Status != NanaZip::Vault::KeyGateStatus::Declined)
        {
            UINT Message = IDS_VAULT_ENABLE_FAILED;
            if (Status == NanaZip::Vault::KeyGateStatus::KeyLost)
            {
                Message = IDS_VAULT_KEY_LOST;
            }
            else if (Status == NanaZip::Vault::KeyGateStatus::Incompatible)
            {
                Message = IDS_VAULT_INCOMPATIBLE;
            }
            ShowMessage(ParentWindowHandle, Message, MB_ICONWARNING);
        }
        return false;
    }

    NanaZip::Vault::VaultSettings Settings =
        NanaZip::Vault::VaultSettings::Load();
    Settings.Enabled = true;
    Settings.Save();
    return true;
}

static void ShowDialog(_In_opt_ HWND ParentWindowHandle)
{
    ShowGuard Guard;

    // WinRT must be up before the dialog's unlock coroutine calls the key
    // gate; the file manager is already an STA, where this is a no-op.
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    NanaZip::Vault::VaultSettings Settings =
        NanaZip::Vault::VaultSettings::Load();
    if (!Settings.Enabled && !NanaZip::FileManager::VaultDialog::RequestEnable(
        ParentWindowHandle))
    {
        return;
    }

    GetStore() = NanaZip::Vault::VaultStore::Open();
    if (!GetStore().has_value())
    {
        ShowMessage(
            ParentWindowHandle,
            IDS_VAULT_OPEN_FAILED,
            MB_ICONERROR);
        return;
    }

    CVaultDialog Dialog;
    Dialog.Create(ParentWindowHandle);
}
