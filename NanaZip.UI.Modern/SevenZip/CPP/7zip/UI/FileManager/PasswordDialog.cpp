// PasswordDialog.cpp

#include "StdAfx.h"

// **************** NanaZip Modification Start ****************
#include <CommCtrl.h>
// **************** NanaZip Modification End ****************

#include "PasswordDialog.h"

// **************** NanaZip Modification Start ****************
// Registered by the FM layer, so this dialog has no VaultCore dependency.
#ifndef _SFX
#include <string>
#include <utility>
#include <vector>
#include <VaultCore.h>
#include <VaultHooks.h>
#endif
// **************** NanaZip Modification End ****************

#ifdef LANG
#include "LangUtils.h"
#endif

#ifdef LANG
static const UInt32 kLangIDs[] =
{
  IDT_PASSWORD_ENTER,
  IDX_PASSWORD_SHOW
};
// **************** NanaZip Modification Start ****************
static const UInt32 kVaultLangIDs[] =
{
  IDX_PASSWORD_SAVE_TO_VAULT,
  IDT_PASSWORD_VAULT_SUGGEST
};
// **************** NanaZip Modification End ****************
#endif

// **************** NanaZip Modification Start ****************
#ifndef _SFX
// ES_PASSWORD only masks the glyphs: WM_GETTEXT and EM_GETLINE still return
// the real text to any process. Drop them when InSendMessage says they came
// from another thread; the dialog's own calls run in the window procedure.
static LRESULT CALLBACK PasswordEditSubclassProc(
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
#endif
// **************** NanaZip Modification End ****************

void CPasswordDialog::ReadControls()
{
  _passwordEdit.GetText(Password);
  ShowPassword = IsButtonCheckedBool(IDX_PASSWORD_SHOW);
}

void CPasswordDialog::SetTextSpec()
{
  _passwordEdit.SetPasswordChar(ShowPassword ? 0: TEXT('*'));
  _passwordEdit.SetText(Password);
}

bool CPasswordDialog::OnInit()
{
  #ifdef LANG
  LangSetWindowText(*this, IDD_PASSWORD);
  LangSetDlgItems(*this, kLangIDs, ARRAY_SIZE(kLangIDs));
  // **************** NanaZip Modification Start ****************
  LangSetDlgItems(*this, kVaultLangIDs, ARRAY_SIZE(kVaultLangIDs));
  // **************** NanaZip Modification End ****************
  #endif
  _passwordEdit.Attach(GetItem(IDE_PASSWORD_PASSWORD));

// **************** NanaZip Modification Start ****************
#ifndef _SFX
  ::SetWindowSubclass(_passwordEdit, PasswordEditSubclassProc, 1, 0);
#endif
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
#ifndef _SFX
  // The vault is opt-in: when the hooks were never registered or the user
  // disabled the vault, hide every control so the dialog reads as before.
  bool VaultActive = (g_VaultHasMatchHook != nullptr) &&
      ::NanaZip::Vault::VaultSettings::Load().Enabled;
  bool HasMatch = false;
  if (VaultActive)
  {
    g_VaultHasMatchHook(ArchivePath, HasMatch);
  }
  // A match hides the save checkbox: the vault needs no second copy. The
  // dropdown lists every entry anyway, since one password often fits several.
  bool ShowSave = VaultActive && ShowSaveToVault && !HasMatch;
  std::vector<std::pair<std::wstring, std::wstring>> Suggestions;
  bool ShowCombo = false;
  if (VaultActive && g_VaultListHook)
  {
    Suggestions = g_VaultListHook();
    ShowCombo = !Suggestions.empty();
  }

  ::ShowWindow(
      GetItem(IDX_PASSWORD_SAVE_TO_VAULT),
      ShowSave ? SW_SHOW : SW_HIDE);
  ::ShowWindow(
      GetItem(IDC_PASSWORD_VAULT_SUGGEST),
      ShowCombo ? SW_SHOW : SW_HIDE);
  ::ShowWindow(
      GetItem(IDT_PASSWORD_VAULT_SUGGEST),
      ShowCombo ? SW_SHOW : SW_HIDE);
  if (ShowCombo)
  {
    _vaultCombo.Attach(GetItem(IDC_PASSWORD_VAULT_SUGGEST));
    for (const auto& Sug : Suggestions)
    {
      _vaultCombo.AddString(Sug.second.c_str());
    }
    VaultSuggestions = std::move(Suggestions);
  }

  if (ShowSave)
  {
    CheckButton(IDX_PASSWORD_SAVE_TO_VAULT, SaveToVault);
  }

  // Collapse the vertical gap the hidden vault controls leave: repack the
  // rows below, then shrink the dialog. The combo row moves as a unit, or
  // it would clip the "Show password" label when the box hides.
  int dyCombo = ShowSave ? 0 : 14;    // original combo Y = 54

  int showPasswordY = 40;
  if (ShowSave) showPasswordY += 10 + 4;   // checkbox height + 4px gap
  if (ShowCombo) showPasswordY += 14 + 6;  // combo height + 6px gap
  int okCancelY = showPasswordY + 10 + 12; // checkbox height + 12px gap
  int newDlgHeight = okCancelY + 16 + 8;   // button height + bottom margin

  int dyShow = 74 - showPasswordY;   // original "Show password" Y = 74
  int dyBtn = 96 - okCancelY;        // original OK/Cancel Y = 96
  int dHeight = 120 - newDlgHeight;  // original dialog height = 120

  if (dyShow != 0 || dHeight != 0)
  {
    RECT dlu = {0, 0, 0, 1};
    ::MapDialogRect(*this, &dlu);
    int pxPerDlu = dlu.bottom;

    RECT rc;
    if (ShowCombo && dyCombo != 0)
    {
      ::GetWindowRect(GetItem(IDC_PASSWORD_VAULT_SUGGEST), &rc);
      ::ScreenToClient(*this, (LPPOINT)&rc);
      ::SetWindowPos(GetItem(IDC_PASSWORD_VAULT_SUGGEST), NULL, rc.left,
          rc.top - dyCombo * pxPerDlu, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
      ::GetWindowRect(GetItem(IDT_PASSWORD_VAULT_SUGGEST), &rc);
      ::ScreenToClient(*this, (LPPOINT)&rc);
      ::SetWindowPos(GetItem(IDT_PASSWORD_VAULT_SUGGEST), NULL, rc.left,
          rc.top - dyCombo * pxPerDlu, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ::GetWindowRect(GetItem(IDX_PASSWORD_SHOW), &rc);
    ::ScreenToClient(*this, (LPPOINT)&rc);
    ::SetWindowPos(GetItem(IDX_PASSWORD_SHOW), NULL, rc.left,
        rc.top - dyShow * pxPerDlu, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ::GetWindowRect(GetItem(IDOK), &rc);
    ::ScreenToClient(*this, (LPPOINT)&rc);
    ::SetWindowPos(GetItem(IDOK), NULL, rc.left,
        rc.top - dyBtn * pxPerDlu, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ::GetWindowRect(GetItem(IDCANCEL), &rc);
    ::ScreenToClient(*this, (LPPOINT)&rc);
    ::SetWindowPos(GetItem(IDCANCEL), NULL, rc.left,
        rc.top - dyBtn * pxPerDlu, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ::GetWindowRect(*this, &rc);
    ::SetWindowPos(*this, NULL, 0, 0, rc.right - rc.left,
        (rc.bottom - rc.top) - dHeight * pxPerDlu,
        SWP_NOMOVE | SWP_NOZORDER);
  }

  VaultTracking = VaultActive;
  if (VaultTracking)
  {
    ::NanaZip::Vault::VaultCore::GetSession().AddNotifyWindow((HWND)*this);
  }
#endif
// **************** NanaZip Modification End ****************

  CheckButton(IDX_PASSWORD_SHOW, ShowPassword);
  SetTextSpec();
  return CModalDialog::OnInit();
}

// **************** NanaZip Modification Start ****************
bool CPasswordDialog::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
#ifndef _SFX
  if (NanaZip::Vault::Session::NotifyMessage == message)
  {
    if (NanaZip::Vault::Session::NotifyLocked == wParam)
    {
      // A revealed password must not stay on screen once the vault locks.
      Password.Wipe_and_Empty();
      ShowPassword = false;
      CheckButton(IDX_PASSWORD_SHOW, false);
      SetTextSpec();
      if (!VaultSuggestions.empty())
      {
        _vaultCombo.SetCurSel(-1);
      }
    }
    return true;
  }
  // Typing here is use of a warm session; without this the idle timer could
  // lock the vault while the user is entering a password.
  if (VaultTracking && (WM_KEYDOWN == message || WM_CHAR == message
      || WM_LBUTTONDOWN == message || WM_MOUSEMOVE == message
      || WM_COMMAND == message))
  {
    ::NanaZip::Vault::VaultCore::GetSession().OnActivity();
  }
#endif
  return CModalDialog::OnMessage(message, wParam, lParam);
}

bool CPasswordDialog::OnDestroy()
{
#ifndef _SFX
  if (VaultTracking)
  {
    ::NanaZip::Vault::VaultCore::GetSession().RemoveNotifyWindow((HWND)*this);
  }
  if (_passwordEdit)
  {
    ::RemoveWindowSubclass(_passwordEdit, PasswordEditSubclassProc, 1);
  }
#endif
  return CModalDialog::OnDestroy();
}
// **************** NanaZip Modification End ****************

bool CPasswordDialog::OnButtonClicked(int buttonID, HWND buttonHWND)
{
  if (buttonID == IDX_PASSWORD_SHOW)
  {
    ReadControls();
    SetTextSpec();
    return true;
  }
// **************** NanaZip Modification Start ****************
#ifndef _SFX
  if (buttonID == IDX_PASSWORD_SAVE_TO_VAULT)
  {
    SaveToVault = IsButtonCheckedBool(IDX_PASSWORD_SAVE_TO_VAULT);
    return true;
  }
#endif
// **************** NanaZip Modification End ****************
  return CDialog::OnButtonClicked(buttonID, buttonHWND);
}

// **************** NanaZip Modification Start ****************
// Outside the _SFX guard on purpose: the header declares the override
// unconditionally, so every build emitting the vtable needs the symbol.
bool CPasswordDialog::OnCommand(int code, int itemID, LPARAM lParam)
{
#ifndef _SFX
  if (code == CBN_SELCHANGE && itemID == IDC_PASSWORD_VAULT_SUGGEST)
  {
    // Picking a label is the explicit trust gesture, so this is where the
    // Hello prompt arms. A programmatic selection never lands here.
    int Index = _vaultCombo.GetCurSel();
    if (Index >= 0 && Index < static_cast<int>(VaultSuggestions.size())
        && g_VaultRevealHook)
    {
      std::wstring Pwd;
      if (g_VaultRevealHook(VaultSuggestions[Index].first, (HWND)*this, Pwd))
      {
        Password = Pwd.c_str();
        SetTextSpec();
        ::InvalidateRect(_passwordEdit, NULL, TRUE);
      }
    }
    return true;
  }
#endif
  return CModalDialog::OnCommand(code, itemID, lParam);
}
// **************** NanaZip Modification End ****************

void CPasswordDialog::OnOK()
{
  ReadControls();
// **************** NanaZip Modification Start ****************
#ifndef _SFX
  // Recorded, not committed: the caller persists only after the archive
  // opens successfully, so a mistyped password never reaches the vault.
  SaveRequested = SaveToVault && !Password.IsEmpty();
#endif
// **************** NanaZip Modification End ****************
  CModalDialog::OnOK();
}
