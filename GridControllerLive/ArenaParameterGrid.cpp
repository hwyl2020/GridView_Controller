// ArenaParameterGrid.cpp
//
// See ArenaParameterGrid.h for usage notes.

#include "ArenaParameterGrid.h"
#include <GenApi/GenApi.h>
#include <algorithm>
#include <cctype>

using namespace GenApi;

namespace ArenaParamGrid
{
    namespace
    {
        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        int VisibilityRank(EVisibility v)
        {
            switch (v)
            {
                case Beginner: return 0;
                case Expert:   return 1;
                case Guru:     return 2;
                default:       return 2; // Invisible/undefined -> treat as most restrictive; caller can still opt in
            }
        }

        // Fills in type-specific fields (value, min/max, enum options) for a
        // single already-identified node. Category/Command have no "value".
        void PopulateTypedFields(INode* pNode, ParameterNode& out)
        {
            if (!IsReadable(pNode) && out.type != ParamType::Command && out.type != ParamType::Category)
                return; // leave valueAsString empty if we can't read it right now

            switch (out.type)
            {
                case ParamType::Integer:
                {
                    CIntegerPtr p(pNode);
                    if (IsReadable(p))
                    {
                        out.valueAsString = std::to_string(p->GetValue());
                        out.minValue  = static_cast<double>(p->GetMin());
                        out.maxValue  = static_cast<double>(p->GetMax());
                        out.increment = static_cast<double>(p->GetInc());
                    }
                    break;
                }
                case ParamType::Float:
                {
                    CFloatPtr p(pNode);
                    if (IsReadable(p))
                    {
                        out.valueAsString = std::to_string(p->GetValue());
                        out.minValue = p->GetMin();
                        out.maxValue = p->GetMax();
                    }
                    break;
                }
                case ParamType::Boolean:
                {
                    CBooleanPtr p(pNode);
                    if (IsReadable(p))
                        out.valueAsString = p->GetValue() ? "true" : "false";
                    break;
                }
                case ParamType::Enumeration:
                {
                    CEnumerationPtr p(pNode);
                    if (IsReadable(p))
                    {
                        CEnumEntryPtr pCurrent = p->GetCurrentEntry();
                        if (IsReadable(pCurrent))
                            out.valueAsString = pCurrent->GetSymbolic().c_str();
                    }
                    NodeList_t entries;
                    p->GetEntries(entries);
                    for (auto& pEntryNode : entries)
                    {
                        CEnumEntryPtr pEntry(pEntryNode);
                        if (IsAvailable(pEntry))
                        {
                            EnumOption opt;
                            opt.symbolic = pEntry->GetSymbolic().c_str();
                            opt.value = pEntry->GetValue();
                            out.enumOptions.push_back(opt);
                        }
                    }
                    break;
                }
                case ParamType::String:
                {
                    CStringPtr p(pNode);
                    if (IsReadable(p))
                        out.valueAsString = p->GetValue().c_str();
                    break;
                }
                case ParamType::Command:
                    // Commands have no persistent "value" to show.
                    break;
                default:
                    break;
            }
        }

        ParamType MapInterfaceType(EInterfaceType t)
        {
            switch (t)
            {
                case intfICategory:    return ParamType::Category;
                case intfIInteger:     return ParamType::Integer;
                case intfIFloat:       return ParamType::Float;
                case intfIBoolean:     return ParamType::Boolean;
                case intfIEnumeration: return ParamType::Enumeration;
                case intfIString:      return ParamType::String;
                case intfICommand:     return ParamType::Command;
                default:               return ParamType::Unknown;
            }
        }

        ParameterNodePtr BuildNode(INode* pNode, const std::string& parentCategory, int maxVisibility)
        {
            auto out = std::make_shared<ParameterNode>();
            out->name        = pNode->GetName().c_str();
            out->displayName = pNode->GetDisplayName().c_str();
            out->description = pNode->GetDescription().c_str();
            out->category    = parentCategory;

            out->isAvailable = IsAvailable(pNode);
            out->isReadable  = IsReadable(pNode);
            out->isWritable  = IsWritable(pNode);
            out->type        = MapInterfaceType(pNode->GetPrincipalInterfaceType());

            if (out->type == ParamType::Category)
            {
                CCategoryPtr pCategory(pNode);
                FeatureList_t features;
                pCategory->GetFeatures(features);

                for (auto& pFeature : features)
                {
                    INode* pChildNode = pFeature->GetNode();
                    if (!pChildNode)
                        continue;

                    // Skip nodes deeper/more advanced than requested (mirrors
                    // the Beginner/Expert/Guru visibility filter in vendor UIs).
                    if (VisibilityRank(pChildNode->GetVisibility()) > maxVisibility)
                        continue;

                    // Skip features that aren't implemented on this camera at all.
                    if (!IsImplemented(pChildNode))
                        continue;

                    out->children.push_back(BuildNode(pChildNode, out->name, maxVisibility));
                }
            }
            else
            {
                PopulateTypedFields(pNode, *out);
            }

            return out;
        }

        // Case/whole-word aware substring match.
        bool TextMatches(const std::string& haystackIn, const std::string& needleIn,
                          bool matchWholeWord, bool matchCase)
        {
            std::string haystack = matchCase ? haystackIn : ToLower(haystackIn);
            std::string needle   = matchCase ? needleIn   : ToLower(needleIn);

            if (matchWholeWord)
                return haystack == needle;

            return haystack.find(needle) != std::string::npos;
        }

        void CollectMatches(const ParameterNodePtr& node, const std::string& searchText,
                             bool searchDescriptionToo, bool matchWholeWord, bool matchCase,
                             std::vector<ParameterNodePtr>& results)
        {
            if (!node) return;

            bool matches =
                TextMatches(node->name, searchText, matchWholeWord, matchCase) ||
                TextMatches(node->displayName, searchText, matchWholeWord, matchCase) ||
                (searchDescriptionToo && TextMatches(node->description, searchText, false, matchCase));

            if (matches)
                results.push_back(node);

            for (auto& child : node->children)
                CollectMatches(child, searchText, searchDescriptionToo, matchWholeWord, matchCase, results);
        }
    } // anonymous namespace

    ParameterNodePtr BuildParameterTree(INodeMap* pNodeMap, int maxVisibility)
    {
        if (!pNodeMap)
            return nullptr;

        INode* pRoot = pNodeMap->GetNode("Root");
        if (!pRoot)
            return nullptr;

        return BuildNode(pRoot, /*parentCategory*/ "", maxVisibility);
    }

    ParameterNodePtr BuildSingleParameterNode(INodeMap* pNodeMap, const std::string& nodeName)
    {
        INode* pNode = pNodeMap->GetNode(nodeName.c_str());
        if (!pNode || !IsImplemented(pNode))
            return nullptr;

        // maxVisibility=2 (Guru) so we never silently skip the node itself;
        // it only matters for recursing into categories, which a single
        // leaf node build never does.
        return BuildNode(pNode, /*parentCategory*/ "", /*maxVisibility*/ 2);
    }

    void RefreshNode(INodeMap* pNodeMap, ParameterNode& node)
    {
        INode* pNode = pNodeMap->GetNode(node.name.c_str());
        if (!pNode)
        {
            node.isAvailable = false;
            node.isReadable  = false;
            node.isWritable  = false;
            return;
        }

        node.isAvailable = IsAvailable(pNode);
        node.isReadable  = IsReadable(pNode);
        node.isWritable  = IsWritable(pNode);

        if (node.type != ParamType::Category)
        {
            node.enumOptions.clear();
            PopulateTypedFields(pNode, node);
        }
    }

    void RefreshTree(INodeMap* pNodeMap, ParameterNodePtr node)
    {
        if (!node) return;
        RefreshNode(pNodeMap, *node);
        for (auto& child : node->children)
            RefreshTree(pNodeMap, child);
    }

    bool SetParameterValue(INodeMap* pNodeMap, const std::string& nodeName,
                            const std::string& newValue, std::string& errorMsg)
    {
        try
        {
            INode* pNode = pNodeMap->GetNode(nodeName.c_str());
            if (!pNode)
            {
                errorMsg = "Parameter '" + nodeName + "' was not found on this device.";
                return false;
            }

            if (!IsAvailable(pNode))
            {
                errorMsg = "'" + nodeName + "' is not available in the camera's current mode.";
                return false;
            }

            if (!IsWritable(pNode) && pNode->GetPrincipalInterfaceType() != intfICommand)
            {
                errorMsg = "'" + nodeName + "' is currently read-only "
                           "(often because the camera is streaming, or a related "
                           "parameter, e.g. an *Auto mode, must be turned off first).";
                return false;
            }

            switch (pNode->GetPrincipalInterfaceType())
            {
                case intfIInteger:
                {
                    CIntegerPtr p(pNode);
                    p->SetValue(std::stoll(newValue));
                    break;
                }
                case intfIFloat:
                {
                    CFloatPtr p(pNode);
                    p->SetValue(std::stod(newValue));
                    break;
                }
                case intfIBoolean:
                {
                    CBooleanPtr p(pNode);
                    p->SetValue(newValue == "true" || newValue == "1");
                    break;
                }
                case intfIEnumeration:
                {
                    CEnumerationPtr p(pNode);
                    p->FromString(newValue.c_str());
                    break;
                }
                case intfIString:
                {
                    CStringPtr p(pNode);
                    p->SetValue(newValue.c_str());
                    break;
                }
                case intfICommand:
                {
                    CCommandPtr p(pNode);
                    p->Execute();
                    break;
                }
                default:
                    errorMsg = "'" + nodeName + "' has an unsupported parameter type.";
                    return false;
            }

            return true;
        }
        catch (const GenICam::GenericException& e)
        {
            // GenICam range/type validation errors land here - message is
            // already human-readable (e.g. "Value 999999 is out of range").
            errorMsg = e.GetDescription();
            return false;
        }
        catch (const std::exception& e)
        {
            errorMsg = e.what();
            return false;
        }
    }

    std::vector<ParameterNodePtr> FindMatches(const ParameterNodePtr& root,
                                               const std::string& searchText,
                                               bool searchDescriptionToo,
                                               bool matchWholeWord,
                                               bool matchCase)
    {
        std::vector<ParameterNodePtr> results;
        if (searchText.empty())
            return results;

        CollectMatches(root, searchText, searchDescriptionToo, matchWholeWord, matchCase, results);
        return results;
    }
}
