// Liveview.cpp

#include "LiveView.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstring>
#include <cctype>
#include <string>
#include <iostream>

namespace LiveView
{
    namespace
    {
        Arena::IDevice* g_pDevice = nullptr;
        HWND g_hOwnerWnd = nullptr;
        RECT g_videoRect = {};

        std::thread g_grabThread;
        std::atomic<bool> g_running{ false };
        bool g_pausedForSettings = false;

        std::mutex g_frameMutex;
        std::vector<uint8_t> g_frameBuffer; // top-down, 24bpp BGR, row-padded to 4 bytes
        int g_frameWidth = 0;
        int g_frameHeight = 0;

        // Extracts the bit depth from a GenICam pixel format symbolic name
        // via its trailing digits - "BayerRG10" -> 10, "Mono12" -> 12,
        // "BGR8" -> 8, "Mono16" -> 16. Formats with no trailing digits are
        // treated as 8-bit.
        int GetBitDepthFromFormatName(const std::string& name)
        {
            size_t i = name.size();
            while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1])))
                --i;
            if (i == name.size())
                return 8;
            return std::stoi(name.substr(i));
        }

        void GrabThreadProc(Arena::IDevice* pDevice)
        {
            while (g_running.load())
            {
                Arena::IImage* pImage = nullptr;
                Arena::IImage* pBitSelected = nullptr;
                Arena::IImage* pConverted = nullptr;

                try
                {
                    pImage = pDevice->GetImage(500);
                }
                catch (const GenICam::GenericException&)
                {
                    continue; // normal timeout waiting for next frame
                }

                try
                {
                    // Higher-bit-depth raw formats (BayerRG10/12/16,
                    // Mono10/12/16, etc.) need their meaningful 8 bits
                    // selected out before converting to an 8-bit display
                    // format - converting directly produces a "noisy"-
                    // looking image because the wrong bits get read.
                    // See Arena SDK's Cpp_ImageFactory_SelectBitsAndScale
                    // sample for the reference explanation.
                    GenICam::gcstring formatName =
                        GetPixelFormatName(static_cast<PfncFormat>(pImage->GetPixelFormat()));
                    int bitDepth = GetBitDepthFromFormatName(std::string(formatName.c_str()));

                    Arena::IImage* pToConvert = pImage;
                    if (bitDepth > 8)
                    {
                        pBitSelected = Arena::ImageFactory::SelectBitsAndScale(pImage, 8, bitDepth - 8);
                        pToConvert = pBitSelected;
                    }

                    pConverted = Arena::ImageFactory::Convert(pToConvert, BGR8);

                    size_t width = pConverted->GetWidth();
                    size_t height = pConverted->GetHeight();
                    const uint8_t* pData = pConverted->GetData();

                    size_t srcStride = width * 3;
                    size_t dstStride = ((srcStride + 3) / 4) * 4;

                    {
                        std::lock_guard<std::mutex> lock(g_frameMutex);
                        g_frameBuffer.assign(dstStride * height, 0);
                        for (size_t row = 0; row < height; ++row)
                        {
                            std::memcpy(&g_frameBuffer[row * dstStride],
                                pData + row * srcStride,
                                srcStride);
                        }
                        g_frameWidth = static_cast<int>(width);
                        g_frameHeight = static_cast<int>(height);
                    }

                    if (g_hOwnerWnd)
                        InvalidateRect(g_hOwnerWnd, &g_videoRect, FALSE);
                }
                catch (const std::exception& e)
                {
                    std::cout << "LiveView frame error: " << e.what() << std::endl;
                }

                if (pConverted)
                    Arena::ImageFactory::Destroy(pConverted);
                if (pBitSelected)
                    Arena::ImageFactory::Destroy(pBitSelected);
                if (pImage)
                    pDevice->RequeueBuffer(pImage);
            }
        }
    } // anonymous namespace

    void StartStreaming(Arena::IDevice* pDevice, HWND hOwnerWnd, RECT videoRect)
    {
        if (g_running.load())
            return;

        g_pDevice = pDevice;
        g_hOwnerWnd = hOwnerWnd;
        g_videoRect = videoRect;

        try
        {
            Arena::SetNodeValue<GenICam::gcstring>(
                pDevice->GetNodeMap(), "AcquisitionMode", "Continuous");
            Arena::SetNodeValue<GenICam::gcstring>(
                pDevice->GetTLStreamNodeMap(), "StreamBufferHandlingMode", "NewestOnly");
            Arena::SetNodeValue<bool>(
                pDevice->GetTLStreamNodeMap(), "StreamAutoNegotiatePacketSize", true);
            Arena::SetNodeValue<bool>(
                pDevice->GetTLStreamNodeMap(), "StreamPacketResendEnable", true);

            pDevice->StartStream();
        }
        catch (const GenICam::GenericException& e)
        {
            std::cout << "LiveView failed to start stream: " << e.GetDescription() << std::endl;
            return;
        }

        g_running = true;
        g_grabThread = std::thread(GrabThreadProc, pDevice);
    }

    void StopStreaming()
    {
        if (!g_running.load())
            return;

        g_running = false;
        if (g_grabThread.joinable())
            g_grabThread.join();

        try
        {
            g_pDevice->StopStream();
        }
        catch (const GenICam::GenericException& e)
        {
            std::cout << "LiveView failed to stop stream cleanly: " << e.GetDescription() << std::endl;
        }

        if (g_hOwnerWnd)
            InvalidateRect(g_hOwnerWnd, &g_videoRect, FALSE);
    }

    bool IsStreaming()
    {
        return g_running.load();
    }

    void PauseForSettingsChange()
    {
        if (g_running.load())
        {
            g_pausedForSettings = true;
            StopStreaming();
        }
    }

    void ResumeAfterSettingsChange()
    {
        if (g_pausedForSettings)
        {
            g_pausedForSettings = false;
            if (g_pDevice)
                StartStreaming(g_pDevice, g_hOwnerWnd, g_videoRect);
        }
    }

    void RenderFrame(HDC hdc, const RECT& destRect)
    {
        std::lock_guard<std::mutex> lock(g_frameMutex);

        int destW = destRect.right - destRect.left;
        int destH = destRect.bottom - destRect.top;

        if (!g_frameBuffer.empty())
        {
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = g_frameWidth;
            bmi.bmiHeader.biHeight = -g_frameHeight; // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 24;
            bmi.bmiHeader.biCompression = BI_RGB;

            // GDI's default scaling mode does a low-quality, effectively
            // nearest-neighbor downscale, which looks speckled/noisy on
            // detailed images being shrunk to fit the display area.
            // HALFTONE does proper area-averaging and looks dramatically
            // cleaner - SetBrushOrgEx must be called right after per the
            // Win32 API's documented requirement for HALFTONE mode.
            int prevMode = SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, nullptr);

            StretchDIBits(hdc,
                destRect.left, destRect.top, destW, destH,
                0, 0, g_frameWidth, g_frameHeight,
                g_frameBuffer.data(), &bmi,
                DIB_RGB_COLORS, SRCCOPY);

            SetStretchBltMode(hdc, prevMode);
        }
        else
        {
            HBRUSH hBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
            RECT rc = destRect;
            FillRect(hdc, &rc, hBrush);
        }
    }

    void SetVideoRect(RECT videoRect)
    {
        g_videoRect = videoRect;
    }
}