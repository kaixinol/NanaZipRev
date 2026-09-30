/*
 * PROJECT:    NanaZip Platform Base Library (K7Base)
 * FILE:       K7BaseFormat.cpp
 * PURPOSE:    Implementation for NanaZip Platform Base Formatting
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: MouriNaruto (Kenji.Mouri@outlook.com)
 */

#include "K7BasePrivate.h"

#include <stdio.h>

EXTERN_C VOID WINAPI K7BaseConvertByteSizeToString(
    _In_ UINT64 ByteSize,
    _Out_writes_z_(TextBufferSize) PWSTR TextBuffer,
    _In_ UINT32 TextBufferSize)
{
    if (!TextBuffer || !TextBufferSize)
    {
        return;
    }

    PCWSTR const Units[] =
    {
        L"Byte",
        L"Bytes",
        L"KiB",
        L"MiB",
        L"GiB",
        L"TiB",
        L"PiB",
        L"EiB"
    };
    UINT32 const UnitsCount = ARRAYSIZE(Units);

    // Output Format:
    // For ByteSize is 0 or 1: x Byte
    // For ByteSize is from 2 to 1023: x Bytes
    // For ByteSize is larger than 1023: x.xx {KiB, MiB, GiB, TiB, PiB, EiB}

    UINT32 UnitIndex = 0;
    double Result = static_cast<double>(ByteSize);

    if (1 < ByteSize)
    {
        for (UnitIndex = 1; UnitIndex < UnitsCount; ++UnitIndex)
        {
            if (1024.0 > Result)
            {
                break;
            }

            Result /= 1024.0;
        }

        // Keep two digits after the decimal point.
        Result = static_cast<UINT64>(Result * 100) / 100.0;
    }

    ::_snwprintf_s(
        TextBuffer,
        TextBufferSize,
        _TRUNCATE,
        (1 < UnitIndex) ? L"%.2f %s" : L"%.0f %s",
        Result,
        Units[UnitIndex]);
}
