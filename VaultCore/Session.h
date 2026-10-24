/*
 * PROJECT:    NanaZip Vault Core Library
 * FILE:       Session.h
 * PURPOSE:    Definition for the vault session
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: Kaesinol (keiplyer#gmail.com)
 */

#ifndef NANAZIP_VAULT_SESSION
#define NANAZIP_VAULT_SESSION

#include <Windows.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "Keystream.h"
#include "Settings.h"

namespace NanaZip::Vault
{
    // The vault's unlocked state and the timers that guard it. Locking
    // wipes the keystream key, leaving old buffers permanently unreadable.
    class Session
    {
    public:
        // Posted by the monitor thread; wParam is one of the values below.
        static constexpr UINT NotifyMessage = WM_APP + 0x1000;
        static constexpr WPARAM NotifyLocked = 1;
        static constexpr WPARAM NotifyClipboardCleared = 2;
        // The wipe was due and the clipboard still held our copy, but it
        // could not be opened to empty it: the secret is out there.
        static constexpr WPARAM NotifyClipboardClearFailed = 3;

        // Copies the settings, so an edit in the registry only reaches the
        // next session.
        Session(_In_ VaultSettings const& Settings);
        ~Session();

        // Launches the monitor thread. Calling it again does nothing.
        void Start();

        // Installs a fresh key, keeping old sessions' buffers unreadable;
        // reports false, still locked, when key generation fails.
        bool Unlock();

        // Locks and wipes the keystream master key.
        void Lock();

        bool IsUnlocked() const;

        // Pushes the idle deadline back; the dialog calls it on user input.
        void OnActivity();

        // Swaps settings of a running session. Deadlines already recorded
        // were computed from the old values and are not rearmed.
        void Reconfigure(_In_ VaultSettings const& Settings);

        // Arms the post-reveal wipe; one call per reveal restarts it.
        // Captures the sequence so a user-replaced clipboard is left alone.
        void ScheduleClipboardClear();

        // Wipes the clipboard only if it still holds our secret; a
        // replaced one is skipped without a notify.
        void ClearClipboardNow();

        // Every open dialog that can show a secret registers here, and must
        // unregister before its window goes away.
        void AddNotifyWindow(_In_ HWND Window);
        void RemoveNotifyWindow(_In_ HWND Window);

    private:
        void MonitorLoop();
        void NotifyAll(_In_ WPARAM Reason);

        VaultSettings m_Settings;
        mutable std::mutex m_Mutex;
        bool m_Unlocked = false;
        bool m_Running = false;
        bool m_Stop = false;
        bool m_ClipboardPending = false;
        std::chrono::steady_clock::time_point m_UnlockAt;
        std::chrono::steady_clock::time_point m_LastActivity;
        std::chrono::steady_clock::time_point m_ClipboardAt;
        DWORD m_ClipboardSequence = 0;
        std::vector<HWND> m_NotifyWindows;
        std::thread m_Thread;
        std::condition_variable m_Condition;
    };
}

#endif // !NANAZIP_VAULT_SESSION
