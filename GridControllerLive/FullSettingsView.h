// FullSettingsView.h
//
// Embeddable "all parameters" tree + details editor - no longer owns its
// own top-level window. The host window creates this module's controls
// inside a region of itself, then forwards WM_COMMAND and WM_NOTIFY
// messages to it; the module ignores anything that isn't one of its own
// control IDs.

#pragma once

#include <windows.h>
#include "ArenaApi.h"

namespace FullSettingsView
{
    // Creates the search box, tree, and details/editor controls as
    // children of hParent, laid out within the given rectangle (x, y,
    // width, height, all in hParent's client coordinates). Also builds
    // the full parameter tree from pNodeMap immediately.
    void CreateControls(HWND hParent, GenApi::INodeMap* pNodeMap, int x, int y, int width, int height);

    // Swaps in a new node map (e.g. after a reconnect), rebuilds the
    // parameter tree from it and re-applies the current search filter.
    void SetNodeMap(GenApi::INodeMap* pNodeMap);

    // Forward every WM_COMMAND from the host window's WndProc here.
    // Returns true if this module handled it (i.e. the control ID
    // belonged to it).
    bool HandleCommand(WPARAM wParam, LPARAM lParam);

    // Forward every WM_NOTIFY from the host window's WndProc here.
    // Returns true if this module handled it.
    bool HandleNotify(LPARAM lParam);

    // Re-reads the currently selected node's value/access mode and
    // refreshes the details panel - call this if something external
    // (e.g. Live View starting/stopping) might have changed what's
    // writable, similar to the old WM_ACTIVATE refresh.
    void RefreshSelectedNode();
}