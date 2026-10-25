// FileFolderPluginOpen.h

#ifndef __FILE_FOLDER_PLUGIN_OPEN_H
#define __FILE_FOLDER_PLUGIN_OPEN_H

#include "../../../Windows/DLL.h"

struct CFfpOpen
{
  CLASS_NO_COPY(CFfpOpen)
public:
// **************** NanaZip Modification Start ****************
  // A retry after a wrong password must not reuse the vault's stored one.
  bool SkipPasswordAutoFill;
// **************** NanaZip Modification End ****************
  // out:
  bool Encrypted;
  UString Password;

  NWindows::NDLL::CLibrary Library;
  CMyComPtr<IFolderFolder> Folder;
  UString ErrorMessage;

// **************** NanaZip Modification Start ****************
  // CFfpOpen(): Encrypted (false) {}
  CFfpOpen(): SkipPasswordAutoFill(false), Encrypted (false) {}
// **************** NanaZip Modification End ****************

  HRESULT OpenFileFolderPlugin(IInStream *inStream,
      const FString &path, const UString &arcFormat, HWND parentWindow);
};


#endif
