# Contributing to Cyberpunk 2077 VR Mod

Thank you for your interest in contributing! This project needs community help to become a fully working VR mod.

## Ways to Contribute

### 1. Testing (No coding required!)
- Install the mod and report what happens
- Check the log file for errors
- Try different VR headsets and report compatibility
- Document steps to reproduce any issues

### 2. Pattern Research
The camera hook relies on finding specific byte patterns in the game executable. These patterns may change with game updates.

**How to help:**
1. Use IDA Pro, Ghidra, or x64dbg to analyze `Cyberpunk2077.exe`
2. Find the `entBaseCameraComponent::Update` function
3. Extract a unique byte signature
4. Submit the pattern in an issue or PR

Current patterns are in `include/PatternScanner.hpp`

### 3. Code Contributions

**Setup:**
```bash
git clone --recurse-submodules https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource.git
cd Cyberpunk2077-VR-OpenSource
# Open in VS Code or Visual Studio
# Build with F7
```

**Guidelines:**
- Use C++20 features
- Follow existing code style
- Use RAII (ComPtr, unique_ptr, lock_guard)
- Make thread-safe by default
- Add logging for debugging
- Test before submitting PR

### 4. Documentation
- Improve README
- Write setup guides
- Create troubleshooting docs
- Add code comments

## Reporting Issues

Please include:
1. **Game version** (e.g., 2.12)
2. **VR headset** (Quest 2, Index, etc.)
3. **VR runtime** (SteamVR, Oculus, WMR)
4. **Log file** contents from `[Game]/bin/x64/plugins/red4ext.log`
5. **Steps to reproduce**
6. **Expected vs actual behavior**

## Pull Request Process

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Test thoroughly
5. Commit with clear message
6. Push to your fork
7. Open a Pull Request

## Priority Areas

| Area | Difficulty | Impact |
|------|------------|--------|
| Camera pattern verification | Medium | Critical |
| VR hardware testing | Easy | Critical |
| CET settings integration | Easy | High |
| Controller mapping | Medium | High |
| Performance optimization | Hard | Medium |

## Code of Conduct

- Be respectful and constructive
- No paywalls or monetization
- Credit original work
- Keep it open source

## Questions?

Open an issue with the `question` label or start a discussion.

---

Every contribution matters. Let's make VR accessible to everyone!
