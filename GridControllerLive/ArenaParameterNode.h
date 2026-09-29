// ArenaParameterNode.h
//
// Framework-agnostic data model representing a single GenICam/Arena SDK
// camera parameter (or category). A tree of these is built once from the
// camera's node map and can then be bound to ANY UI: MFC CMFCPropertyGridCtrl,
// Qt QTreeWidget/QTableWidget, WinForms PropertyGrid via C++/CLI, a raw
// Win32 ListView/TreeView, or even printed to a console/log.
//
// No GUI headers are included here on purpose - this file has zero
// dependency on any windowing toolkit.

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace ArenaParamGrid
{
    // Mirrors GenApi's EInterfaceType, simplified for grid display purposes.
    enum class ParamType
    {
        Category,
        Integer,
        Float,
        Boolean,
        Enumeration,
        String,
        Command,
        Unknown
    };

    struct EnumOption
    {
        std::string symbolic;   // e.g. "Mono8"
        int64_t     value = 0;  // underlying integer value
    };

    struct ParameterNode
    {
        // --- Identity ---
        std::string name;          // GenICam node name, e.g. "ExposureTime"
        std::string displayName;   // Human-readable label, e.g. "Exposure Time"
        std::string description;  // Tooltip / description text
        std::string category;      // Immediate parent category name (for flat views/filtering)

        ParamType type = ParamType::Unknown;

        // --- State (re-evaluate these before every read/write; they can
        //     change dynamically, e.g. while the camera is streaming) ---
        bool isAvailable = false;
        bool isReadable  = false;
        bool isWritable  = false;

        // --- Value ---
        // Always populated as a string for easy grid binding, regardless
        // of underlying type. Use 'type' to pick the right editor widget.
        std::string valueAsString;

        // --- Numeric constraints (Integer / Float only) ---
        double minValue   = 0.0;
        double maxValue   = 0.0;
        double increment  = 0.0;

        // --- Enumeration options (Enumeration only) ---
        std::vector<EnumOption> enumOptions;

        // --- Tree structure (populated for Category nodes) ---
        std::vector<std::shared_ptr<ParameterNode>> children;
    };

    using ParameterNodePtr = std::shared_ptr<ParameterNode>;
}
