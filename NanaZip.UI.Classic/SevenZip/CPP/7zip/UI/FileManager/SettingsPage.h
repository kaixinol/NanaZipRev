// SettingsPage.h
 
#ifndef __SETTINGS_PAGE_H
#define __SETTINGS_PAGE_H

#include "../../../Windows/Control/PropertyPage.h"
#include "../../../Windows/Control/ComboBox.h"
#include "../../../Windows/Control/Edit.h"

class CSettingsPage: public NWindows::NControl::CPropertyPage
{
  bool _wasChanged;
  bool _largePages_wasChanged;
  // **************** NanaZip Modification Start ****************
  bool _wasChanged_ThemeMode;
  // **************** NanaZip Modification End ****************
  /*
  bool _wasChanged_MemLimit;
  NWindows::NControl::CComboBox _memCombo;
  UStringVector _memLimitStrings;
  UInt64 _ramSize;
  UInt64 _ramSize_Defined;
 
  int AddMemComboItem(UInt64 size, UInt64 percents = 0, bool isDefault = false);
  */

  // void EnableSubItems();
  // **************** NanaZip Modification Start ****************
  bool OnCommand(int code, int itemID, LPARAM param);
  // **************** NanaZip Modification End ****************
  bool OnButtonClicked(int buttonID, HWND buttonHWND);
  virtual bool OnInit();
  virtual LONG OnApply();
public:
};

#endif
