// RegistryUtils.cpp

#include "StdAfx.h"

#include "../../../Common/IntToString.h"
// **************** NanaZip Modification Start ****************
// ConvertStringToUInt32 lives here, not in IntToString.h, which only converts
// numbers to text.
#include "../../../Common/StringToInt.h"
// **************** NanaZip Modification End ****************
#include "../../../Windows/Registry.h"

#include "RegistryUtils.h"

using namespace NWindows;
using namespace NRegistry;

// **************** NanaZip Modification Start ****************
// The fork keeps its settings under its own key, so an installed
// upstream package and this one never read each other's settings.
//#define REG_PATH_7Z TEXT("Software") TEXT(STRING_PATH_SEPARATOR) TEXT("NanaZip")
#define REG_PATH_7Z TEXT("Software") TEXT(STRING_PATH_SEPARATOR) TEXT("NanaZipRev")
// **************** NanaZip Modification End ****************

static LPCTSTR const kCUBasePath = REG_PATH_7Z;
static LPCTSTR const kCU_FMPath = REG_PATH_7Z TEXT(STRING_PATH_SEPARATOR) TEXT("FM");
// static LPCTSTR const kLM_Path = REG_PATH_7Z TEXT(STRING_PATH_SEPARATOR) TEXT("FM");

static LPCWSTR const kLangValueName = L"Lang";

static LPCWSTR const kViewer = L"Viewer";
static LPCWSTR const kEditor = L"Editor";
static LPCWSTR const kDiff = L"Diff";
static LPCWSTR const kVerCtrlPath = L"7vc";

static LPCTSTR const kShowDots = TEXT("ShowDots");
static LPCTSTR const kShowRealFileIcons = TEXT("ShowRealFileIcons");
static LPCTSTR const kFullRow = TEXT("FullRow");
static LPCTSTR const kShowGrid = TEXT("ShowGrid");
static LPCTSTR const kSingleClick = TEXT("SingleClick");
static LPCTSTR const kAlternativeSelection = TEXT("AlternativeSelection");
// static LPCTSTR const kUnderline = TEXT("Underline");

static LPCTSTR const kShowSystemMenu = TEXT("ShowSystemMenu");

// static LPCTSTR const kLockMemoryAdd = TEXT("LockMemoryAdd");
static LPCTSTR const kLargePages = TEXT("LargePages");

// they default to off (0) in 7-Zip ZS /TR
static LPCTSTR const kArcHistory = TEXT("WantArcHistory");
static LPCTSTR const kPathHistory = TEXT("WantPathHistory");
static LPCTSTR const kCopyHistory = TEXT("WantCopyHistory");
static LPCTSTR const kFolderHistory = TEXT("WantFolderHistory");
static LPCTSTR const kLowercaseHashes = TEXT("LowercaseHashes");

static LPCTSTR const kFlatViewName = TEXT("FlatViewArc");
// static LPCTSTR const kShowDeletedFiles = TEXT("ShowDeleted");

// **************** NanaZip Modification Start ****************
static LPCTSTR const kShowFileSizeUnits = TEXT("ShowFileSizeUnits");
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
static LPCTSTR const kCodePage = TEXT("CodePage");
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
static LPCTSTR const kThemeMode = TEXT("ThemeMode");
// **************** NanaZip Modification End ****************

static void SaveCuString(LPCTSTR keyPath, LPCWSTR valuePath, LPCWSTR value)
{
  CKey key;
  key.Create(HKEY_CURRENT_USER, keyPath);
  key.SetValue(valuePath, value);
}

static void ReadCuString(LPCTSTR keyPath, LPCWSTR valuePath, UString &res)
{
  res.Empty();
  CKey key;
  if (key.Open(HKEY_CURRENT_USER, keyPath, KEY_READ) == ERROR_SUCCESS)
    key.QueryValue(valuePath, res);
}

void SaveRegLang(const UString &path) { SaveCuString(kCUBasePath, kLangValueName, path); }
void ReadRegLang(UString &path) { ReadCuString(kCUBasePath, kLangValueName, path); }

void SaveRegEditor(bool useEditor, const UString &path) { SaveCuString(kCU_FMPath, useEditor ? kEditor : kViewer, path); }
void ReadRegEditor(bool useEditor, UString &path) { ReadCuString(kCU_FMPath, useEditor ? kEditor : kViewer, path); }

void SaveRegDiff(const UString &path) { SaveCuString(kCU_FMPath, kDiff, path); }
void ReadRegDiff(UString &path) { ReadCuString(kCU_FMPath, kDiff, path); }

void ReadReg_VerCtrlPath(UString &path) { ReadCuString(kCU_FMPath, kVerCtrlPath, path); }

// **************** NanaZip Modification Start ****************
/* The remembered code page is stored as text, not as a DWORD, because a value
   that means "no choice" has to be distinguishable from a page number, and text
   keeps that obvious in the registry. */
void SaveRegCodePage(unsigned codePage)
{
  // **************** NanaZip Modification Start ****************
  /* Code page zero means automatic, which is the absence of a preference rather
     than a preference of its own, so the value is removed instead of stored as
     "0". A reader looking at the key then sees that nothing was chosen, rather
     than having to know what zero means. */
  if (codePage == 0)
  {
    CKey key;
    if (key.Open(HKEY_CURRENT_USER, kCU_FMPath, KEY_WRITE) == ERROR_SUCCESS)
      key.DeleteValue(kCodePage);
    return;
  }
  // **************** NanaZip Modification End ****************
  wchar_t buf[16];
  ConvertUInt32ToString(codePage, buf);
  SaveCuString(kCU_FMPath, kCodePage, buf);
}

void ReadRegCodePage(unsigned &codePage)
{
  codePage = 0;
  UString s;
  ReadCuString(kCU_FMPath, kCodePage, s);
  if (s.IsEmpty())
    return;
  const wchar_t *end;
  const UInt32 value = ConvertStringToUInt32(s, &end);
  if (*end != 0)   // a hand-edited or truncated value is not a code page
    return;
  codePage = value;
}
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
void SaveRegThemeMode(UInt32 themeMode)
{
  CKey key;
  key.Create(HKEY_CURRENT_USER, kCU_FMPath);
  key.SetValue(kThemeMode, themeMode);
}

UInt32 ReadRegThemeMode()
{
  CKey key;
  UInt32 themeMode = 0;
  if (key.Open(HKEY_CURRENT_USER, kCU_FMPath, KEY_READ) == ERROR_SUCCESS)
    key.QueryValue(kThemeMode, themeMode);
  // A hand-edited value must not select an undefined theme. K7User and
  // NanaZip Modern clamp the value again when they read it, so a stale one
  // written by an older build cannot reach the drawing code either.
  if (themeMode > static_cast<UInt32>(ThemeMode_Dark))
    themeMode = static_cast<UInt32>(ThemeMode_System);
  return themeMode;
}
// **************** NanaZip Modification End ****************

static void Save7ZipOption(LPCTSTR value, bool enabled)
{
  CKey key;
  key.Create(HKEY_CURRENT_USER, kCUBasePath);
  key.SetValue(value, enabled);
}

static void SaveOption(LPCTSTR value, bool enabled)
{
  CKey key;
  key.Create(HKEY_CURRENT_USER, kCU_FMPath);
  key.SetValue(value, enabled);
}

static bool Read7ZipOption(LPCTSTR value, bool defaultValue)
{
  CKey key;
  if (key.Open(HKEY_CURRENT_USER, kCUBasePath, KEY_READ) == ERROR_SUCCESS)
  {
    bool enabled;
    if (key.QueryValue(value, enabled) == ERROR_SUCCESS)
      return enabled;
  }
  return defaultValue;
}

static bool ReadFMOption(LPCTSTR value)
{
  CKey key;
  bool enabled = false;
  if (key.Open(HKEY_CURRENT_USER, kCU_FMPath, KEY_READ) == ERROR_SUCCESS)
  {
    if (key.QueryValue(value, enabled) == ERROR_SUCCESS)
      return enabled;
  }
  return enabled;
}

static void ReadOption(CKey &key, LPCTSTR value, bool &dest)
{
  bool enabled = false;
  if (key.QueryValue(value, enabled) == ERROR_SUCCESS)
    dest = enabled;
}

/*
static void SaveLmOption(LPCTSTR value, bool enabled)
{
  CKey key;
  key.Create(HKEY_LOCAL_MACHINE, kLM_Path);
  key.SetValue(value, enabled);
}

static bool ReadLmOption(LPCTSTR value, bool defaultValue)
{
  CKey key;
  if (key.Open(HKEY_LOCAL_MACHINE, kLM_Path, KEY_READ) == ERROR_SUCCESS)
  {
    bool enabled;
    if (key.QueryValue(value, enabled) == ERROR_SUCCESS)
      return enabled;
  }
  return defaultValue;
}
*/

void CFmSettings::Save() const
{
  SaveOption(kShowDots, ShowDots);
  SaveOption(kShowRealFileIcons, ShowRealFileIcons);
  SaveOption(kFullRow, FullRow);
  SaveOption(kShowGrid, ShowGrid);
  SaveOption(kSingleClick, SingleClick);
  SaveOption(kAlternativeSelection, AlternativeSelection);
  SaveOption(kArcHistory, ArcHistory);
  SaveOption(kPathHistory, PathHistory);
  SaveOption(kCopyHistory, CopyHistory);
  SaveOption(kFolderHistory, FolderHistory);
  SaveOption(kLowercaseHashes, LowercaseHashes);
  // SaveOption(kUnderline, Underline);

  SaveOption(kShowSystemMenu, ShowSystemMenu);
  // **************** NanaZip Modification Start ****************
  SaveOption(kShowFileSizeUnits, ShowFileSizeUnits);
  // **************** NanaZip Modification End ****************
}

void CFmSettings::Load()
{
  ShowDots = false;
  ShowRealFileIcons = false;
  FullRow = false;
  ShowGrid = false;
  SingleClick = false;
  AlternativeSelection = false;
  ArcHistory = false;
  PathHistory = false;
  CopyHistory = false;
  FolderHistory = false;
  LowercaseHashes = false;
  // Underline = false;

  ShowSystemMenu = false;
  // **************** NanaZip Modification Start ****************
  ShowFileSizeUnits = false;
  // **************** NanaZip Modification End ****************

  CKey key;
  if (key.Open(HKEY_CURRENT_USER, kCU_FMPath, KEY_READ) == ERROR_SUCCESS)
  {
    ReadOption(key, kShowDots, ShowDots);
    ReadOption(key, kShowRealFileIcons, ShowRealFileIcons);
    ReadOption(key, kFullRow, FullRow);
    ReadOption(key, kShowGrid, ShowGrid);
    ReadOption(key, kSingleClick, SingleClick);
    ReadOption(key, kAlternativeSelection, AlternativeSelection);
    ReadOption(key, kArcHistory, ArcHistory);
    ReadOption(key, kPathHistory, PathHistory);
    ReadOption(key, kCopyHistory, CopyHistory);
    ReadOption(key, kFolderHistory, FolderHistory);
    ReadOption(key, kLowercaseHashes, LowercaseHashes);
    // ReadOption(key, kUnderline, Underline);

    ReadOption(key, kShowSystemMenu, ShowSystemMenu );

    // **************** NanaZip Modification Start ****************
    ReadOption(key, kShowFileSizeUnits, ShowFileSizeUnits);
    // **************** NanaZip Modification End ****************
  }
}


// void SaveLockMemoryAdd(bool enable) { SaveLmOption(kLockMemoryAdd, enable); }
// bool ReadLockMemoryAdd() { return ReadLmOption(kLockMemoryAdd, true); }

void SaveLockMemoryEnable(bool enable) { Save7ZipOption(kLargePages, enable); }
bool ReadLockMemoryEnable() { return Read7ZipOption(kLargePages, false); }

bool WantArcHistory() { return ReadFMOption(kArcHistory); }
bool WantPathHistory() { return ReadFMOption(kPathHistory); }
bool WantCopyHistory() { return ReadFMOption(kCopyHistory); }
bool WantFolderHistory() { return ReadFMOption(kFolderHistory); }
bool WantLowercaseHashes() { return ReadFMOption(kLowercaseHashes); }

static CSysString GetFlatViewName(UInt32 panelIndex)
{
  TCHAR panelString[16];
  ConvertUInt32ToString(panelIndex, panelString);
  return (CSysString)kFlatViewName + panelString;
}

void SaveFlatView(UInt32 panelIndex, bool enable) { SaveOption(GetFlatViewName(panelIndex), enable); }

bool ReadFlatView(UInt32 panelIndex)
{
  bool enabled = false;
  CKey key;
  if (key.Open(HKEY_CURRENT_USER, kCU_FMPath, KEY_READ) == ERROR_SUCCESS)
    ReadOption(key, GetFlatViewName(panelIndex), enabled);
  return enabled;
}

/*
void Save_ShowDeleted(bool enable) { SaveOption(kShowDeletedFiles, enable); }
bool Read_ShowDeleted() { return ReadOption(kShowDeletedFiles, false); }
*/
