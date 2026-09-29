// ArenaParameterGrid.h
//
// Core engine: builds a ParameterNode tree from an Arena SDK / GenICam
// node map, refreshes individual nodes, applies edits back to the camera,
// and provides simple text search/filtering over the tree.
//
// This file DOES depend on Arena SDK / GenApi headers, but NOT on any
// GUI toolkit. Link this against ArenaSDK; bind ArenaParameterNode
// structures to whatever widget your application uses.

#pragma once

#include "ArenaParameterNode.h"
#include <GenApi/GenApi.h>   // GenApi::INodeMap, INode, etc. (ships with Arena SDK)

namespace ArenaParamGrid
{
    // Builds the full parameter tree starting at the node map's "Root" node.
    // Call this once after the device is opened, and again any time you
    // want to fully rebuild the tree (e.g. after loading a UserSet).
    //
    //   pNodeMap        - typically pDevice->GetNodeMap() for camera/device
    //                      features. Can also be pDevice->GetTLStreamNodeMap()
    //                      or GetTLDeviceNodeMap() if you want those trees too.
    //   maxVisibility   - 0 = Beginner, 1 = Expert, 2 = Guru. Pass 2 to see
    //                      everything (matches "show all" in most vendor UIs).
    ParameterNodePtr BuildParameterTree(GenApi::INodeMap* pNodeMap, int maxVisibility = 2);

    // Re-reads a single node's current value + access mode (isReadable/
    // isWritable/isAvailable) from the camera without rebuilding the whole
    // tree. Call this:
    //   - right before displaying a row's current value in the grid
    //   - right before allowing an edit (access mode can change dynamically,
    //     e.g. camera starts streaming and Width/Height become read-only)
    //   - after any SetParameterValue call that might affect sibling nodes
    //     (e.g. changing ExposureAuto affects ExposureTime's writability)
    void RefreshNode(GenApi::INodeMap* pNodeMap, ParameterNode& node);

    // Recursively refreshes an entire subtree (convenience wrapper around
    // RefreshNode). Use sparingly - it's O(number of nodes); prefer
    // RefreshNode for single-row updates on selection/edit.
    void RefreshTree(GenApi::INodeMap* pNodeMap, ParameterNodePtr node);

    // Applies a new value to a named parameter on the camera.
    //   nodeName  - GenICam feature name, e.g. "ExposureTime"
    //   newValue  - textual value from your grid's editor:
    //                 Integer/Float : numeric string, e.g. "5000.0"
    //                 Boolean       : "true"/"false" or "1"/"0"
    //                 Enumeration   : the symbolic name, e.g. "Mono8"
    //                 String        : the literal string
    //                 Command       : value is ignored, node is executed
    //   errorMsg  - filled in on failure with a human-readable reason,
    //               suitable for showing in a message box / status bar
    // Returns true on success, false on failure (errorMsg explains why -
    // e.g. "not currently writable", "value out of range", camera busy, etc.)
    bool SetParameterValue(GenApi::INodeMap* pNodeMap,
                            const std::string& nodeName,
                            const std::string& newValue,
                            std::string& errorMsg);

    // Builds a single named node (not its whole subtree) - useful when you
    // only want to expose a curated subset of parameters (e.g. just
    // Exposure/Gain/PixelFormat for a prototype) instead of the full tree.
    // Returns nullptr if the node doesn't exist on this device.
    ParameterNodePtr BuildSingleParameterNode(GenApi::INodeMap* pNodeMap, const std::string& nodeName);

    // Case-insensitive search over name / displayName / (optionally)
    // description. Returns pointers into the SAME tree (not copies) for
    // every leaf or category node whose text matches - handy for
    // implementing the "Search" box from the reference UI.
    //   searchDescriptionToo - also match against node->description
    std::vector<ParameterNodePtr> FindMatches(const ParameterNodePtr& root,
                                               const std::string& searchText,
                                               bool searchDescriptionToo = false,
                                               bool matchWholeWord = false,
                                               bool matchCase = false);
}
