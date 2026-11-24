#pragma once
#include "memory.h"
#include <string>
#include <vector>
#include <cstdint>

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Vec2 { float x = -1, y = -1; };
struct Matrix4 { float m[16]; };

class Instance
{
public:
    uintptr_t addr = 0;
    Instance() = default;
    explicit Instance(uintptr_t a) : addr(a) {}
    bool valid() const { return addr > 0x10000; }
    explicit operator bool() const { return valid(); }

    std::string GetName() const;
    std::string GetClassName() const;
    std::vector<Instance> GetChildren() const;
    Instance FindFirstChild(const std::string& name) const;
    Instance FindFirstChildOfClass(const std::string& cls) const;

    Instance GetCharacter() const;      // Player -> ModelInstance
    Instance GetLocalPlayer() const;    // Players -> LocalPlayer
    Instance GetHumanoid() const;       // Character -> Humanoid

    float GetHealth() const;
    float GetMaxHealth() const;
    uintptr_t GetTeam() const;

    // replicated attributes (name -> string value); used for kit ESP etc.
    // debug=true logs each resolution stage once (diagnostics)
    std::vector<std::pair<std::string, std::string>> GetAttributes(bool debug = false) const;

    // BasePart helpers
    static Vec3 PartPosition(uintptr_t partAddr);
    static Vec3 PartSize(uintptr_t partAddr);
    static float PartTransparency(uintptr_t partAddr);
};

namespace sdk
{
    // resolved once per attach
    struct World
    {
        uintptr_t visualEngine = 0;
        uintptr_t dataModel = 0;
        uintptr_t workspace = 0;
        uintptr_t players = 0;
        uintptr_t camera = 0;
        uintptr_t localPlayer = 0;
    };

    bool Resolve();          // call after attach + offsets fetch; retries internally
    const World& Get();
    std::vector<Instance> GetPlayers();
    bool WorldToScreen(const Vec3& world, Vec2& out);
    Vec3 LocalPlayerPos();
}
