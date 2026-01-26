#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace PatternScanner
{
    // Scan for a pattern in the main game module
    // Pattern format: "48 8B 05 ?? ?? ?? ?? 48 85 C0" where ?? is wildcard
    uintptr_t FindPattern(std::string_view pattern);

    // Scan in a specific module
    uintptr_t FindPattern(const char* moduleName, std::string_view pattern);

    // Scan in a specific memory range
    uintptr_t FindPattern(uintptr_t start, size_t size, std::string_view pattern);

    // Get module base address and size
    bool GetModuleInfo(const char* moduleName, uintptr_t& baseOut, size_t& sizeOut);

    // Resolve a relative call/jump address (for patterns that find CALL/JMP instructions)
    uintptr_t ResolveRelativeAddress(uintptr_t instructionAddr, int32_t offset, int instructionSize);

    // Common patterns for Cyberpunk 2077 (v2.x)
    namespace Patterns
    {
        // Camera update function - BaseCameraComponent::Update
        // This pattern targets the prologue of the camera update function
        constexpr const char* CameraUpdate =
            "40 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 8B CB";

        // D3D12 Present hook - IDXGISwapChain::Present
        // We hook this to grab the command queue and backbuffer
        constexpr const char* DXGIPresent =
            "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 48 8B F9 41 8B F0";

        // D3D12 CommandQueue creation
        constexpr const char* CreateCommandQueue =
            "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B E8";

        // Alternative: REDengine render thread entry
        constexpr const char* RenderThreadMain =
            "48 8B C4 48 89 58 ?? 48 89 68 ?? 48 89 70 ?? 48 89 78 ?? 41 56 48 83 EC";
    }
}
