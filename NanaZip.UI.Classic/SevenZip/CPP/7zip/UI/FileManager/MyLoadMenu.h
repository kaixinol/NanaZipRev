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
/* Code page number for a Code page submenu index, or the sentinel value below
   when the index is out of range. Shared with the menu builder in
   PanelMenu.cpp. */
#define kCodePageNone ((unsigned)(-1))
unsigned GetCodePageForMenuIndex(unsigned index);
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
/* The formats whose entry names a code page can be applied to. Each of
   them reads a "cp" open property and decodes the name with it: Zip
   probes the name when the page is the automatic one, and Rar4, Tar and
   gzip take a page literally. Rar5 keeps names as UTF-8 by format
   definition and has nothing to apply a code page to.

   This is a list rather than a test for the opposite case on purpose: a
   format left out gets a menu that quietly does nothing, which is worse
   than one that is plainly unavailable. The names are the ones the
   handlers pass to REGISTER_ARC_I. Rar5 is not named here even though it
   claims the .rar extension as well, because a RAR5 archive reaches a
   different handler under a different name.

   The helper is inline because two translation units need it:
   PanelFolderChange.cpp answers whether the submenu can be enabled, and
   PanelMenu.cpp is the one that builds it. */
inline bool FormatAcceptsCodePage(const wchar_t *formatName)
{
  return StringsAreEqualNoCase_Ascii(formatName, "zip")
      || StringsAreEqualNoCase_Ascii(formatName, "rar")
      || StringsAreEqualNoCase_Ascii(formatName, "tar")
      || StringsAreEqualNoCase_Ascii(formatName, "gzip");
}
// **************** NanaZip Modification End ****************

#endif
