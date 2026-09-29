# Grid Controller Live

Win32 C++ test application for LUCID Vision Labs GigE cameras: live video on the
left, and on the right Start/Stop Stream, Quick Settings (Exposure Time, Exposure
Auto, Gain, Pixel Format) and a searchable tree of every GenICam camera parameter.

Built on the LUCID **Arena SDK** (C++ API). No MFC/Qt.

## 1. Prerequisites (install once)

| What | Notes |
|---|---|
| Windows 10/11, 64-bit | x64 only - the Arena SDK is 64-bit. |
| **Visual Studio 2022 or 2026** (Community is fine) | Install the **"Desktop development with C++"** workload. Use Visual Studio, not VS Code. |
| **LUCID Arena SDK for Windows** | Download from [thinklucid.com](https://thinklucid.com/downloads-hub/). Developed against Arena SDK 1.0.80.x (GenICam v3.3). The installer sets the `LUCID_DEV_ROOT` and `LUCID_GENICAM_PATH` environment variables and adds the SDK DLL folders to `PATH`. |

After installing the SDK, **restart Visual Studio** (or sign out and back in) so it
sees the new environment variables.

Check it worked - in a new PowerShell window:

```powershell
$env:LUCID_DEV_ROOT        # e.g. C:\Program Files\Lucid Vision Labs\Arena SDK\
$env:LUCID_GENICAM_PATH    # e.g. C:\Program Files\Lucid Vision Labs\Arena SDK\GenICam
```

## 2. Get the code

**Recommended - clone with git:**

```powershell
git clone https://github.com/hwyl2020/GridView_Controller.git
```

**Or download a zip** (GitHub: *Code > Download ZIP*). If you received a zip by
email/Teams/download, unblock it **before** extracting, otherwise Windows marks
every file as "from the internet":

```powershell
Unblock-File .\GridView_Controller.zip     # then extract
```

Extract to a short local path (e.g. `C:\Projects\GridControllerLive`), not inside
OneDrive or a network drive.

## 3. Build

1. Open **`GridControllerLive.slnx`** (VS 2022 17.13+ / VS 2026) or
   **`GridControllerLive.sln`** (any VS 2022).
2. Select configuration **Debug** (or Release) and platform **x64** in the toolbar.
3. *Build > Build Solution* (Ctrl+Shift+B).

The exe is written to `bin\x64\<Debug|Release>\GridControllerLive.exe`.

If Visual Studio asks to **retarget** the project, accept the defaults - the project
uses whichever C++ toolset your Visual Studio has.

Command-line build (Developer PowerShell for VS):

```powershell
msbuild GridControllerLive.slnx /p:Configuration=Debug /p:Platform=x64 /m
```

## 4. Run

1. Connect the camera and **wait 20-30 s** after power-on so it gets its IP address.
2. **Close ArenaView** - a GigE camera accepts only one connection at a time.
3. Press F5 in Visual Studio, or run `bin\x64\Debug\GridControllerLive.exe`.

You get a console window (log output, e.g. `Connected to: <model>`) and the
**Camera Control** window. Closing the console window also closes the app.

If no camera is found, a *Connection Failed* message appears; fix the cause and
click **Reconnect** at the bottom of the right-hand panel.

## 5. Troubleshooting

| Symptom | Fix |
|---|---|
| Build error *"LUCID Arena SDK not found"* | Install the Arena SDK, then restart Visual Studio. |
| `Cannot open include file 'ArenaApi.h'` / unresolved `Arena::` symbols | Same as above; also make sure the platform is **x64**, not x86/Win32. |
| `LNK2019`/`LNK1181` mentioning `*_v3_3_LUCID.lib` | Your Arena SDK has a different GenICam version. Look in `%LUCID_GENICAM_PATH%\library\CPP\lib\Win64_x64` and update the library names under *Project Properties > Linker > Input*. |
| `LNK1104: cannot open file ...GridControllerLive.exe` | The app is still running - close it (check Task Manager). |
| App starts but *"No LUCID cameras found"* | Wait for the camera to boot; close ArenaView; PC NIC and camera on the same subnet (check with ArenaView / IPConfigUtility); allow `GridControllerLive.exe` through Windows Defender Firewall (tick **Public** too if the camera network is classified Public). |
| `GC_ERR_ACCESS_DENIED` / "Invalid security code" | Another app (usually ArenaView) holds the camera, or antivirus/EDR is blocking the unsigned exe - add an exclusion. |
| App crashes at start with a missing DLL (e.g. `Arena_v140.dll`) | The SDK's `x64Release`/`x64Debug` folders are not on `PATH` - reinstall the SDK or add them to `PATH`, then restart. |
| Video is choppy | Use a gigabit link end to end (check the NIC shows 1 Gbps, not 100 Mbps). |

## 6. Project layout

```
GridControllerLive.slnx / .sln     solution (open either)
GridControllerLive/
  GridControllerLive.vcxproj       x64 Debug/Release; SDK paths come from environment variables
  ArenaParameterNode.h             GUI-agnostic parameter data model
  ArenaParameterGrid.h/.cpp        engine: build parameter tree, refresh, set values, search
  LiveView.h/.cpp                  background grab thread + GDI rendering of frames
  FullSettingsView.h/.cpp          searchable parameter tree + type-specific editor
  prototype_main.cpp               WinMain, main window, camera connection, Quick Settings
```

Build outputs (`bin/`, `obj/`) and Visual Studio's `.vs/` folder are not part of the
source - never copy or zip them.
