-- CyberpunkVR - Configuration & UI Mod
-- Requires: Cyber Engine Tweaks
-- Communicates with CyberpunkVR.dll via native functions

local CyberpunkVR = {
    settings = {
        enabled = true,
        ipd = 64.0, -- mm
        worldScale = 1.0,
        uiDistance = 2.0, -- meters (future use)
        decoupleAim = true, -- (future use)
        debugMode = false
    },
    isOverlayOpen = false,
    initialized = false
}

-- Helper to safely call native functions
local function SafeCall(funcName, ...)
    local success, result = pcall(function(...)
        return _G[funcName](...)
    end, ...)

    if not success then
        if CyberpunkVR.settings.debugMode then
            print("[CyberpunkVR] Native call failed: " .. funcName .. " - " .. tostring(result))
        end
        return nil
    end
    return result
end

function CyberpunkVR:SyncFromNative()
    -- Pull current values from C++ on startup
    local enabled = SafeCall("CyberpunkVR_GetEnabled")
    local ipd = SafeCall("CyberpunkVR_GetIPD")
    local worldScale = SafeCall("CyberpunkVR_GetWorldScale")

    if enabled ~= nil then self.settings.enabled = enabled end
    if ipd ~= nil then self.settings.ipd = ipd end
    if worldScale ~= nil then self.settings.worldScale = worldScale end

    self.initialized = true
    print("[CyberpunkVR] Settings synced from native: IPD=" .. self.settings.ipd .. "mm, Scale=" .. self.settings.worldScale)
end

function CyberpunkVR:ApplySettings()
    -- Push all settings to C++
    SafeCall("CyberpunkVR_SetEnabled", self.settings.enabled)
    SafeCall("CyberpunkVR_SetIPD", self.settings.ipd)
    SafeCall("CyberpunkVR_SetWorldScale", self.settings.worldScale)

    if self.settings.debugMode then
        print("[CyberpunkVR] Settings applied to native")
    end
end

function CyberpunkVR:OnInitialize()
    print("[CyberpunkVR] Lua Module Initialized - Waiting for native DLL...")

    -- Delay sync to ensure DLL is loaded
    Cron.After(2.0, function()
        self:SyncFromNative()
    end)
end

function CyberpunkVR:OnOverlayOpen()
    self.isOverlayOpen = true

    -- Sync settings when opening menu
    if self.initialized then
        self:SyncFromNative()
    end
end

function CyberpunkVR:OnOverlayClose()
    self.isOverlayOpen = false
end

function CyberpunkVR:OnDraw()
    if self.isOverlayOpen then
        ImGui.Begin("Cyberpunk VR Settings")

        -- Connection Status
        if not self.initialized then
            ImGui.TextColored(1.0, 0.5, 0.0, 1.0, "Waiting for VR DLL...")
        else
            ImGui.TextColored(0.0, 1.0, 0.0, 1.0, "VR DLL Connected")
        end

        ImGui.Separator()

        -- Master Toggle
        local enabled, enabledChanged = ImGui.Checkbox("VR Enabled", self.settings.enabled)
        if enabledChanged then
            self.settings.enabled = enabled
            SafeCall("CyberpunkVR_SetEnabled", enabled)
        end

        ImGui.Separator()

        -- Rendering Settings
        ImGui.Text("Rendering")

        local ipd, ipdChanged = ImGui.SliderFloat("IPD (mm)", self.settings.ipd, 50.0, 80.0, "%.1f")
        if ipdChanged then
            self.settings.ipd = ipd
            SafeCall("CyberpunkVR_SetIPD", ipd)
        end
        ImGui.SameLine()
        if ImGui.Button("Reset##IPD") then
            self.settings.ipd = 64.0
            SafeCall("CyberpunkVR_SetIPD", 64.0)
        end

        local scale, scaleChanged = ImGui.SliderFloat("World Scale", self.settings.worldScale, 0.5, 2.0, "%.2f")
        if scaleChanged then
            self.settings.worldScale = scale
            SafeCall("CyberpunkVR_SetWorldScale", scale)
        end
        ImGui.SameLine()
        if ImGui.Button("Reset##Scale") then
            self.settings.worldScale = 1.0
            SafeCall("CyberpunkVR_SetWorldScale", 1.0)
        end

        -- UI Settings (placeholders for future implementation)
        ImGui.Separator()
        ImGui.Text("User Interface (Coming Soon)")
        ImGui.BeginDisabled()
        self.settings.uiDistance = ImGui.SliderFloat("UI Distance (m)", self.settings.uiDistance, 0.5, 5.0, "%.1f")
        ImGui.EndDisabled()

        -- Input Settings (placeholder)
        ImGui.Separator()
        ImGui.Text("Input (Coming Soon)")
        ImGui.BeginDisabled()
        self.settings.decoupleAim = ImGui.Checkbox("Decoupled Aiming", self.settings.decoupleAim)
        ImGui.EndDisabled()

        -- Debug
        ImGui.Separator()
        self.settings.debugMode = ImGui.Checkbox("Debug Logging", self.settings.debugMode)

        -- Info
        ImGui.Separator()
        ImGui.TextColored(0.5, 0.5, 0.5, 1.0, "Changes apply immediately")
        ImGui.TextColored(0.5, 0.5, 0.5, 1.0, "IPD: Inter-Pupillary Distance")

        ImGui.End()
    end
end

function CyberpunkVR:OnShutdown()
    print("[CyberpunkVR] Lua Module Shutdown")
end

return CyberpunkVR
