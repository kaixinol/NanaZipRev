/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       Session.cpp
 * PURPOSE:    Implementation for the vault session
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#include "Session.h"

#include <algorithm>

namespace
{
    // Waking once a day when idle keeps stopping the thread responsive.
    constexpr std::chrono::hours g_IdleWait(24);

    // windows.h defines min as a macro, clobbering the standard one.
    std::chrono::steady_clock::time_point Earlier(
        std::chrono::steady_clock::time_point const& Left,
        std::chrono::steady_clock::time_point const& Right)
    {
        return (Left < Right) ? Left : Right;
    }
}

NanaZip::Vault::Session::Session(_In_ VaultSettings const& Settings)
    : m_Settings(Settings)
{
}

NanaZip::Vault::Session::~Session()
{
    {
        std::lock_guard<std::mutex> Guard(this->m_Mutex);
        this->m_Stop = true;
    }
    this->m_Condition.notify_all();
    if (this->m_Thread.joinable())
    {
        this->m_Thread.join();
    }
    // The session may end unlocked; the key must not outlive it.
    Keystream::Erase();
}

void NanaZip::Vault::Session::Start()
{
    std::lock_guard<std::mutex> Guard(this->m_Mutex);
    if (this->m_Running)
    {
        return;
    }
    this->m_Running = true;
    this->m_Stop = false;
    this->m_Thread = std::thread([this]() { this->MonitorLoop(); });
}

bool NanaZip::Vault::Session::Unlock()
{
    // A fresh key keeps old sessions' buffers unreadable. The key must
    // exist before the session reports unlocked.
    if (!Keystream::Install())
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> Guard(this->m_Mutex);
        this->m_Unlocked = true;
        this->m_UnlockAt = std::chrono::steady_clock::now();
        this->m_LastActivity = this->m_UnlockAt;
    }
    this->m_Condition.notify_all();
    return true;
}

void NanaZip::Vault::Session::Lock()
{
    bool Wipe = false;
    {
        std::lock_guard<std::mutex> Guard(this->m_Mutex);
        if (!this->m_Unlocked)
        {
            return;
        }
        this->m_Unlocked = false;
        if (this->m_ClipboardPending)
        {
            // Locking is the deadline the user chose for every secret this
            // session revealed, so a pending wipe is pulled forward here.
            this->m_ClipboardAt = std::chrono::steady_clock::now();
            Wipe = true;
        }
    }
    // Erasing the key makes every scrambled buffer permanently unreadable.
    Keystream::Erase();
    NotifyAll(NotifyLocked);
    if (Wipe)
    {
        ClearClipboardNow();
    }
}

bool NanaZip::Vault::Session::IsUnlocked() const
{
    std::lock_guard<std::mutex> Guard(this->m_Mutex);
    return this->m_Unlocked;
}

void NanaZip::Vault::Session::OnActivity()
{
    std::lock_guard<std::mutex> Guard(this->m_Mutex);
    if (this->m_Unlocked)
    {
        this->m_LastActivity = std::chrono::steady_clock::now();
    }
}

void NanaZip::Vault::Session::Reconfigure(
    _In_ NanaZip::Vault::VaultSettings const& Settings)
{
    {
        std::lock_guard<std::mutex> Guard(this->m_Mutex);
        this->m_Settings = Settings;
    }
    // Wake the loop from a deadline computed with the old timers.
    this->m_Condition.notify_all();
}

void NanaZip::Vault::Session::ScheduleClipboardClear()
{
    {
        std::lock_guard<std::mutex> Guard(this->m_Mutex);
        this->m_ClipboardPending = true;
        this->m_ClipboardAt = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(this->m_Settings.ClipboardClearMs);
    }
    this->m_Condition.notify_all();
}

void NanaZip::Vault::Session::ClearClipboardNow()
{
    // Opening the clipboard here beats posting to the UI thread,
    // which can stall behind a pumped message.
    bool Cleared = false;
    if (::OpenClipboard(nullptr))
    {
        ::EmptyClipboard();
        ::CloseClipboard();
        Cleared = true;
    }
    {
        std::lock_guard<std::mutex> Guard(this->m_Mutex);
        m_ClipboardPending = false;
    }
    // A refused open leaves the secret where it is, which is not the
    // report the user needs after a copy.
    NotifyAll(Cleared
        ? NotifyClipboardCleared
        : NotifyClipboardClearFailed);
}

void NanaZip::Vault::Session::AddNotifyWindow(_In_ HWND Window)
{
    if (!Window)
    {
        return;
    }
    std::lock_guard<std::mutex> Guard(this->m_Mutex);
    if (std::find(
        this->m_NotifyWindows.begin(),
        this->m_NotifyWindows.end(),
        Window) == this->m_NotifyWindows.end())
    {
        this->m_NotifyWindows.push_back(Window);
    }
}

void NanaZip::Vault::Session::RemoveNotifyWindow(_In_ HWND Window)
{
    std::lock_guard<std::mutex> Guard(this->m_Mutex);
    this->m_NotifyWindows.erase(
        std::remove(
            this->m_NotifyWindows.begin(),
            this->m_NotifyWindows.end(),
            Window),
        this->m_NotifyWindows.end());
}

void NanaZip::Vault::Session::NotifyAll(_In_ WPARAM Reason)
{
    // Copied under the lock: posting is done without holding it, and a
    // dialog that unregisters during the post is not waited on.
    std::vector<HWND> Windows;
    {
        std::lock_guard<std::mutex> Guard(this->m_Mutex);
        Windows = this->m_NotifyWindows;
    }
    for (HWND Window : Windows)
    {
        if (Window)
        {
            ::PostMessageW(Window, NotifyMessage, Reason, 0);
        }
    }
}

void NanaZip::Vault::Session::MonitorLoop()
{
    while (true)
    {
        std::unique_lock<std::mutex> Guard(this->m_Mutex);

        if (this->m_Stop)
        {
            return;
        }

        std::chrono::steady_clock::time_point Now =
            std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point Next = Now + g_IdleWait;

        if (this->m_Unlocked)
        {
            // Zero disables the timer: the vault stays unlocked until
            // the process exits, and the key dies with it.
            if (this->m_Settings.AbsoluteTimeoutMs != 0)
            {
                Next = Earlier(
                    Next,
                    this->m_UnlockAt + std::chrono::milliseconds(
                        this->m_Settings.AbsoluteTimeoutMs));
            }
            if (this->m_Settings.IdleTimeoutMs != 0)
            {
                Next = Earlier(
                    Next,
                    this->m_LastActivity + std::chrono::milliseconds(
                        this->m_Settings.IdleTimeoutMs));
            }
        }
        if (this->m_ClipboardPending)
        {
            Next = Earlier(Next, this->m_ClipboardAt);
        }

        if (Next > Now)
        {
            this->m_Condition.wait_until(Guard, Next);
            continue;
        }

        bool LockDue = false;
        if (this->m_Unlocked)
        {
            LockDue = (this->m_Settings.AbsoluteTimeoutMs != 0
                && Now >= this->m_UnlockAt + std::chrono::milliseconds(
                    this->m_Settings.AbsoluteTimeoutMs))
                || (this->m_Settings.IdleTimeoutMs != 0
                    && Now >= this->m_LastActivity
                        + std::chrono::milliseconds(
                            this->m_Settings.IdleTimeoutMs));
        }
        bool ClipboardDue =
            this->m_ClipboardPending && Now >= this->m_ClipboardAt;

        Guard.unlock();

        if (LockDue)
        {
            Lock();
        }
        if (ClipboardDue)
        {
            ClearClipboardNow();
        }
    }
}
