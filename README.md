# Cyberpunk 2077 VR Mod (Open Source)

**Status: ALPHA - Seeking Testers & Contributors**

> This is an open-source VR mod framework for Cyberpunk 2077. The core infrastructure is complete and ready for community testing. We need people with VR headsets to test, report issues, and help refine the experience.

## What's Implemented

| Component | Status | Description |
|-----------|--------|-------------|
| RED4ext Plugin | ✅ Complete | Loads into game, accesses engine systems |
| OpenXR Integration | ✅ Complete | Session, swapchains, head tracking, stereo rendering |
| D3D12 Hook | ✅ Complete | Captures game backbuffer, submits to VR headset |
| Camera Hook | ⚠️ Needs Testing | Pattern-based hook for head tracking injection |
| Texture Copy | ✅ Complete | Copies game frames to OpenXR swapchains |
| Coordinate Conversion | ✅ Complete | OpenXR ↔ REDengine coordinate system |
| Thread Safety | ✅ Complete | Mutex, atomics, ComPtr throughout |
| Session Handling | ✅ Complete | Full OpenXR state machine, HMD disconnect detection |
| VR Controller Input | ✅ Complete | OpenXR action system → XInput mapping |
| CET Settings UI | ✅ Complete | Lua UI connected to C++ via native functions |

## Known Limitations

- **Camera patterns are estimates** - May need adjustment for your game version
- **Controller mapping is basic** - VR controllers map to gamepad buttons (no motion aiming yet)
- **AER (Alternate Eye Rendering)** - Runs at half framerate per eye
- **No depth buffer** - 3D effect from stereo only, no reprojection

## VR Controller Mapping

| VR Controller | Game Action (XInput) |
|---------------|---------------------|
| Left Thumbstick | Move (Left Stick) |
| Right Thumbstick | Look (Right Stick) |
| Left Trigger | Aim (LT) |
| Right Trigger | Fire (RT) |
| Left Grip | Left Shoulder (LB) |
| Right Grip | Right Shoulder (RB) |
| A / X Buttons | A / X |
| B / Y Buttons | B / Y |
| Menu Button | Start |
| Thumbstick Click | L3 / R3 |

## Quick Start (For Testers)

### Requirements
- Cyberpunk 2077 (latest version)
- [RED4ext](https://github.com/WopsS/RED4ext) installed
- VR Headset with OpenXR runtime (SteamVR, Oculus, WMR, etc.)
- Windows 10/11

### Installation
1. Download the latest release (or build from source)
2. Copy `CyberpunkVR.dll` to: `[Game]/bin/x64/plugins/CyberpunkVR/`
3. Start your VR runtime (SteamVR, etc.)
4. Launch Cyberpunk 2077

### What to Report
Check `[Game]/bin/x64/plugins/red4ext.log` and report:
- Does OpenXR initialize? Look for `OpenXR: Fully initialized!`
- Does D3D12 hook work? Look for `D3D12Hook: Resources captured successfully!`
- Does camera hook find the pattern? Or does it say `Could not find camera update function`?
- Do you see the game in your headset?
- Does head tracking work?

## Building from Source

### Prerequisites
- Visual Studio 2019/2022 with C++ desktop development
- CMake 3.22+
- Git

### Build Steps
```bash
git clone --recurse-submodules https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource.git
cd Cyberpunk2077-VR-OpenSource
```

**Option A: Visual Studio**
1. Open folder in VS Code or Visual Studio
2. Let CMake configure
3. Build (F7 or Ctrl+Shift+B)

**Option B: Command Line**
```bash
cmake -B out -A x64
cmake --build out --config Release
```

Output: `out/build/x64-Release/bin/CyberpunkVR.dll`

## Project Structure

```
├── include/
│   ├── VRSystem.hpp        # OpenXR interface (PIMPL pattern)
│   ├── D3D12Hook.hpp       # Present hook for frame capture
│   ├── CameraHook.hpp      # Game camera manipulation
│   ├── PatternScanner.hpp  # Memory pattern scanning
│   ├── InputHook.hpp       # XInput interception
│   ├── ThreadSafe.hpp      # Thread safety utilities
│   └── Utils.hpp           # Logging
├── src/
│   ├── Main.cpp            # RED4ext entry point
│   ├── VRSystem.cpp        # OpenXR + D3D12 implementation
│   ├── D3D12Hook.cpp       # IDXGISwapChain::Present hook
│   ├── CameraHook.cpp      # Camera update hook + AER
│   ├── PatternScanner.cpp  # Signature scanning
│   └── InputHook.cpp       # XInput hook
├── deps/
│   ├── RED4ext.SDK/        # Game engine SDK
│   └── OpenXR-SDK/         # Khronos OpenXR
└── bin/x64/plugins/cyber_engine_tweaks/mods/CyberpunkVR/
    └── init.lua            # CET settings UI (WIP)
```

## Contributing

### Priority Tasks

**High Priority (Blocking VR Experience)**
1. **Test & verify camera patterns** - The patterns in `PatternScanner.hpp` are educated guesses. We need someone to verify them against CP2077 2.x binaries using IDA/Ghidra.
2. **Test on actual VR hardware** - We need reports from Quest, Index, Vive, WMR, etc.
3. **Fix any crashes** - Report stack traces and repro steps.

**Medium Priority (Improve Experience)**
4. ~~Wire CET settings to C++~~ ✅ Done - IPD, world scale, VR toggle now work from CET menu
5. **Tune coordinate conversion** - The quaternion math may need adjustment.
6. ~~Add VR controller support~~ ✅ Done - OpenXR controllers map to XInput gamepad

**Lower Priority (Polish)**
7. **Motion controller aiming** - Decouple aim from head tracking for gun-pointing
8. **Add comfort options** - Vignette, snap turning, etc.
9. **Performance optimization** - Reduce copy overhead, async reprojection hints.
10. **Documentation** - Setup guides, troubleshooting.

### Code Style
- C++20 standard
- RAII everywhere (ComPtr, unique_ptr, lock_guard)
- No raw `new`/`delete`
- Thread-safe by default

## Architecture

```
Game Loop                          VR System
─────────                          ─────────

[Camera Update] ──► CameraHook ──► Get head pose from OpenXR
       │                           Apply IPD offset (AER)
       │                           Override camera transform
       ▼
[Render Frame]
       │
       ▼
[Present] ──────► D3D12Hook ─────► Copy backbuffer to OpenXR swapchain
                                   Submit left eye (even frames)
                                   Submit right eye (odd frames)
                                   xrEndFrame
```

## Credits

- **RED4ext Team** - Engine SDK that makes this possible
- **Khronos Group** - OpenXR standard
- **3Dmigoto/HelixMod** - Pioneered stereoscopic injection techniques
- **Praydog (UEVR)** - Inspiration for open-source VR injection
- **Luke Ross** - Proved CP2077 VR is possible with AER

## License & Legal

- **Open Source** - MIT License
- **Non-Commercial** - Always free, no paywalls
- **CDPR Compliant** - Respects [Fan Content Guidelines](https://cdprojektred.com/en/fan-content)

---

**Questions?** Open an issue on GitHub.

**Want to help?** Fork, fix, and PR. All contributions welcome!
