// LiveView.h
//
// Windowless streaming engine: starts Arena SDK image acquisition on a
// background thread, converts each frame to BGR8, and exposes a Render()
// call so the HOST window can paint the current frame into whatever
// region it likes. This module no longer owns any window itself - it's
// meant to be embedded into a region of a single consolidated app window.

#pragma once

#include <windows.h>
#include "ArenaApi.h"

namespace LiveView
{
    // Starts streaming from pDevice. hOwnerWnd is the window LiveView
    // will call InvalidateRect() on (in videoRect) whenever a new frame
    // is ready, so the host repaints. Safe to call if already running
    // (no-op).
    void StartStreaming(Arena::IDevice* pDevice, HWND hOwnerWnd, RECT videoRect);

    // Stops streaming and the background grab thread. Safe to call if
    // not running (no-op).
    void StopStreaming();

    bool IsStreaming();

    // Call immediately before changing a stream-format-affecting
    // parameter while streaming might be active. Stops the stream if
    // running and remembers to restart it afterward. Safe to call even
    // if not streaming (no-op).
    void PauseForSettingsChange();

    // Call right after the parameter change attempt completes (success
    // or failure) to resume streaming if PauseForSettingsChange() paused it.
    void ResumeAfterSettingsChange();

    // Paints the current frame (or a blank placeholder if none yet) into
    // hdc, scaled to fit destRect. Call this from the host window's
    // WM_PAINT for the video region.
    void RenderFrame(HDC hdc, const RECT& destRect);

    // Update the region LiveView should invalidate on new frames - call
    // this if the host window resizes and the video area moves.
    void SetVideoRect(RECT videoRect);
}