# NYX External

Native C++ Roblox external — separate process, no injection. Reads/writes
`RobloxPlayerBeta.exe` memory via `ReadProcessMemory`/`WriteProcessMemory`,
renders ESP and the menu through a transparent topmost DirectX11 + ImGui
overlay, and simulates input with `SendInput`.

## Repo layout

```
├── CMakeLists.txt          build definition
├── include/                public headers (memory, sdk, offsets, overlay, settings)
├── src/
│   ├── main.cpp            entry point
│   ├── core/               memory.cpp (RPM/WPM), http.cpp (WinINet)
│   ├── game/               offsets.cpp (runtime offset fetch), sdk.cpp (datamodel traversal)
│   ├── render/             overlay.cpp (DX11 window + ImGui + ESP + features)
│   └── menu/               settings.cpp (config load/save)
├── ext/imgui/              Dear ImGui (v1.91.9b) + win32/dx11 backends
├── assets/                 images/logo
└── config/nyx.cfg          generated settings file
```

## How externals like this are built (GitHub workflow)

GitHub only hosts source — it doesn't compile anything by itself. The usual
flow:

1. Create a repo, clone it locally.
2. Write C++ sources (`src/`), headers (`include/`), vendored deps (`ext/`).
3. Build locally with MSVC/CMake or Visual Studio — GitHub never runs the
   compiler for you (GitHub Actions *can* automate builds, but that's optional).
4. Commit changes, push to `main`, tag releases, upload compiled `.zip`/`.exe`
   artifacts under `releases/` for download.

## Building

Requirements: Windows 10/11 x64, Visual Studio 2022+ (or Build Tools) with
the C++ workload, CMake.

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build/Release/NyxExternal.exe`

Or open the folder in Visual Studio and build `Release|x64`.

## Usage

1. Run `NyxExternal.exe` **as administrator** (memory access requires it).
2. It auto-attaches to `RobloxPlayerBeta.exe` once you join a game.
3. `INSERT` toggles the menu. `END` closes the app.
4. Config is saved to `config/nyx.cfg` on exit.

## Features (v1)

- ESP: box, name, distance, health bar, snaplines, team check
- Aimbot: hold `X`, configurable FOV/smoothness/range
- Triggerbot: hold `X`, fires when crosshair is on an enemy
- Hitbox expander: hold `C`, configurable multiplier + stud cap
  (set `hitbox_max=14.4` for Bedwars)
- Streamproof overlay (`WDA_EXCLUDEFROMCAPTURE`)
- Runtime offsets fetched from `imtheo.lol` (auto-updates with Roblox)

## Notes

- Offsets are fetched at runtime — no manual offset updates needed unless the
  dumper changes.
- Untested against live Roblox; memory layouts can shift between client builds.
- For educational purposes.
