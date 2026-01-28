#include "PatternScanner.hpp"
#include "Utils.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "psapi.lib")

namespace PatternScanner
{
    // Internal: Parse pattern string into bytes and mask
    static bool ParsePattern(std::string_view pattern, std::vector<uint8_t>& bytes, std::vector<bool>& mask)
    {
        bytes.clear();
        mask.clear();

        std::string patternStr(pattern);
        std::istringstream stream(patternStr);
        std::string token;

        while (stream >> token)
        {
            if (token == "??" || token == "?")
            {
                bytes.push_back(0x00);
                mask.push_back(false); // wildcard
            }
            else
            {
                try
                {
                    uint8_t byte = static_cast<uint8_t>(std::stoul(token, nullptr, 16));
                    bytes.push_back(byte);
                    mask.push_back(true); // must match
                }
                catch (...)
                {
                    Utils::LogError("PatternScanner: Invalid pattern byte");
                    return false;
                }
            }
        }

        return !bytes.empty();
    }

    // Internal: Compare memory against pattern
    static bool ComparePattern(const uint8_t* data, const std::vector<uint8_t>& bytes, const std::vector<bool>& mask)
    {
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            if (mask[i] && data[i] != bytes[i])
            {
                return false;
            }
        }
        return true;
    }

    bool GetModuleInfo(const char* moduleName, uintptr_t& baseOut, size_t& sizeOut)
    {
        HMODULE hModule = nullptr;

        if (moduleName == nullptr || moduleName[0] == '\0')
        {
            // Get main executable module
            hModule = GetModuleHandleA(nullptr);
        }
        else
        {
            hModule = GetModuleHandleA(moduleName);
        }

        if (!hModule)
        {
            return false;
        }

        MODULEINFO modInfo = {};
        if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo)))
        {
            return false;
        }

        baseOut = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
        sizeOut = modInfo.SizeOfImage;
        return true;
    }

    uintptr_t FindPattern(uintptr_t start, size_t size, std::string_view pattern)
    {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask;

        if (!ParsePattern(pattern, bytes, mask))
        {
            Utils::LogError("PatternScanner: Failed to parse pattern");
            return 0;
        }

        // Bounds check to prevent integer underflow
        if (bytes.empty() || size < bytes.size())
        {
            Utils::LogWarn("PatternScanner: Pattern larger than search region");
            return 0;
        }

        const uint8_t* scanStart = reinterpret_cast<const uint8_t*>(start);
        const size_t scanSize = size - bytes.size() + 1;

        for (size_t offset = 0; offset < scanSize; ++offset)
        {
            if (ComparePattern(scanStart + offset, bytes, mask))
            {
                return start + offset;
            }
        }

        return 0;
    }

    uintptr_t FindPattern(const char* moduleName, std::string_view pattern)
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(moduleName, base, size))
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "PatternScanner: Module '%s' not found",
                     moduleName ? moduleName : "main");
            Utils::LogError(msg);
            return 0;
        }

        uintptr_t result = FindPattern(base, size, pattern);

        if (result == 0)
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "PatternScanner: Pattern not found in '%s'",
                     moduleName ? moduleName : "main");
            Utils::LogWarn(msg);
        }
        else
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "PatternScanner: Found pattern at 0x%llX",
                     static_cast<unsigned long long>(result));
            Utils::LogInfo(msg);
        }

        return result;
    }

    uintptr_t FindPattern(std::string_view pattern)
    {
        // Scan main executable
        return FindPattern(nullptr, pattern);
    }

    uintptr_t ResolveRelativeAddress(uintptr_t instructionAddr, int32_t offset, int instructionSize)
    {
        // For instructions like CALL rel32 or JMP rel32:
        // The target address = instruction address + instruction size + relative offset
        // The relative offset is typically at (instructionAddr + offset)
        int32_t relativeOffset = *reinterpret_cast<int32_t*>(instructionAddr + offset);
        return instructionAddr + instructionSize + relativeOffset;
    }
}
