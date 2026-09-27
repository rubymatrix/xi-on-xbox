# FFXI on Xbox One (developer mode)

Run xi-on-mac's natively recompiled FINAL FANTASY XI as a UWP game on an Xbox One in
developer mode.

Sister project of [xi-on-mac](https://github.com/rubymatrix/xi-on-mac). This repo holds only what the Xbox
needs on top of it: the UWP app shell, the Direct3D 11 back end, packaging, and tooling.
The recompiler, runtime, and platform layer stay in xi-on-mac and are built from there.
The shell's build expects it cloned beside this repo as `xi-on-mac` (git's default folder name);
elsewhere, pass `-p:XiOnMacDir=<path>\` to MSBuild.

## Rules

- Same rule as xi-on-mac: **no Square Enix bytes in this repo.** No DLLs, no DAT files,
  no generated C. The player supplies their own install.
- Dev mode builds are sideloaded to your own console. They aren't distributed.

## Why it can work

- The game is recompiled ahead of time, so there's no JIT. The UWP sandbox forbids
  executable memory, and nothing here needs it.
- xi-on-mac's runtime already emulates the Win32 surface the game uses (files,
  registry, threads, sockets, DirectInput, DirectSound, polcore), and `host64` already
  runs on Windows x64.
- The CPU budget is close but workable: see [docs/feasibility.md](docs/feasibility.md).

## Plan

| gate / phase | work | size |
|---|---|---|
| **Gate 0**: CPU budget estimate | `tools/frame_budget.py` on macOS profiles | **done: go** ([feasibility](docs/feasibility.md)) |
| **Gate 1**: CPU budget on the console | headless UWP build (null graphics), scripted login, profile to LocalState. **Handoff: [docs/handoff-gate1.md](docs/handoff-gate1.md)** | ~1 wk |
| D3D11 back end | `gfx_d3d11.c` behind xi-on-mac's `gfx.h`; D3D8 fixed-function and vs/ps 1.x generated as HLSL; command encoding on a render thread. Also gives Windows x64 graphics (it has only `gfx_null.c` today). | 1.5–3 wk |
| UWP shell | CoreWindow, Windows.Gaming.Input, XAudio2 in place of SDL3 (SDL3 has no UWP support); package declared as a **Game** (about 5 GB RAM and the full GPU, versus about 1 GB for an App) | 1–2 wk |
| UWP API changes in xi-on-mac's `plat_win.c` / `vfs.c` | `VirtualAllocFromApp`, `CreateFile2`, the game folder in LocalState, the `internetClient` capability | days |
| Game data | 14 GB install uploaded beside the app, registered as a loose app on the console's development drive (LocalState's drive is too small): **[docs/xbox-install.md](docs/xbox-install.md)** | done |
| Login | LandSandBoat (`lsb_login.c`, already inside `host64`) | days |

## Tools

Installing on the console and getting the game onto it, from any computer on its network: set it up
once, then each build is a small update that leaves the game in place. Why it's done this way, and
every quirk found on the way: [docs/xbox-install.md](docs/xbox-install.md).

```
XBOX=<console address> python3 tools/xbox.py setup build/XIonXbox-xbox.msix "<...>/FINAL FANTASY XI"
python3 tools/xbox.py deploy build/XIonXbox-xbox.msix     # each new build
python3 tools/xbox.py logs <folder>                         # host64.log, app.log, samples.bin
```

Claude Code skills for the same: `.claude/skills/xbox-setup`, `xbox-deploy`, `xbox-logs`.

```
python3 tools/frame_budget.py ../xi-on-mac/build/run_phoenix_0903b.log [--factor 6.5] [--min-draws 500]
```

It reads `host64`'s `FFXI_PROFILE=1` output and projects the game thread's busy time per
frame onto a slower CPU.
