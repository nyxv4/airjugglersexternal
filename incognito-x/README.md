# IncognitoX — extracted data

Data recovered from `IncognitoX.exe` (4.3 MB native **C++** external, ImGui menu,
MSVC/nlohmann::json/curl, requires admin). The binary is **not packed** — strings and
embedded data are fully readable, but it is closed-source; no source code is public.

## Binary facts

- Attaches to `RobloxPlayerBeta.exe` via `OpenProcess` + `ReadProcessMemory` /
  `WriteProcessMemory` (plain usermode, no driver).
- Embedded Roblox build string: `version-f5a60436d48947d3`.
- Requires administrator (`requireAdministrator` manifest).
- Configs stored under `\IncognitoX\Configs`.
- Auth via **KeyAuth** (`##license`, `##username`, `##password` UI fields).
- Bundled **Lua 5.4** environment with script editor + console (`##lua_editor`,
  `##lua_console`, "Lua output will appear here.").
- Built with GIMP-edited image assets (icons embedded as PNG/XMP).

## Contents

- `strings-all.txt` — every printable string in the binary (24k lines; includes
  curl/SSL noise).
- `feature-strings.txt` — curated feature/UI/config strings (196 lines).
- `roblox-classes.txt` — ~800 Roblox class names embedded for the SDK/instance walker
  (covers Workspace services, UI classes, physics constraints, etc.).

## Feature set (from recovered strings)

### Combat
- **Aimbot** — activation modes, target part, smoothing X/Y, shake, prediction X/Y,
  knock check, team check, health check, range check, min health, sticky aim,
  priorities list, FOV circle (fill + outline), auto-switch.
- **Silent Aim** — gun/bow checks, Rivals support, wallbang, ray scale, mesh id,
  visualizer, prediction, target bone, sticky.
- **Raycast Silent** — separate silent-aim variant with priorities.
- **Rivals Silent** — Rivals-specific silent aim.
- **Triggerbot** — delay min/max, randomize delay, key.
- **Hitbox Expander** — size X/Y/Z, update interval, per-part colours/outline,
  wallcheck, sword check.
- **Wallcheck** — transparency check, visualizer.

### Games
- **Bedwars** — Auto Collect, AutoFish, prediction, smoothing; ESP for bee, hive,
  star, pot, elder tree, gen, hidden metal, bedplates, team crates, inventory,
  enemy hotbar; role ESP; item/config strings for bedplates, coins, fishing
  minigame, gear.
- **MM2 (Murder Mystery 2)** — sheriff/innocent/murderer ESP colours, coin delay,
  tween farm/speed, autofarm, autofish.
- **Rivals** — silent aim + ESP.
- **Skin Changer** — weapon skin replacement (`rbxassetid://` IDs embedded).

### Movement / misc
- **Speedhack** (modes), **Flyhack** (modes), **Knockback**, **Desync**, **Spin360**,
  **Gravity**, **Tickrate** — each with keybinds.
- ESP framework: boxes, skeleton, chams (+outline), tracers, health bar, tool,
  username/distance/health colours, `rbxassetid://137502385784703` font/asset.
- Streamproof, unlock FPS / fps cap, vsync, performance mode, keybind list,
  console hide.
- Lua executor tab (editor + output console).
- Theme system + config save/load/delete (named configs).

## Not included

- The `.exe` itself — closed-source commercial software, not redistributed.
- Actual source code — does not exist publicly; the binary would need full
  reverse engineering to approximate.
