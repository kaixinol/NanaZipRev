// RegistryUtils.h

#ifndef __REGISTRY_UTILS_H
#define __REGISTRY_UTILS_H

#include "../../../Common/MyTypes.h"
#include "../../../Common/MyString.h"

void SaveRegLang(const UString &path);
void ReadRegLang(UString &path);

void SaveRegEditor(bool useEditor, const UString &path);
void ReadRegEditor(bool useEditor, UString &path);

void SaveRegDiff(const UString &path);
void ReadRegDiff(UString &path);

void ReadReg_VerCtrlPath(UString &path);

// **************** NanaZip Modification Start ****************
/* Remembers the filename code page the user last chose for archive names. Zero
   means "no explicit choice", which leaves the handler to decode names as it
   sees fit. */
void SaveRegCodePage(unsigned codePage);
void ReadRegCodePage(unsigned &codePage);
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
/* The theme the application uses, in the order the settings page offers it.
   The numbering is the one K7_USER_THEME_MODE declares, so the value can be
   handed to K7User as it is. */
enum EThemeMode
{
  ThemeMode_System = 0,
  ThemeMode_Light = 1,
  ThemeMode_Dark = 2
};

void SaveRegThemeMode(UInt32 themeMode);
UInt32 ReadRegThemeMode();
// **************** NanaZip Modification End ****************

struct CFmSettings
{
  bool ShowDots;
  bool ShowRealFileIcons;
  bool FullRow;
  bool ShowGrid;
  bool SingleClick;
  bool AlternativeSelection;
  bool ArcHistory;
  bool PathHistory;
  bool CopyHistory;
  bool FolderHistory;
  bool LowercaseHashes;
  // bool Underline;

  bool ShowSystemMenu;
  // **************** NanaZip Modification Start ****************
  bool ShowFileSizeUnits;
  // **************** NanaZip Modification End ****************

  void Save() const;
  void Load();
};

// void SaveLockMemoryAdd(bool enable);
// bool ReadLockMemoryAdd();

bool ReadLockMemoryEnable();
void SaveLockMemoryEnable(bool enable);

bool WantArcHistory();
bool WantPathHistory();
bool WantCopyHistory();
bool WantFolderHistory();
bool WantLowercaseHashes();

void SaveFlatView(UInt32 panelIndex, bool enable);
bool ReadFlatView(UInt32 panelIndex);

/*
void Save_ShowDeleted(bool enable);
bool Read_ShowDeleted();
*/

#endif
