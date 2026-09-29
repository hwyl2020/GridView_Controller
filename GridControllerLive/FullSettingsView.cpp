// FullSettingsView.cpp

#include "FullSettingsView.h"
#include "ArenaParameterGrid.h"
#include "LiveView.h"
#include <commctrl.h>
#include <windowsx.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <cwctype>

#pragma comment(lib, "comctl32.lib")

using namespace ArenaParamGrid;

namespace FullSettingsView
{
    namespace
    {
        std::wstring Utf8ToWide(const std::string& s)
        {
            if (s.empty()) return std::wstring();
            int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
            std::wstring result(size, 0);
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &result[0], size);
            return result;
        }

        std::string WideToUtf8(const std::wstring& s)
        {
            if (s.empty()) return std::string();
            int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
            std::string result(size, 0);
            WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &result[0], size, nullptr, nullptr);
            return result;
        }

        std::wstring ToLowerW(std::wstring s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
            return s;
        }

        HWND g_hParent = nullptr;
        GenApi::INodeMap* g_pNodeMap = nullptr;
        HWND g_hTree = nullptr;
        ParameterNodePtr g_root;

        enum
        {
            IDC_EDIT_SEARCH = 4001,
            IDC_TREE,
            IDC_STATIC_NAME,
            IDC_EDIT_DESCRIPTION,
            IDC_STATIC_TYPE,
            IDC_STATIC_RANGE,
            IDC_EDIT_VALUE,
            IDC_BTN_APPLY_VALUE,
            IDC_COMBO_VALUE,
            IDC_CHECK_VALUE,
            IDC_BTN_EXECUTE
        };

        void ShowError(const std::string& msg)
        {
            MessageBoxW(g_hParent, Utf8ToWide(msg).c_str(), L"Camera Setting", MB_OK | MB_ICONWARNING);
        }

        ParameterNode* GetSelectedNode()
        {
            HTREEITEM hSel = TreeView_GetSelection(g_hTree);
            if (!hSel) return nullptr;

            TVITEMW item = {};
            item.mask = TVIF_PARAM;
            item.hItem = hSel;
            TreeView_GetItem(g_hTree, &item);
            return reinterpret_cast<ParameterNode*>(item.lParam);
        }

        void UpdateSelectedTreeItemLabel(ParameterNode* node)
        {
            HTREEITEM hSel = TreeView_GetSelection(g_hTree);
            if (!hSel || !node) return;

            std::wstring label = Utf8ToWide(node->displayName);
            if (node->type != ParamType::Category && !node->valueAsString.empty())
                label += L"  (" + Utf8ToWide(node->valueAsString) + L")";

            TVITEMW item = {};
            item.mask = TVIF_TEXT;
            item.hItem = hSel;
            item.pszText = const_cast<LPWSTR>(label.c_str());
            TreeView_SetItem(g_hTree, &item);
        }

        void HideAllValueControls()
        {
            ShowWindow(GetDlgItem(g_hParent, IDC_EDIT_VALUE), SW_HIDE);
            ShowWindow(GetDlgItem(g_hParent, IDC_BTN_APPLY_VALUE), SW_HIDE);
            ShowWindow(GetDlgItem(g_hParent, IDC_COMBO_VALUE), SW_HIDE);
            ShowWindow(GetDlgItem(g_hParent, IDC_CHECK_VALUE), SW_HIDE);
            ShowWindow(GetDlgItem(g_hParent, IDC_BTN_EXECUTE), SW_HIDE);
        }

        void UpdateDetailsPanel()
        {
            ParameterNode* node = GetSelectedNode();

            HWND hName = GetDlgItem(g_hParent, IDC_STATIC_NAME);
            HWND hDesc = GetDlgItem(g_hParent, IDC_EDIT_DESCRIPTION);
            HWND hType = GetDlgItem(g_hParent, IDC_STATIC_TYPE);
            HWND hRange = GetDlgItem(g_hParent, IDC_STATIC_RANGE);

            if (!node)
            {
                SetWindowTextW(hName, L"");
                SetWindowTextW(hDesc, L"");
                SetWindowTextW(hType, L"");
                SetWindowTextW(hRange, L"");
                HideAllValueControls();
                return;
            }

            RefreshNode(g_pNodeMap, *node);
            UpdateSelectedTreeItemLabel(node);

            SetWindowTextW(hName, Utf8ToWide(node->displayName).c_str());
            SetWindowTextW(hDesc, Utf8ToWide(node->description).c_str());

            HideAllValueControls();

            switch (node->type)
            {
            case ParamType::Category:
                SetWindowTextW(hType, L"Category");
                SetWindowTextW(hRange, L"");
                break;

            case ParamType::Integer:
            case ParamType::Float:
            {
                SetWindowTextW(hType, node->type == ParamType::Integer ? L"Integer" : L"Float");
                wchar_t rangeBuf[128];
                swprintf_s(rangeBuf, L"Min: %g   Max: %g", node->minValue, node->maxValue);
                SetWindowTextW(hRange, rangeBuf);

                HWND hEdit = GetDlgItem(g_hParent, IDC_EDIT_VALUE);
                SetWindowTextW(hEdit, Utf8ToWide(node->valueAsString).c_str());
                EnableWindow(hEdit, node->isWritable);
                EnableWindow(GetDlgItem(g_hParent, IDC_BTN_APPLY_VALUE), node->isWritable);
                ShowWindow(hEdit, SW_SHOW);
                ShowWindow(GetDlgItem(g_hParent, IDC_BTN_APPLY_VALUE), SW_SHOW);
                break;
            }

            case ParamType::String:
            {
                SetWindowTextW(hType, L"String");
                SetWindowTextW(hRange, L"");

                HWND hEdit = GetDlgItem(g_hParent, IDC_EDIT_VALUE);
                SetWindowTextW(hEdit, Utf8ToWide(node->valueAsString).c_str());
                EnableWindow(hEdit, node->isWritable);
                EnableWindow(GetDlgItem(g_hParent, IDC_BTN_APPLY_VALUE), node->isWritable);
                ShowWindow(hEdit, SW_SHOW);
                ShowWindow(GetDlgItem(g_hParent, IDC_BTN_APPLY_VALUE), SW_SHOW);
                break;
            }

            case ParamType::Boolean:
            {
                SetWindowTextW(hType, L"Boolean");
                SetWindowTextW(hRange, L"");

                HWND hCheck = GetDlgItem(g_hParent, IDC_CHECK_VALUE);
                Button_SetCheck(hCheck, node->valueAsString == "true" ? BST_CHECKED : BST_UNCHECKED);
                EnableWindow(hCheck, node->isWritable);
                ShowWindow(hCheck, SW_SHOW);
                break;
            }

            case ParamType::Enumeration:
            {
                SetWindowTextW(hType, L"Enumeration");
                SetWindowTextW(hRange, L"");

                HWND hCombo = GetDlgItem(g_hParent, IDC_COMBO_VALUE);
                SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
                int selectIndex = -1;
                for (size_t i = 0; i < node->enumOptions.size(); ++i)
                {
                    std::wstring w = Utf8ToWide(node->enumOptions[i].symbolic);
                    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)w.c_str());
                    if (node->enumOptions[i].symbolic == node->valueAsString)
                        selectIndex = static_cast<int>(i);
                }
                if (selectIndex >= 0)
                    SendMessageW(hCombo, CB_SETCURSEL, selectIndex, 0);
                EnableWindow(hCombo, node->isWritable);
                ShowWindow(hCombo, SW_SHOW);
                break;
            }

            case ParamType::Command:
            {
                SetWindowTextW(hType, L"Command");
                SetWindowTextW(hRange, L"");

                HWND hBtn = GetDlgItem(g_hParent, IDC_BTN_EXECUTE);
                EnableWindow(hBtn, node->isAvailable);
                ShowWindow(hBtn, SW_SHOW);
                break;
            }

            default:
                SetWindowTextW(hType, L"Unknown");
                SetWindowTextW(hRange, L"");
                break;
            }
        }

        void ApplyValueToSelectedNode(const std::string& value)
        {
            ParameterNode* node = GetSelectedNode();
            if (!node) return;

            LiveView::PauseForSettingsChange();

            std::string errorMsg;
            if (!SetParameterValue(g_pNodeMap, node->name, value, errorMsg))
                ShowError("Failed to set '" + node->displayName + "':\n" + errorMsg);

            LiveView::ResumeAfterSettingsChange();

            UpdateDetailsPanel();
        }

        HTREEITEM InsertNodeFiltered(HTREEITEM hParent, const ParameterNodePtr& node,
            const std::wstring& searchTextLower)
        {
            bool isCategory = (node->type == ParamType::Category);

            std::wstring label = Utf8ToWide(node->displayName);
            if (!isCategory && !node->valueAsString.empty())
                label += L"  (" + Utf8ToWide(node->valueAsString) + L")";

            TVINSERTSTRUCTW tvis = {};
            tvis.hParent = hParent;
            tvis.hInsertAfter = TVI_LAST;
            tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
            tvis.item.pszText = const_cast<LPWSTR>(label.c_str());
            tvis.item.lParam = reinterpret_cast<LPARAM>(node.get());
            HTREEITEM hItem = TreeView_InsertItem(g_hTree, &tvis);

            int insertedChildCount = 0;
            if (isCategory)
            {
                for (auto& child : node->children)
                {
                    if (InsertNodeFiltered(hItem, child, searchTextLower))
                        insertedChildCount++;
                }
            }

            bool keep;
            if (isCategory)
            {
                keep = (insertedChildCount > 0);
            }
            else if (searchTextLower.empty())
            {
                keep = true;
            }
            else
            {
                std::wstring nameLower = ToLowerW(Utf8ToWide(node->name));
                std::wstring dispLower = ToLowerW(Utf8ToWide(node->displayName));
                keep = (nameLower.find(searchTextLower) != std::wstring::npos) ||
                    (dispLower.find(searchTextLower) != std::wstring::npos);
            }

            if (!keep)
            {
                TreeView_DeleteItem(g_hTree, hItem);
                return nullptr;
            }
            return hItem;
        }

        void RebuildTree(const std::wstring& searchText)
        {
            TreeView_DeleteAllItems(g_hTree);
            if (!g_root) return;

            std::wstring searchLower = ToLowerW(searchText);
            for (auto& topLevel : g_root->children)
                InsertNodeFiltered(nullptr, topLevel, searchLower);

            UpdateDetailsPanel();
        }

        // Called from the host WndProc, so never let an SDK exception escape.
        ParameterNodePtr BuildRootSafe()
        {
            if (!g_pNodeMap) return nullptr;
            try { return BuildParameterTree(g_pNodeMap, /*maxVisibility*/ 2); }
            catch (const GenICam::GenericException&) { return nullptr; }
            catch (const std::exception&) { return nullptr; }
        }
    } // anonymous namespace

    void CreateControls(HWND hParent, GenApi::INodeMap* pNodeMap, int x, int y, int width, int height)
    {
        g_hParent = hParent;
        g_pNodeMap = pNodeMap;

        int searchH = 24;
        int treeH = (height - searchH - 10) * 3 / 5;
        int detailsY = y + searchH + 10 + treeH + 10;

        CreateWindowW(L"STATIC", L"Search:", WS_CHILD | WS_VISIBLE,
            x, y + 2, 50, 20, hParent, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            x + 55, y, width - 55, searchH, hParent, (HMENU)IDC_EDIT_SEARCH, nullptr, nullptr);

        g_hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            x, y + searchH + 10, width, treeH, hParent, (HMENU)IDC_TREE, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Name:", WS_CHILD | WS_VISIBLE,
            x, detailsY, 45, 18, hParent, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            x + 50, detailsY, width - 50, 18, hParent, (HMENU)IDC_STATIC_NAME, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Description:", WS_CHILD | WS_VISIBLE,
            x, detailsY + 22, 100, 18, hParent, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            x, detailsY + 42, width, 55, hParent, (HMENU)IDC_EDIT_DESCRIPTION, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            x, detailsY + 102, width, 18, hParent, (HMENU)IDC_STATIC_TYPE, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            x, detailsY + 122, width, 18, hParent, (HMENU)IDC_STATIC_RANGE, nullptr, nullptr);

        int valueY = detailsY + 148;
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            x, valueY, width - 70, 24, hParent, (HMENU)IDC_EDIT_VALUE, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Apply", WS_CHILD | BS_PUSHBUTTON,
            x + width - 65, valueY, 65, 26, hParent, (HMENU)IDC_BTN_APPLY_VALUE, nullptr, nullptr);
        CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            x, valueY, width, 220, hParent, (HMENU)IDC_COMBO_VALUE, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Enabled", WS_CHILD | BS_AUTOCHECKBOX,
            x, valueY, 150, 24, hParent, (HMENU)IDC_CHECK_VALUE, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Execute", WS_CHILD | BS_PUSHBUTTON,
            x, valueY, 120, 28, hParent, (HMENU)IDC_BTN_EXECUTE, nullptr, nullptr);

        // No node map when the initial connect failed - leave the tree empty
        // until SetNodeMap is called after a successful reconnect.
        g_root = BuildRootSafe();
        RebuildTree(L"");
    }

    void SetNodeMap(GenApi::INodeMap* pNodeMap)
    {
        // Clear the tree first - its items hold raw pointers into the old g_root.
        TreeView_DeleteAllItems(g_hTree);
        g_pNodeMap = pNodeMap;
        g_root = BuildRootSafe();

        wchar_t buf[256] = {};
        GetWindowTextW(GetDlgItem(g_hParent, IDC_EDIT_SEARCH), buf, 256);
        RebuildTree(buf);
    }

    bool HandleCommand(WPARAM wParam, LPARAM lParam)
    {
        (void)lParam;
        WORD id = LOWORD(wParam);
        WORD notify = HIWORD(wParam);

        if (id == IDC_EDIT_SEARCH && notify == EN_CHANGE)
        {
            wchar_t buf[256] = {};
            GetWindowTextW(GetDlgItem(g_hParent, IDC_EDIT_SEARCH), buf, 256);
            RebuildTree(buf);
            return true;
        }
        if (id == IDC_BTN_APPLY_VALUE && notify == BN_CLICKED)
        {
            wchar_t buf[256] = {};
            GetWindowTextW(GetDlgItem(g_hParent, IDC_EDIT_VALUE), buf, 256);
            ApplyValueToSelectedNode(WideToUtf8(buf));
            return true;
        }
        if (id == IDC_COMBO_VALUE && notify == CBN_SELCHANGE)
        {
            HWND hCombo = GetDlgItem(g_hParent, IDC_COMBO_VALUE);
            int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0)
            {
                wchar_t buf[128] = {};
                SendMessageW(hCombo, CB_GETLBTEXT, sel, (LPARAM)buf);
                ApplyValueToSelectedNode(WideToUtf8(buf));
            }
            return true;
        }
        if (id == IDC_CHECK_VALUE && notify == BN_CLICKED)
        {
            bool checked = Button_GetCheck(GetDlgItem(g_hParent, IDC_CHECK_VALUE)) == BST_CHECKED;
            ApplyValueToSelectedNode(checked ? "true" : "false");
            return true;
        }
        if (id == IDC_BTN_EXECUTE && notify == BN_CLICKED)
        {
            ApplyValueToSelectedNode("");
            return true;
        }
        return false;
    }

    bool HandleNotify(LPARAM lParam)
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->hwndFrom == g_hTree && pnmh->code == TVN_SELCHANGEDW)
        {
            UpdateDetailsPanel();
            return true;
        }
        return false;
    }

    void RefreshSelectedNode()
    {
        if (g_hTree)
            UpdateDetailsPanel();
    }
}