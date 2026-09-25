# FFXI on Xbox One (developer mode)

Run FFXIRecompile's natively recompiled FINAL FANTASY XI as a UWP game on an Xbox One in
developer mode.

Sister project of [FFXIRecompile](../FFXIRecompile). This repo holds only what the Xbox
needs on top of it: the UWP app shell, the Direct3D 11 back end, packaging, and tooling.
The recompiler, runtime, and platform layer stay in FFXIRecompile and are built from there.

## Rules

- Same rule as FFXIRecompile: **no Square Enix bytes in this repo.** No DLLs, no DAT files,
  no generated C. The player supplies their own install.
- Dev mode builds are sideloaded to your own console. They aren't distributed.

## Why it can work

- The game is recompiled ahead of time, so there's no JIT. The UWP sandbox forbids
  executable memory, and nothing here needs it.
- FFXIRecompile's runtime already emulates the Win32 surface the game uses (files,
  registry, threads, sockets, DirectInput, DirectSound, polcore), and `host64` already
  runs on Windows x64.
- The CPU budget is close but workable: see [docs/feasibility.md](docs/feasibility.md).

## Plan

| gate / phase | work | size |
|---|---|---|
| **Gate 0**: CPU budget estimate | `tools/frame_budget.py` on macOS profiles | **done: go** ([feasibility](docs/feasibility.md)) |
| **Gate 1**: CPU budget on the console | headless UWP build (null graphics), scripted login, profile to LocalState. **Handoff: [docs/handoff-gate1.md](docs/handoff-gate1.md)** | ~1 wk |
| D3D11 back end | `gfx_d3d11.c` behind FFXIRecompile's `gfx.h`; D3D8 fixed-function and vs/ps 1.x generated as HLSL; command encoding on a render thread. Also gives Windows x64 graphics (it has only `gfx_null.c` today). | 1.5–3 wk |
| UWP shell | CoreWindow, Windows.Gaming.Input, XAudio2 in place of SDL3 (SDL3 has no UWP support); package declared as a **Game** (about 5 GB RAM and the full GPU, versus about 1 GB for an App) | 1–2 wk |
| UWP API changes in FFXIRecompile's `plat_win.c` / `vfs.c` | `VirtualAllocFromApp`, `CreateFile2`, the game folder in LocalState, the `internetClient` capability | days |
| Game data | 14 GB install uploaded to LocalState through Device Portal (or a USB drive) | days |
| Login | LandSandBoat (`lsb_login.c`, already inside `host64`) first; retail PlayOnline moves from FFXI-Signet's `signet` launcher into the app later | days / 1–2 wk |

## Tools

```
python3 tools/frame_budget.py ../FFXIRecompile/build/run_phoenix_0903b.log [--factor 6.5] [--min-draws 500]
```

It reads `host64`'s `FFXI_PROFILE=1` output and projects the game thread's busy time per
frame onto a slower CPU.
