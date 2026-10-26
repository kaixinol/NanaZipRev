// OpenCallback.cpp

#include "StdAfx.h"

#include "../../../Common/ComTry.h"
#include "../../../Common/StringConvert.h"

#include "../../../Windows/FileName.h"
#include "../../../Windows/PropVariant.h"

#include "../../Common/FileStreams.h"

#include "../Common/ZipRegistry.h"

#include "OpenCallback.h"
#include "PasswordDialog.h"

// **************** NanaZip Modification Start ****************
// The vault hooks are declared here and registered by FM.cpp; the settings
// alongside them come from VaultCore.
#ifndef _SFX
#include <string>
#include <VaultCore.h>
#include <VaultHooks.h>
#endif
// **************** NanaZip Modification End ****************

using namespace NWindows;

STDMETHODIMP COpenArchiveCallback::SetTotal(const UInt64 *numFiles, const UInt64 *numBytes)
{
  RINOK(ProgressDialog.Sync.CheckStop());
  {
    // NSynchronization::CCriticalSectionLock lock(_criticalSection);
    ProgressDialog.Sync.Set_NumFilesTotal(numFiles ? *numFiles : (UInt64)(Int64)-1);
    // if (numFiles)
    {
      ProgressDialog.Sync.Set_BytesProgressMode(numFiles == NULL);
    }
    if (numBytes)
      ProgressDialog.Sync.Set_NumBytesTotal(*numBytes);
  }
  return S_OK;
}

STDMETHODIMP COpenArchiveCallback::SetCompleted(const UInt64 *numFiles, const UInt64 *numBytes)
{
  // NSynchronization::CCriticalSectionLock lock(_criticalSection);
  if (numFiles)
    ProgressDialog.Sync.Set_NumFilesCur(*numFiles);
  if (numBytes)
    ProgressDialog.Sync.Set_NumBytesCur(*numBytes);
  return ProgressDialog.Sync.CheckStop();
}

STDMETHODIMP COpenArchiveCallback::SetTotal(const UInt64 total)
{
  RINOK(ProgressDialog.Sync.CheckStop());
  ProgressDialog.Sync.Set_NumBytesTotal(total);
  return S_OK;
}

STDMETHODIMP COpenArchiveCallback::SetCompleted(const UInt64 *completed)
{
  return ProgressDialog.Sync.Set_NumBytesCur(completed);
}

STDMETHODIMP COpenArchiveCallback::GetProperty(PROPID propID, PROPVARIANT *value)
{
  NCOM::CPropVariant prop;
  if (_subArchiveMode)
  {
    switch (propID)
    {
      case kpidName: prop = _subArchiveName; break;
    }
  }
  else
  {
    switch (propID)
    {
      case kpidName:  prop = fs2us(_fileInfo.Name); break;
      case kpidIsDir:  prop = _fileInfo.IsDir(); break;
      case kpidSize:  prop = _fileInfo.Size; break;
      case kpidAttrib:  prop = (UInt32)_fileInfo.Attrib; break;
      case kpidCTime:  prop = _fileInfo.CTime; break;
      case kpidATime:  prop = _fileInfo.ATime; break;
      case kpidMTime:  prop = _fileInfo.MTime; break;
    }
  }
  prop.Detach(value);
  return S_OK;
}

STDMETHODIMP COpenArchiveCallback::GetStream(const wchar_t *name, IInStream **inStream)
{
  COM_TRY_BEGIN
  *inStream = NULL;
  if (_subArchiveMode)
    return S_FALSE;

  FString fullPath;
  if (!NFile::NName::GetFullPath(_folderPrefix, us2fs(name), fullPath))
    return S_FALSE;
  if (!_fileInfo.Find_FollowLink(fullPath))
    return S_FALSE;
  if (_fileInfo.IsDir())
    return S_FALSE;
  CInFileStream *inFile = new CInFileStream;
  CMyComPtr<IInStream> inStreamTemp = inFile;
  if (!inFile->Open(fullPath))
    return ::GetLastError();
  *inStream = inStreamTemp.Detach();
  return S_OK;
  COM_TRY_END
}

STDMETHODIMP COpenArchiveCallback::CryptoGetTextPassword(BSTR *password)
{
  COM_TRY_BEGIN
  PasswordWasAsked = true;
  if (!PasswordIsDefined)
  {
// **************** NanaZip Modification Start ****************
#ifndef _SFX
    // Every hook fingerprints the content itself, so a moved or renamed
    // archive still matches.
    std::wstring ArchivePath;
    std::wstring ArchiveLabel;
    if (!_fileInfo.Name.IsEmpty())
    {
      // Absolute, because the entry shows this path to the user: a
      // relative open would record one that only means something from
      // one directory.
      FString FullPath;
      if (!NFile::NName::GetFullPath(_folderPrefix, _fileInfo.Name,
          FullPath))
        FullPath = _folderPrefix + _fileInfo.Name;
      const UString ArchivePathText = fs2us(FullPath);
      ArchivePath.assign(ArchivePathText.Ptr(), ArchivePathText.Len());
      const UString Base = fs2us(_fileInfo.Name);
      ArchiveLabel.assign(Base.Ptr(), Base.Len());
    }

    // Silent auto-fill first: a matching entry in an already unlocked
    // session opens the archive with no dialog at all.
    if (g_VaultAutoFillHook && !SkipPasswordAutoFill && !ArchivePath.empty())
    {
      std::wstring AutoPwd;
      if (g_VaultAutoFillHook(ArchivePath, AutoPwd))
      {
        Password = AutoPwd.c_str();
        PasswordIsDefined = true;
        // Password holds its own copy; this one must not linger on the stack.
        ::SecureZeroMemory(AutoPwd.data(), AutoPwd.size() * sizeof(wchar_t));
        return StringToBstr(Password, password);
      }
    }
#endif
// **************** NanaZip Modification End ****************

    CPasswordDialog dialog;
    bool showPassword = NExtract::Read_ShowPassword();
    dialog.ShowPassword = showPassword;

// **************** NanaZip Modification Start ****************
#ifndef _SFX
    if (!ArchivePath.empty())
    {
      dialog.ArchivePath = ArchivePath;
      dialog.ArchiveLabel = ArchiveLabel;
      dialog.SaveToVault =
          ::NanaZip::Vault::VaultSettings::Load().SavePasswordByDefault;
    }
#endif
// **************** NanaZip Modification End ****************

    ProgressDialog.WaitCreating();
    if (dialog.Create(ProgressDialog) != IDOK)
      return E_ABORT;

    Password = dialog.Password;
    PasswordIsDefined = true;
    if (dialog.ShowPassword != showPassword)
      NExtract::Save_ShowPassword(dialog.ShowPassword);

// **************** NanaZip Modification Start ****************
#ifndef _SFX
    // Remember the checkbox state for the next dialog. When a match hid the
    // checkbox, the value still holds the stored default, so this is a no-op.
    if (dialog.ShowSaveToVault)
    {
      ::NanaZip::Vault::VaultSettings Settings =
          ::NanaZip::Vault::VaultSettings::Load();
      if (Settings.SavePasswordByDefault != dialog.SaveToVault)
      {
        Settings.SavePasswordByDefault = dialog.SaveToVault;
        Settings.Save();
      }
    }

    // Record the save request without committing it: the password is not
    // verified yet, so the FM layer persists it only after a successful open.
    SavePasswordRequested = dialog.SaveRequested;
    SavePasswordPath = dialog.ArchivePath;
    SavePasswordLabel = dialog.ArchiveLabel;
#endif
// **************** NanaZip Modification End ****************
  }
  return StringToBstr(Password, password);
  COM_TRY_END
}
