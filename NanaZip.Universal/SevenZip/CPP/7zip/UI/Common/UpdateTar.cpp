/*
 * PROJECT:    NanaZip
 * FILE:       UpdateTar.cpp
 * PURPOSE:    Implementation for the automatic tar wrapper of single-stream
 *             compression formats
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "StdAfx.h"

#include "../../../Common/ComTry.h"

#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileIO.h"
#include "../../../Windows/Registry.h"

#include "../../Common/FileStreams.h"
#include "../../Common/StreamObjects.h"

#include "Update.h"
#include "UpdateCallback.h"
#include "UpdateTar.h"

namespace
{
    constexpr UInt64 TarBlockSize = 512;
    constexpr UInt64 TarNameFieldSize = 100;
    constexpr UInt64 TarMemoryLimitMin = (UInt64)256 << 20;

    UInt64 AlignToBlock(UInt64 Size)
    {
        return (Size + TarBlockSize - 1) & ~(TarBlockSize - 1);
    }

    // A hint for picking the buffer only, the tar writer is not bounded by it.
    UInt64 EstimateTarSize(CDirItems const& DirItems)
    {
        UInt64 Size = 2 * TarBlockSize;
        FOR_VECTOR (Index, DirItems.Items)
        {
            CDirItem const& Item = DirItems.Items[Index];
            Size += TarBlockSize;
            // Names go out as UTF-8, at most three bytes per UTF-16 unit, and
            // GNU tar appends a null to the ones it stores in a LongName.
            UInt64 NameSize = (UInt64)DirItems.GetLogPath(Index).Len() * 3 + 1;
            if (NameSize > TarNameFieldSize)
            {
                Size += TarBlockSize + AlignToBlock(NameSize);
            }
            if (!Item.IsDir())
            {
                Size += AlignToBlock(Item.Size);
            }
        }
        return Size;
    }

    // The buffer stays alive as the input of the compression stage, so it
    // competes with the compressor for the same physical memory.
    UInt64 GetTarMemoryLimit()
    {
        MEMORYSTATUSEX Status;
        Status.dwLength = sizeof(Status);
        if (!::GlobalMemoryStatusEx(&Status))
        {
            return TarMemoryLimitMin;
        }
        const UInt64 Limit = Status.ullAvailPhys / 4;
        return Limit > TarMemoryLimitMin ? Limit : TarMemoryLimitMin;
    }

    bool ReadTarSwitch(LPCTSTR Name)
    {
        // The console honors both switches, so they live in the application
        // key rather than the file manager one.
        NWindows::NRegistry::CKey Key;
        bool Enabled = false;
        if (ERROR_SUCCESS == Key.Open(
            HKEY_CURRENT_USER,
            TEXT("Software") TEXT(STRING_PATH_SEPARATOR) TEXT("NanaZipRev"),
            KEY_READ))
        {
            Key.GetValue_bool_IfOk(Name, Enabled);
        }
        return Enabled;
    }

    bool IsTarAutoWrapEnabled()
    {
        return ReadTarSwitch(TEXT("TarAutoWrap"));
    }

    bool IsTarMemoryBufferEnabled()
    {
        return ReadTarSwitch(TEXT("TarMemoryBuffer"));
    }
}

bool IsTarWrapFormat(CArcInfoEx const& Format)
{
    if (!IsTarAutoWrapEnabled())
    {
        return false;
    }
    return Format.UpdateEnabled &&
        Format.Flags_KeepName() &&
        !Format.Flags_HashHandler();
}

bool NeedTarWrapping(
    _In_ CCodecs const* Codecs,
    CUpdateOptions const& Options,
    CDirItems const& DirItems,
    bool ThereIsInArchive)
{
    if (ThereIsInArchive || Options.SfxMode)
    {
        return false;
    }
    const int FormatIndex = Options.MethodMode.Type.FormatIndex;
    if (FormatIndex < 0 ||
        !IsTarWrapFormat(Codecs->Formats[(unsigned)FormatIndex]))
    {
        return false;
    }
    return DirItems.Items.Size() != 1 || DirItems.Items[0].IsDir();
}

Z7_CLASS_IMP_COM_2(
    CTarItemUpdateCallback
    , IArchiveUpdateCallback
    , ICompressProgressInfo
)
    Z7_IFACE_COM7_IMP(IProgress)

    UInt64 m_TarSize;
    CMyComPtr<ISequentialInStream> m_TarStream;
    IUpdateCallbackUI* m_UiCallback;

public:
    void Init(
        UInt64 TarSize,
        _In_ ISequentialInStream* TarStream,
        _In_ IUpdateCallbackUI* UiCallback)
    {
        this->m_TarSize = TarSize;
        this->m_TarStream = TarStream;
        this->m_UiCallback = UiCallback;
    }
};

Z7_COM7F_IMF(CTarItemUpdateCallback::GetUpdateItemInfo(
    UInt32 Index,
    _Out_opt_ Int32* NewData,
    _Out_opt_ Int32* NewProps,
    _Out_opt_ UInt32* IndexInArchive))
{
    UNREFERENCED_PARAMETER(Index);
    if (NewData)
    {
        *NewData = 1;
    }
    if (NewProps)
    {
        *NewProps = 1;
    }
    if (IndexInArchive)
    {
        *IndexInArchive = (UInt32)(Int32)-1;
    }
    return S_OK;
}

Z7_COM7F_IMF(CTarItemUpdateCallback::GetProperty(
    UInt32 Index,
    PROPID PropId,
    _Out_ PROPVARIANT* Value))
{
    COM_TRY_BEGIN
    UNREFERENCED_PARAMETER(Index);
    NWindows::NCOM::CPropVariant Prop;
    switch (PropId)
    {
        case kpidSize:
            Prop = this->m_TarSize;
            break;
        case kpidIsDir:
            Prop = false;
            break;
        default:
            break;
    }
    Prop.Detach(Value);
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CTarItemUpdateCallback::GetStream(
    UInt32 Index,
    _Out_ ISequentialInStream** InStream))
{
    UNREFERENCED_PARAMETER(Index);
    *InStream = this->m_TarStream;
    if (*InStream)
    {
        (*InStream)->AddRef();
    }
    return S_OK;
}

Z7_COM7F_IMF(CTarItemUpdateCallback::SetOperationResult(Int32 OperationResult))
{
    return this->m_UiCallback->SetOperationResult(OperationResult);
}

Z7_COM7F_IMF(CTarItemUpdateCallback::SetTotal(UInt64 Total))
{
    return this->m_UiCallback->SetTotal(Total);
}

Z7_COM7F_IMF(CTarItemUpdateCallback::SetCompleted(
    _In_opt_ const UInt64* CompleteValue))
{
    return this->m_UiCallback->SetCompleted(CompleteValue);
}

// The codecs reach CheckBreak() only through SetRatioInfo, so it has to be
// forwarded even though the tar stage already reported the real byte counts.
Z7_COM7F_IMF(CTarItemUpdateCallback::SetRatioInfo(
    _In_opt_ const UInt64* InSize,
    _In_opt_ const UInt64* OutSize))
{
    return this->m_UiCallback->SetRatioInfo(InSize, OutSize);
}

HRESULT UpdateArchiveTarWrapped(
    _In_ CCodecs const* Codecs,
    _In_ IOutArchive* OutArchive,
    _In_ ISequentialOutStream* OutStream,
    UInt32 NumItems,
    _In_ IArchiveUpdateCallback* Callback,
    _In_ IUpdateCallbackUI* UiCallback,
    CDirItems const& DirItems)
{
    COM_TRY_BEGIN

    const int TarFormatIndex = Codecs->FindFormatForArchiveType(UString("tar"));
    if (TarFormatIndex < 0)
    {
        return E_NOTIMPL;
    }

    // Declared first so the temp file is removed after every stream holding it.
    NWindows::NFile::NDir::CTempFile TempFile;
    CMyComPtr<IOutArchive> TarArchive;
    RINOK(Codecs->CreateOutArchive((unsigned)TarFormatIndex, TarArchive))
    if (!TarArchive)
    {
        return E_NOTIMPL;
    }

    CMyComPtr<ISequentialOutStream> TarOutStream;
    CMyComPtr<ISequentialInStream> TarInStream;
    CDynBufSeqOutStream* BufferSpec = nullptr;
    COutFileStream* FileSpec = nullptr;

    if (IsTarMemoryBufferEnabled() &&
        EstimateTarSize(DirItems) <= GetTarMemoryLimit())
    {
        BufferSpec = new CDynBufSeqOutStream;
        TarOutStream = BufferSpec;
    }
    else
    {
        FileSpec = new COutFileStream;
        TarOutStream = FileSpec;
        if (!TempFile.CreateRandomInTempFolder(
            FTEXT("NanaZipTar"), &FileSpec->File))
        {
            return ::GetLastError_noZero_HRESULT();
        }
    }

    RINOK(TarArchive->UpdateItems(TarOutStream, NumItems, Callback))

    UInt64 TarSize = 0;
    if (BufferSpec)
    {
        TarSize = BufferSpec->GetSize();
        CBufInStream* InStreamSpec = new CBufInStream;
        TarInStream = InStreamSpec;
        InStreamSpec->Init(
            BufferSpec->GetBuffer(),
            (size_t)TarSize,
            BufferSpec);
    }
    else
    {
        RINOK(FileSpec->GetSize(&TarSize))
        RINOK(FileSpec->Close())
        CInFileStream* InStreamSpec = new CInFileStream;
        TarInStream = InStreamSpec;
        if (!InStreamSpec->Open(TempFile.GetPath()))
        {
            return ::GetLastError_noZero_HRESULT();
        }
    }

    CTarItemUpdateCallback* WrapperSpec = new CTarItemUpdateCallback;
    CMyComPtr<IArchiveUpdateCallback> Wrapper(WrapperSpec);
    WrapperSpec->Init(TarSize, TarInStream, UiCallback);
    return OutArchive->UpdateItems(OutStream, 1, Wrapper);

    COM_TRY_END
}
