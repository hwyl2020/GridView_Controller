// prototype_main.cpp
//
// Single consolidated window: live video on the left, all controls
// (Start/Stop stream, curated quick settings, and the full searchable
// parameter tree) in a panel on the right.

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>

#include "ArenaApi.h"
#include "ArenaParameterGrid.h"
#include "LiveView.h"
#include "FullSettingsView.h"

#pragma comment(lib, "comctl32.lib")

using namespace ArenaParamGrid;

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

    // -------------------------------------------------------------
    // Globals
    // -------------------------------------------------------------
    Arena::ISystem* g_pSystem = nullptr;
    Arena::IDevice* g_pDevice = nullptr;
    GenApi::INodeMap* g_pNodeMap = nullptr;

    ParameterNodePtr g_exposureNode;
    ParameterNodePtr g_exposureAutoNode;
    ParameterNodePtr g_gainNode;
    ParameterNodePtr g_pixelFormatNode;
    std::string g_gainNodeName = "Gain";

    HWND g_hMainWnd = nullptr;
    RECT g_videoRect = {};

    enum
    {
        IDC_BTN_START_STREAM = 1001,
        IDC_BTN_STOP_STREAM,
        IDC_BTN_RECONNECT,

        IDC_EDIT_EXPOSURE = 2001,
        IDC_SLIDER_EXPOSURE,
        IDC_COMBO_EXPOSUREAUTO,
        IDC_EDIT_GAIN,
        IDC_SLIDER_GAIN,
        IDC_COMBO_PIXELFORMAT
    };

    void LogLine(const std::string& s) { std::cout << s << std::endl; }

    // valueAsString is empty when the node isn't readable - fall back
    // instead of letting std::stod throw.
    double ParseDoubleOr(const std::string& s, double fallback)
    {
        if (s.empty()) return fallback;
        try { return std::stod(s); }
        catch (const std::exception&) { return fallback; }
    }

    void ShowError(const std::string& msg)
    {
        MessageBoxW(g_hMainWnd, Utf8ToWide(msg).c_str(), L"Camera Setting", MB_OK | MB_ICONWARNING);
    }

    // -------------------------------------------------------------
    // Camera connection
    // -------------------------------------------------------------
    bool ConnectToFirstCamera(std::string& errorMsg)
    {
        try
        {
            g_pSystem = Arena::OpenSystem();
            g_pSystem->UpdateDevices(2000);

            std::vector<Arena::DeviceInfo> deviceInfos = g_pSystem->GetDevices();
            if (deviceInfos.empty())
            {
                errorMsg =
                    "No LUCID cameras found.\n\n"
                    "For GigE cameras, check:\n"
                    " - Camera and PC NIC are on the same subnet\n"
                    " - Windows Firewall isn't blocking discovery\n"
                    " - The camera shows up in ArenaView / IPConfigUtility";
                return false;
            }

            g_pDevice = g_pSystem->CreateDevice(deviceInfos[0]);
            g_pNodeMap = g_pDevice->GetNodeMap();
            LogLine("Connected to: " + std::string(deviceInfos[0].ModelName()));
            return true;
        }
        catch (const GenICam::GenericException& e)
        {
            errorMsg = std::string("Arena SDK error while connecting: ") + e.GetDescription();
            return false;
        }
        catch (const std::exception& e)
        {
            errorMsg = std::string("Error while connecting: ") + e.what();
            return false;
        }
    }

    void DisconnectCamera()
    {
        if (g_pSystem && g_pDevice)
        {
            g_pSystem->DestroyDevice(g_pDevice);
            g_pDevice = nullptr;
            g_pNodeMap = nullptr;
        }
        if (g_pSystem)
        {
            Arena::CloseSystem(g_pSystem);
            g_pSystem = nullptr;
        }
    }

    std::string ResolveGainNodeName(GenApi::INodeMap* pNodeMap)
    {
        GenApi::INode* pSelectorNode = pNodeMap->GetNode("GainSelector");
        if (pSelectorNode && GenApi::IsImplemented(pSelectorNode) && GenApi::IsWritable(pSelectorNode))
        {
            try
            {
                GenApi::CEnumerationPtr pSelector(pSelectorNode);
                GenApi::NodeList_t entries;
                pSelector->GetEntries(entries);
                for (auto& pEntryNode : entries)
                {
                    GenApi::CEnumEntryPtr pEntry(pEntryNode);
                    if (GenApi::IsAvailable(pEntry) &&
                        std::string(pEntry->GetSymbolic().c_str()) == "All")
                    {
                        pSelector->FromString("All");
                        break;
                    }
                }
            }
            catch (...) {}
        }
        return "Gain";
    }

    void BuildCuratedParameters()
    {
        g_exposureNode = BuildSingleParameterNode(g_pNodeMap, "ExposureTime");
        g_exposureAutoNode = BuildSingleParameterNode(g_pNodeMap, "ExposureAuto");
        g_pixelFormatNode = BuildSingleParameterNode(g_pNodeMap, "PixelFormat");
        g_gainNodeName = ResolveGainNodeName(g_pNodeMap);
        g_gainNode = BuildSingleParameterNode(g_pNodeMap, g_gainNodeName);
    }

    // -------------------------------------------------------------
    // Curated quick-settings controls
    // -------------------------------------------------------------
    void PopulateExposureControls()
    {
        if (!g_exposureNode) return;
        RefreshNode(g_pNodeMap, *g_exposureNode);

        HWND hEdit = GetDlgItem(g_hMainWnd, IDC_EDIT_EXPOSURE);
        HWND hSlider = GetDlgItem(g_hMainWnd, IDC_SLIDER_EXPOSURE);

        SetWindowTextW(hEdit, Utf8ToWide(g_exposureNode->valueAsString).c_str());

        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        double range = g_exposureNode->maxValue - g_exposureNode->minValue;
        double current = ParseDoubleOr(g_exposureNode->valueAsString, g_exposureNode->minValue);
        int pos = range > 0 ? static_cast<int>(((current - g_exposureNode->minValue) / range) * 1000.0) : 0;
        SendMessageW(hSlider, TBM_SETPOS, TRUE, pos);

        EnableWindow(hEdit, g_exposureNode->isWritable);
        EnableWindow(hSlider, g_exposureNode->isWritable);
    }

    void PopulateExposureAutoControl()
    {
        if (!g_exposureAutoNode) return;
        RefreshNode(g_pNodeMap, *g_exposureAutoNode);

        HWND hCombo = GetDlgItem(g_hMainWnd, IDC_COMBO_EXPOSUREAUTO);
        SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

        int selectIndex = -1;
        for (size_t i = 0; i < g_exposureAutoNode->enumOptions.size(); ++i)
        {
            const auto& opt = g_exposureAutoNode->enumOptions[i];
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)Utf8ToWide(opt.symbolic).c_str());
            if (opt.symbolic == g_exposureAutoNode->valueAsString)
                selectIndex = static_cast<int>(i);
        }
        if (selectIndex >= 0)
            SendMessageW(hCombo, CB_SETCURSEL, selectIndex, 0);

        EnableWindow(hCombo, g_exposureAutoNode->isWritable);
    }

    void PopulateGainControls()
    {
        if (!g_gainNode) return;
        RefreshNode(g_pNodeMap, *g_gainNode);

        HWND hEdit = GetDlgItem(g_hMainWnd, IDC_EDIT_GAIN);
        HWND hSlider = GetDlgItem(g_hMainWnd, IDC_SLIDER_GAIN);

        SetWindowTextW(hEdit, Utf8ToWide(g_gainNode->valueAsString).c_str());

        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        double range = g_gainNode->maxValue - g_gainNode->minValue;
        double current = ParseDoubleOr(g_gainNode->valueAsString, g_gainNode->minValue);
        int pos = range > 0 ? static_cast<int>(((current - g_gainNode->minValue) / range) * 1000.0) : 0;
        SendMessageW(hSlider, TBM_SETPOS, TRUE, pos);

        EnableWindow(hEdit, g_gainNode->isWritable);
        EnableWindow(hSlider, g_gainNode->isWritable);
    }

    void PopulatePixelFormatControl()
    {
        if (!g_pixelFormatNode) return;
        RefreshNode(g_pNodeMap, *g_pixelFormatNode);

        HWND hCombo = GetDlgItem(g_hMainWnd, IDC_COMBO_PIXELFORMAT);
        SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

        int selectIndex = -1;
        for (size_t i = 0; i < g_pixelFormatNode->enumOptions.size(); ++i)
        {
            const auto& opt = g_pixelFormatNode->enumOptions[i];
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)Utf8ToWide(opt.symbolic).c_str());
            if (opt.symbolic == g_pixelFormatNode->valueAsString)
                selectIndex = static_cast<int>(i);
        }
        if (selectIndex >= 0)
            SendMessageW(hCombo, CB_SETCURSEL, selectIndex, 0);

        EnableWindow(hCombo, g_pixelFormatNode->isWritable);
    }

    void RefreshAllCuratedControls()
    {
        PopulateExposureControls();
        PopulateExposureAutoControl();
        PopulateGainControls();
        PopulatePixelFormatControl();
    }

    std::string GetEditTextUtf8(HWND hEdit)
    {
        wchar_t buf[64] = {};
        GetWindowTextW(hEdit, buf, 64);
        return WideToUtf8(buf);
    }

    void ApplyExposureFromEdit()
    {
        std::string value = GetEditTextUtf8(GetDlgItem(g_hMainWnd, IDC_EDIT_EXPOSURE));
        std::string errorMsg;
        if (!SetParameterValue(g_pNodeMap, "ExposureTime", value, errorMsg))
            ShowError("Failed to set Exposure Time:\n" + errorMsg);
        PopulateExposureControls();
    }

    void ApplyExposureAutoFromCombo()
    {
        HWND hCombo = GetDlgItem(g_hMainWnd, IDC_COMBO_EXPOSUREAUTO);
        int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
        if (sel < 0) return;
        wchar_t buf[64] = {};
        SendMessageW(hCombo, CB_GETLBTEXT, sel, (LPARAM)buf);

        std::string errorMsg;
        if (!SetParameterValue(g_pNodeMap, "ExposureAuto", WideToUtf8(buf), errorMsg))
            ShowError("Failed to set Exposure Auto:\n" + errorMsg);

        PopulateExposureAutoControl();
        PopulateExposureControls();
    }

    void ApplyGainFromEdit()
    {
        std::string value = GetEditTextUtf8(GetDlgItem(g_hMainWnd, IDC_EDIT_GAIN));
        std::string errorMsg;
        if (!SetParameterValue(g_pNodeMap, g_gainNodeName, value, errorMsg))
            ShowError("Failed to set Gain:\n" + errorMsg);
        PopulateGainControls();
    }

    void ApplyExposureFromSlider(int pos)
    {
        if (!g_exposureNode) return;
        double range = g_exposureNode->maxValue - g_exposureNode->minValue;
        double newVal = g_exposureNode->minValue + (pos / 1000.0) * range;
        std::ostringstream oss; oss << newVal;

        std::string errorMsg;
        if (!SetParameterValue(g_pNodeMap, "ExposureTime", oss.str(), errorMsg))
            ShowError("Failed to set Exposure Time:\n" + errorMsg);
        PopulateExposureControls();
    }

    void ApplyGainFromSlider(int pos)
    {
        if (!g_gainNode) return;
        double range = g_gainNode->maxValue - g_gainNode->minValue;
        double newVal = g_gainNode->minValue + (pos / 1000.0) * range;
        std::ostringstream oss; oss << newVal;

        std::string errorMsg;
        if (!SetParameterValue(g_pNodeMap, g_gainNodeName, oss.str(), errorMsg))
            ShowError("Failed to set Gain:\n" + errorMsg);
        PopulateGainControls();
    }

    void ApplyPixelFormatFromCombo()
    {
        HWND hCombo = GetDlgItem(g_hMainWnd, IDC_COMBO_PIXELFORMAT);
        int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
        if (sel < 0) return;
        wchar_t buf[128] = {};
        SendMessageW(hCombo, CB_GETLBTEXT, sel, (LPARAM)buf);

        LiveView::PauseForSettingsChange();
        std::string errorMsg;
        if (!SetParameterValue(g_pNodeMap, "PixelFormat", WideToUtf8(buf), errorMsg))
            ShowError("Failed to set Pixel Format:\n" + errorMsg);
        LiveView::ResumeAfterSettingsChange();

        PopulatePixelFormatControl();
    }

    // -------------------------------------------------------------
    // Layout constants
    // -------------------------------------------------------------
    const int VIDEO_X = 10, VIDEO_Y = 10, VIDEO_W = 870, VIDEO_H = 760;
    const int PANEL_X = 900, PANEL_W = 440;
    const int WINDOW_W = 1360, WINDOW_H = 860;

    void CreateAllControls(HWND hWnd)
    {
        int px = PANEL_X;

        CreateWindowW(L"BUTTON", L"Start Stream", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            px, 10, 210, 32, hWnd, (HMENU)IDC_BTN_START_STREAM, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Stop Stream", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            px + 220, 10, 210, 32, hWnd, (HMENU)IDC_BTN_STOP_STREAM, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"-- Quick Settings --", WS_CHILD | WS_VISIBLE,
            px, 54, 300, 20, hWnd, nullptr, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Exposure Time (us):", WS_CHILD | WS_VISIBLE,
            px, 78, 200, 18, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            px, 98, 130, 24, hWnd, (HMENU)IDC_EDIT_EXPOSURE, nullptr, nullptr);
        CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
            px + 140, 96, 300, 26, hWnd, (HMENU)IDC_SLIDER_EXPOSURE, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Exposure Auto:", WS_CHILD | WS_VISIBLE,
            px, 130, 200, 18, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            px, 150, 180, 100, hWnd, (HMENU)IDC_COMBO_EXPOSUREAUTO, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Gain (dB):", WS_CHILD | WS_VISIBLE,
            px, 184, 200, 18, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            px, 204, 130, 24, hWnd, (HMENU)IDC_EDIT_GAIN, nullptr, nullptr);
        CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
            px + 140, 202, 300, 26, hWnd, (HMENU)IDC_SLIDER_GAIN, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Pixel Format:", WS_CHILD | WS_VISIBLE,
            px, 236, 200, 18, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            px, 256, 300, 220, hWnd, (HMENU)IDC_COMBO_PIXELFORMAT, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"-- All Parameters (search & edit everything) --", WS_CHILD | WS_VISIBLE,
            px, 292, 420, 20, hWnd, nullptr, nullptr, nullptr);

        int fsvY = 316;
        int fsvHeight = WINDOW_H - fsvY - 30;
        FullSettingsView::CreateControls(hWnd, g_pNodeMap, px, fsvY, PANEL_W, fsvHeight);
    }

    // Called from the WndProc, so nothing may escape from here.
    bool InitializeAfterConnect(HWND hWnd, std::string& errorMsg)
    {
        try
        {
            BuildCuratedParameters();
            RefreshAllCuratedControls();

            g_videoRect.left = VIDEO_X;
            g_videoRect.top = VIDEO_Y;
            g_videoRect.right = VIDEO_X + VIDEO_W;
            g_videoRect.bottom = VIDEO_Y + VIDEO_H;

            LiveView::StartStreaming(g_pDevice, hWnd, g_videoRect);
            return true;
        }
        catch (const GenICam::GenericException& e)
        {
            errorMsg = std::string("Arena SDK error while initializing: ") + e.GetDescription();
            return false;
        }
        catch (const std::exception& e)
        {
            errorMsg = std::string("Error while initializing: ") + e.what();
            return false;
        }
    }

    // -------------------------------------------------------------
    // Main window procedure
    // -------------------------------------------------------------
    LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            g_hMainWnd = hWnd;

            std::string errorMsg;
            bool connected = ConnectToFirstCamera(errorMsg);

            CreateAllControls(hWnd);

            if (connected)
            {
                if (!InitializeAfterConnect(hWnd, errorMsg))
                {
                    LogLine(errorMsg);
                    ShowError(errorMsg);
                }
            }
            else
            {
                LogLine("Connection failed: " + errorMsg);
                CreateWindowW(L"BUTTON", L"Reconnect", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    PANEL_X, WINDOW_H - 40, 150, 28, hWnd, (HMENU)IDC_BTN_RECONNECT, nullptr, nullptr);
                MessageBoxW(hWnd, Utf8ToWide(errorMsg).c_str(), L"Connection Failed", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            LiveView::RenderFrame(hdc, g_videoRect);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_COMMAND:
        {
            WORD id = LOWORD(wParam);
            WORD notify = HIWORD(wParam);

            if (id == IDC_BTN_START_STREAM && notify == BN_CLICKED)
            {
                if (g_pDevice) LiveView::StartStreaming(g_pDevice, hWnd, g_videoRect);
                RefreshAllCuratedControls();
                FullSettingsView::RefreshSelectedNode();
            }
            else if (id == IDC_BTN_STOP_STREAM && notify == BN_CLICKED)
            {
                LiveView::StopStreaming();
                RefreshAllCuratedControls();
                FullSettingsView::RefreshSelectedNode();
            }
            else if (id == IDC_BTN_RECONNECT && notify == BN_CLICKED)
            {
                // Release anything left over from the failed attempt (e.g. a
                // system that opened but found no devices) before reopening.
                DisconnectCamera();

                std::string errorMsg;
                if (ConnectToFirstCamera(errorMsg))
                {
                    FullSettingsView::SetNodeMap(g_pNodeMap);
                    // Hide rather than destroy - we're inside the button's own click notification.
                    if (InitializeAfterConnect(hWnd, errorMsg))
                        ShowWindow(GetDlgItem(hWnd, IDC_BTN_RECONNECT), SW_HIDE);
                    else
                        ShowError(errorMsg);
                }
                else
                {
                    ShowError(errorMsg);
                }
            }
            else if (id == IDC_EDIT_EXPOSURE && notify == EN_KILLFOCUS)
                ApplyExposureFromEdit();
            else if (id == IDC_COMBO_EXPOSUREAUTO && notify == CBN_SELCHANGE)
                ApplyExposureAutoFromCombo();
            else if (id == IDC_EDIT_GAIN && notify == EN_KILLFOCUS)
                ApplyGainFromEdit();
            else if (id == IDC_COMBO_PIXELFORMAT && notify == CBN_SELCHANGE)
                ApplyPixelFormatFromCombo();
            else
                FullSettingsView::HandleCommand(wParam, lParam);

            return 0;
        }

        case WM_HSCROLL:
        {
            HWND hSlider = (HWND)lParam;
            int pos = static_cast<int>(SendMessageW(hSlider, TBM_GETPOS, 0, 0));

            if (hSlider == GetDlgItem(hWnd, IDC_SLIDER_EXPOSURE))
                ApplyExposureFromSlider(pos);
            else if (hSlider == GetDlgItem(hWnd, IDC_SLIDER_GAIN))
                ApplyGainFromSlider(pos);

            return 0;
        }

        case WM_NOTIFY:
            FullSettingsView::HandleNotify(lParam);
            return 0;

        case WM_DESTROY:
            LiveView::StopStreaming();
            DisconnectCamera();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"AccuVisionPrototypeMainWindow";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    RECT wr = { 0, 0, WINDOW_W, WINDOW_H };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hMain = CreateWindowW(
        L"AccuVisionPrototypeMainWindow", L"Camera Control",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hMain, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}