#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

// Fetches Roblox offsets at runtime from imtheo.lol (theo's dumper).
// Same source the previous C# build used.
namespace offsets
{
    bool Fetch();
    uintptr_t Get(const char* cls, const char* field);
    bool Ready();
    const std::string& ClientVersion();
}
