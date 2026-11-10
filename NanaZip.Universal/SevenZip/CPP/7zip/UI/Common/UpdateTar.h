/*
 * PROJECT:    NanaZip
 * FILE:       UpdateTar.h
 * PURPOSE:    Definition for the automatic tar wrapper of single-stream
 *             compression formats
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_COMMON_UPDATETAR
#define NANAZIP_COMMON_UPDATETAR

#include "DirItem.h"
#include "LoadCodecs.h"

struct CUpdateOptions;
struct IUpdateCallbackUI;

// Single-stream codecs accept exactly one item, so any other selection has to
// be collected into a tar before they can compress it.
bool IsTarWrapFormat(CArcInfoEx const& Format);

bool NeedTarWrapping(
    _In_ CCodecs const* Codecs,
    CUpdateOptions const& Options,
    CDirItems const& DirItems,
    bool ThereIsInArchive);

HRESULT UpdateArchiveTarWrapped(
    _In_ CCodecs const* Codecs,
    _In_ IOutArchive* OutArchive,
    _In_ ISequentialOutStream* OutStream,
    UInt32 NumItems,
    _In_ IArchiveUpdateCallback* Callback,
    _In_ IUpdateCallbackUI* UiCallback,
    CDirItems const& DirItems);

#endif // NANAZIP_COMMON_UPDATETAR
