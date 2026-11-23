# NYX External — Patch Notes

## v10 — UI Themes
- 4 switchable menu themes: Midnight, Incognito, Neon, Light
- Live accent color picker (tints sliders, tabs, buttons, checkmarks)
- Theme + accent persist in `config/nyx.cfg`

## v9 — Streamproof hardening
- Streamproof toggle verified: affinity result logged to `nyx_debug.log`
- `WDA_EXCLUDEFROMCAPTURE` re-applied every 2s so capture setups can't reset it
- Misc tab checkbox turns it on/off

## v8 — Activation modes + Kit ESP
- Aim Assist / Triggerbot / Hitbox each get an **Activation** mode:
  `Hold key` / `Toggle key` / `Always on`
- Hitbox defaults to Always on (script-style, no key hold needed)
- **Kit ESP**: scans player/backpack/character names, draws `[KIT]` above heads
  - BedWars kits: Void Knight, Block Kicker, Elk Master, Summoner, Paladin,
    Davey, Airbender, Hatter, Mage, Archer, Assassin, Emerald, Fireball,
    Pyromancer, Frosty, Gravity, Healer, Invis, Knight, Lucky, Mason,
    Shepard, Smelly, Soapy, Spider, TNT, Warrior, Wizard
  - Singles: Metal, Elder, Iron, Steel, Bronze, Copper, Silver, Gold,
    Diamond, Ruby, Sapphire, Topaz, Amethyst, Quartz, Jade, Onyx, Pearl,
    Opal, Netherite
- Hitbox memory writes logged for diagnosis

## v7 — Lag fix
- Player part addresses (Head/HRP/Humanoid) cached, refreshed ~2hz or on respawn
- View matrix cached 8ms instead of re-read per projection
- Health read directly from cached humanoid (~10x fewer memory reads)
- Local player position/humanoid cached
- Render loop capped ~120fps (was uncapped, pinning a core)

## v6 — Aim Assist options
- Enable smoothing + smoothing style (None / Linear)
- Enable shake + shake px slider
- Enable prediction + prediction ms (velocity-based target leading)
- Checks: Team, Knock, Health, Range

## v5 — Live fixes
- Offsets URL fixed (imtheo.lol → offsets.imtheo.lol)
- Player name reading fixed via NameContainer
- Transparency offset corrected to BasePart
- `nyx_debug.log` diagnostics added

## v4 — Destroy/Unload
- DESTROY / UNLOAD button in Misc tab: restores hitboxes, detaches, exits clean

## v3 — Aim Assist rename + tuning
- AIMBOT → Aim Assist, FOV → ANGLE
- Smoothness slider 1–10, range capped at 30 studs

## v2 — Menu & performance
- Menu key default Right Shift, starts closed
- Keybinds rebindable in Misc tab
- Menu/features only work while Roblox is focused
- ESP pauses when Roblox unfocused; child enumeration throttled

## v1 — C++ rewrite
- Full native rewrite (C++ / DX11 / ImGui), ~275KB exe, no .NET required
- Auto-attach to Roblox, live offset fetch
- ESP: box, name, distance, health, snaplines, team check
- Triggerbot, hitbox expander (14.4 stud cap support)
- Streamproof overlay, `INSERT` menu / `END` quit
