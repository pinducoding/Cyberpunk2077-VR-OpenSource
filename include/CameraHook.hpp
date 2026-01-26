#pragma once
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/entIPlacedComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/BaseCameraComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/WorldTransform.hpp>

class CameraHook
{
public:
    CameraHook();
    ~CameraHook();

    bool InstallHooks();
    
    // The hook target
    static void __fastcall OnCameraUpdate(RED4ext::ent::BaseCameraComponent* aComponent);

    // Trampoline (Original function)
    static inline void (*Real_CameraUpdate)(RED4ext::ent::BaseCameraComponent*) = nullptr;

private:
    bool m_hooksInstalled = false;
};