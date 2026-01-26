# Cyberpunk 2077 VR Mod (Open Source)

**⚠️ DEVELOPER PREVIEW / CALL FOR CONTRIBUTORS ⚠️**

> **Current Status:** The core framework (RED4ext + OpenXR + Camera Hooks) compiles and loads. We are releasing this source code to the community to accelerate development. This is **NOT** a ready-to-play mod for gamers yet.

## ✊ The Motive
VR mods shouldn't be locked behind paywalls or lost when a single developer stops working on them. 

We saw the drama surrounding paid VR mods and DMCA takedowns, and we asked: **"Why isn't there an open-source alternative?"**
This project is the answer. We are building a foundation that belongs to the community—free, open, and impossible to shut down.

## 🗿 Standing on the Shoulders of Giants
This project did not appear out of thin air. It is built upon the massive engineering efforts of the modding and VR communities. We acknowledge and credit the pioneers whose work made this possible:

*   **The RED4ext Team (WopsS & Community):** For the incredible reverse-engineering SDK that allows us to talk to REDengine. Without them, we'd be blindly poking at memory addresses.
*   **The 3Dmigoto & HelixMod Communities:** The original "Shaderhackers" who invented many of the stereoscopic rendering techniques (like Alternate Eye Rendering) used in modern VR injectors.
*   **The GTA V Modding Community:** For their extensive research into engine reverse-engineering and VR implementation patterns, which paved the way for mods like this.
*   **Praydog (UEVR):** For showing the world how generic VR injection should be done—open source, configurable, and robust.
*   **Luke Ross:** For proving that Cyberpunk 2077 *can* look amazing in VR using the AER technique, providing the proof-of-concept that inspired this open implementation.
*   **Khronos Group:** For OpenXR, which lets us support every headset with a single codebase.

## 🛠️ Technical Status
We have successfully implemented the "Hard Part" - the boilerplate infrastructure that connects the game engine to the VR headset:
*   ✅ **RED4ext Integration:** Plugin loads and accesses Game/Camera systems.
*   ✅ **OpenXR Implementation:** Initializes session, swapchains, and head tracking.
*   ✅ **Camera Hook:** Intercepts the rendering camera (`entBaseCameraComponent`) to apply head tracking and stereoscopic offsets.
*   ✅ **Build System:** Compiles cleanly with Visual Studio 2026 / C++20.

## 🤝 Call for Contributors
We need developers with C++ experience and VR hardware to take this across the finish line. 
**Immediate Tasks:**
1.  **Verification:** Run the mod, verify the `CyberpunkVR.log` shows successful OpenXR initialization.
2.  **Tuning:** Adjust the `ipd` (inter-pupillary distance) offset in `CameraHook.cpp` to look correct in-game.
3.  **Rendering:** Verify the "Alternate Eye Rendering" (AER) logic is syncing correctly with the frame rate.

## 📦 Building the Mod
### Prerequisites
1.  **Visual Studio 2026** (Community Edition is fine) with "Desktop development with C++".
2.  **Cyberpunk 2077** (Latest Version).
3.  **RED4ext Loader** installed in your game directory.

### Build Instructions
1.  Clone this repository:
    ```bash
    git clone --recurse-submodules https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource.git
    ```
2.  Open the folder in **Visual Studio**.
3.  Wait for CMake to configure.
4.  **Build All** (Ctrl+Shift+B).
5.  Copy `out/build/x64-Release/bin/CyberpunkVR.dll` to:
    `[Game Path]/bin/x64/plugins/CyberpunkVR/CyberpunkVR.dll`

## ⚖️ Legal & Compliance
**Strictly Non-Commercial / Free to Download**
To fully comply with [CD PROJEKT RED's Fan Content Guidelines](https://cdprojektred.com/en/fan-content):
*   This project is and will always be **100% free** to download.
*   No features will be paywalled or locked behind subscriptions.
*   We respect CDPR's IP and rights.
