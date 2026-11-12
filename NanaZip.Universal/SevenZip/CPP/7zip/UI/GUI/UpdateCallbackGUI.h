// UpdateCallbackGUI.h

#ifndef ZIP7_INC_UPDATE_CALLBACK_GUI_H
#define ZIP7_INC_UPDATE_CALLBACK_GUI_H

#include "../Common/Update.h"
#include "../Common/ArchiveOpenCallback.h"

#include "UpdateCallbackGUI2.h"

class CUpdateCallbackGUI Z7_final:
  public IOpenCallbackUI,
  public IUpdateCallbackUI2,
  public CUpdateCallbackGUI2
{
  Z7_IFACE_IMP(IOpenCallbackUI)
  Z7_IFACE_IMP(IUpdateCallbackUI)
  Z7_IFACE_IMP(IDirItemsCallback)
  Z7_IFACE_IMP(IUpdateCallbackUI2)

public:
  bool AskPassword;
  FStringVector FailedFiles;

  // **************** NanaZip Modification Start ****************
  // "Add" on an existing archive needs the full compress dialog to
  // supply a password, so the caller reruns UpdateGUI with showDialog.
  bool NeedFullDialog = false;
  bool ExistingArchiveHandled = false;
  // **************** NanaZip Modification End ****************

  CUpdateCallbackGUI():
      AskPassword(false)
      {}
  void Init();
};

#endif
