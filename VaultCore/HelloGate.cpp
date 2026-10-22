/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       HelloGate.cpp
 * PURPOSE:    Implementation for the Windows Hello gate
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "HelloGate.h"

#include <winrt/base.h>

// The SDK declaration of IUserConsentVerifierInterop, which derives from
// IInspectable: a hand-rolled copy would call through the wrong vtable slot.
#include <UserConsentVerifierInterop.h>

namespace Foundation = winrt::Windows::Foundation;
namespace Credentials = winrt::Windows::Security::Credentials::UI;

namespace
{
    // Queries the activation factory for the interop interface, which is how a
    // desktop window is handed to an API that otherwise only knows UWP ones.
    Foundation::IAsyncOperation<Credentials::UserConsentVerificationResult>
    RequestVerificationForWindowAsync(
        _In_ HWND Window,
        _In_ winrt::hstring const& Message)
    {
        auto Factory = winrt::get_activation_factory<
            Credentials::UserConsentVerifier,
            IUserConsentVerifierInterop>();
        winrt::guid OperationId = winrt::guid_of<Foundation::IAsyncOperation<
            Credentials::UserConsentVerificationResult>>();
        void* Operation = nullptr;
        winrt::check_hresult(Factory->RequestVerificationForWindowAsync(
            Window,
            reinterpret_cast<HSTRING>(winrt::get_abi(Message)),
            OperationId,
            &Operation));
        Foundation::IAsyncOperation<
            Credentials::UserConsentVerificationResult> OperationWrapper(
            Operation,
            winrt::take_ownership_from_abi);
        return OperationWrapper;
    }
}

Foundation::IAsyncOperation<bool> NanaZip::Vault::HelloGate::IsAvailableAsync()
{
    // Mapped here so no caller has to know the availability enumeration.
    Credentials::UserConsentVerifierAvailability Availability =
        co_await Credentials::UserConsentVerifier::CheckAvailabilityAsync();
    co_return Credentials::UserConsentVerifierAvailability::Available
        == Availability;
}

Foundation::IAsyncOperation<bool> NanaZip::Vault::HelloGate::VerifyAsync(
    _In_ HWND Window,
    _In_ std::wstring const& Message)
{
    // Awaiting resumes the caller on its own apartment, so a prompt left open
    // for a minute does not hold the thread that opened it.
    Credentials::UserConsentVerificationResult Result =
        co_await RequestVerificationForWindowAsync(
            Window,
            winrt::hstring(Message));
    co_return Credentials::UserConsentVerificationResult::Verified == Result;
}
