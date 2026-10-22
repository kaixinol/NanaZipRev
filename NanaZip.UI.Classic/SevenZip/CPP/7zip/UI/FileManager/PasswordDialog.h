// PasswordDialog.h

#ifndef __PASSWORD_DIALOG_H
#define __PASSWORD_DIALOG_H

#include "../../../Windows/Control/Dialog.h"
#include "../../../Windows/Control/Edit.h"
// **************** NanaZip Modification Start ****************
#include "../../../Windows/Control/ComboBox.h"
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
// Not every flavor's precompiled header provides these, so include them here.
#include <string>
#include <utility>
#include <vector>
// **************** NanaZip Modification End ****************

#include "PasswordDialogRes.h"

class CPasswordDialog: public NWindows::NControl::CModalDialog
{
  NWindows::NControl::CEdit _passwordEdit;
  // **************** NanaZip Modification Start ****************
  NWindows::NControl::CComboBox _vaultCombo;
  // **************** NanaZip Modification End ****************

  virtual void OnOK();
  virtual bool OnInit();
  // **************** NanaZip Modification Start ****************
  virtual bool OnDestroy();
  virtual bool OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
  virtual bool OnCommand(int code, int itemID, LPARAM lParam);
  // **************** NanaZip Modification End ****************
  virtual bool OnButtonClicked(int buttonID, HWND buttonHWND);
  void SetTextSpec();
  void ReadControls();
public:
  UString Password;
  bool ShowPassword;

  // **************** NanaZip Modification Start ****************
  // The archive path, not a fingerprint: every hook computes the key itself.
  // When the vault matches and the session is unlocked the dialog never shows.
#ifndef _SFX
  std::wstring ArchivePath;
  // The name shown in the vault list for a freshly saved entry; empty means
  // "no label", which the list renders as "(no label)".
  std::wstring ArchiveLabel;
  // Whether the "save to vault" checkbox starts checked, and the choice the
  // dialog reports back so a later change of heart can update the setting.
  bool SaveToVault = false;
  // Set by OnOK so the caller can tell a real save from a cancel.
  bool SaveRequested = false;
  // Whether the checkbox is shown at all: the extract path does not wire the
  // deferred save yet, so it hides the control rather than show a dead one.
  bool ShowSaveToVault = true;
  // The vault entries offered in the dropdown as (id, label) pairs. Empty
  // when the vault is off or has no entries, which is what hides it.
  std::vector<std::pair<std::wstring, std::wstring>> VaultSuggestions;
  // Set when this dialog joined the vault's lock notifications.
  bool VaultTracking = false;
#endif
  // **************** NanaZip Modification End ****************

  CPasswordDialog(): ShowPassword(false) {}
  INT_PTR Create(HWND parentWindow = 0) { return CModalDialog::Create(IDD_PASSWORD, parentWindow); }
};

#endif
