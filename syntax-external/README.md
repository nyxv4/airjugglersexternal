# Syntax External — extracted data

Data recovered from the local install of **Syntax External** (`com.boots.syntax-external`,
Tauri/Rust app by "boots"). The application itself is closed-source; its frontend assets
are embedded in the binary brotli-compressed and its backend is compiled Rust, so no
source code could be recovered. Everything extractable is collected here.

## Contents

- `offsets/` — Roblox offset dumps in JSON form (theo's RbxDumperV2 format), one per
  client version. Same data as `offsets.imtheo.lol`, fetched at runtime by the app.
- `settings.json` — the app's persisted config schema (feature toggles are stored under
  `__flags`, `__nums`, `__drops`, `__armless`, `__collapsed`, `__open`, `__pos`; UI theme
  under `__ui`; active game profile under `game`, e.g. `"uni"` for universal).
- `frontend-strings.txt` — all string literals recovered from the app's compiled
  frontend (V8 code cache). Contains the **complete config key schema** (`bw.*`,
  `mv.*`, `rv.*` profiles), every feature name, UI label, and description text.
- `logs/aimlog.txt` — runtime log of the aimbot: per-tick diagnostics showing FOV cone
  checks (`in cone`), wall/block checks (`blocks`, `walls`), game-focus and
  cursor-position gating, keybind toggles.
- `logs/draftlog.txt` — log of the draft assistant (`MatchDraftApp` detection via
  PlayerGui).

## Feature set (from recovered strings)

Game tabs: **Bedwars** (`bw.*`), **MVS?** (`mv.*`/`tab.mvsd`), **Rivals** (`rv.*`).

### Aimbot / combat
- Aimlock with target bone selection (Head, HumanoidRootPart, Torso, UpperTorso),
  target modes (Closest, Lowest/Highest health, Highlight nearest), sticky target,
  grace period, toggle vs hold key, sword-only, visible-only, team check, FOV/range.
- Triggerbot: FOV, range, delay, jitter, direct-fire option, sword-only, NPC support
  (Rivals), "send clicks to the game".
- Hitbox Expander: per-part multiplier and range.
- Wall check (skip targets behind blocks), third-person aim (moves pointer onto target).

### ESP / visuals
- Box ESP (styles, thickness, fill, corner boxes), health bar, name tags, team labels,
  weapon display, tracers (from crosshair/bottom), distance labels, text outline.
- Kit ESP (Bedwars): tracks orbs/pickups by kind color and label (bees amber, metal
  blue, orbs green, stars white…), nearest-item highlight.
- Rivals ESP + trigger with NPC targeting.

### Misc
- Speed (ground/air boost, key toggle), Spinbot (direction/speed), Fly.
- Headless (hides hair/accessories, size multiplier).
- Stream proof (hides overlay from capture; can borrow Roblox's cursor).
- FPS counter, watermark, keybind list, input tester, demo markers.
- Offsets manager: auto-fetch per client version, paste-your-own `offsets.json`,
  "use pasted offsets", re-fetch now.
- Config save/load, UI themes (accent, opacity, scale, Mica/blur), overlay windows
  (menu, HUD, console/log reader, player list).

## Not included

- `syntax_external.exe` / `Syntax.exe` — the compiled binaries are **not** redistributed
  here (closed-source commercial software).
- Frontend source (HTML/JS/CSS) — embedded compressed inside the binaries; only string
  literals were recoverable.
