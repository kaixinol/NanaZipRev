// AppState.h

#ifndef __APP_STATE_H
#define __APP_STATE_H

#include "../../../Windows/Synchronization.h"

#include "ViewSettings.h"

class CFastFolders
{
  NWindows::NSynchronization::CCriticalSection _criticalSection;
public:
  UStringVector Strings;
  void SetString(unsigned index, const UString &s)
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_criticalSection);
    while (Strings.Size() <= index)
      Strings.AddNew();
    Strings[index] = s;
  }
  UString GetString(unsigned index)
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_criticalSection);
    if (index >= Strings.Size())
      return UString();
    return Strings[index];
  }
  void Save()
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_criticalSection);
    SaveFastFolders(Strings);
  }
  void Read()
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_criticalSection);
    ReadFastFolders(Strings);
  }
};

class CFolderHistory
{
  NWindows::NSynchronization::CCriticalSection _criticalSection;
  UStringVector Strings;

  void Normalize();
  
public:
  
  void GetList(UStringVector &foldersHistory)
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_criticalSection);
    foldersHistory = Strings;
  }
  
  void AddString(const UString &s);

  void RemoveAll()
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_criticalSection);
    Strings.Clear();
  }
  
  void Save()
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_criticalSection);
    SaveFolderHistory(Strings);
  }
  
  void Read()
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_criticalSection);
    ReadFolderHistory(Strings);
    Normalize();
  }
};

struct CAppState
{
  CFastFolders FastFolders;
  CFolderHistory FolderHistory;
  // **************** NanaZip Modification Start ****************
  /* Filename code page the user last chose, or 0 to let the automatic
     detection decide. Seeded from the registry at startup and written back
     when the user picks a code page from the submenu, so a legacy archive opens
     the same way after a restart. */
  unsigned CodePage;
  // **************** NanaZip Modification End ****************

  // **************** NanaZip Modification Start ****************
  CAppState(): CodePage(0) {}
  // **************** NanaZip Modification End ****************

  void Save()
  {
    FastFolders.Save();
    FolderHistory.Save();
  }
  void Read()
  {
    FastFolders.Read();
    FolderHistory.Read();
  }
};

#endif
