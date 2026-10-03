// MyLoadMenu.h

#ifndef __MY_LOAD_MENU_H
#define __MY_LOAD_MENU_H

void OnMenuActivating(HWND hWnd, HMENU hMenu, int position);
// void OnMenuUnActivating(HWND hWnd, HMENU hMenu, int id);
// void OnMenuUnActivating(HWND hWnd);

bool OnMenuCommand(HWND hWnd, unsigned id);
void MyLoadMenu();

struct CFileMenu
{
  bool programMenu;
  bool readOnly;
  bool isHashFolder;
  bool isFsFolder;
  bool allAreFiles;
  bool isAltStreamsSupported;
  int numItems;
  
  FString FilePath;

  CFileMenu():
      programMenu(false),
      readOnly(false),
      isHashFolder(false),
      isFsFolder(false),
      allAreFiles(false),
      isAltStreamsSupported(true),
      numItems(0)
    {}

  void Load(HMENU hMenu, unsigned startPos);
};

bool ExecuteFileCommand(unsigned id);

// **************** NanaZip Modification Start ****************
/* Maps a Code page submenu command id to a code page number. Returns false if
   the id is not in the submenu, so the caller can fall through to the rest of
   the first party commands. codePage 0 means "no cp property at all". */
bool CodePageFromMenuID(unsigned id, unsigned &codePage);
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
/* Code page number for a Code page submenu index, or the sentinel below when the
   index is out of range. Shared with the menu builder in PanelMenu.cpp. */
#define kCodePageNone ((unsigned)(-1))
unsigned GetCodePageForMenuIndex(unsigned index);
// **************** NanaZip Modification End ****************

#endif
