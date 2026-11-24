#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <cstdint>

class Memory
{
public:
    HANDLE hProcess = nullptr;
    DWORD pid = 0;
    uintptr_t base = 0;
    uintptr_t moduleSize = 0;

    bool Attach(const wchar_t* processName);
    void Detach();
    bool IsAttached() const { return hProcess != nullptr; }

    template <typename T>
    T Read(uintptr_t address) const
    {
        T buf{};
        SIZE_T read = 0;
        ReadProcessMemory(hProcess, (LPCVOID)address, &buf, sizeof(T), &read);
        return buf;
    }

    // bulk read - one RPM for a whole struct region
    bool Read(uintptr_t address, void* buf, size_t size) const
    {
        SIZE_T read = 0;
        return ReadProcessMemory(hProcess, (LPCVOID)address, buf, size, &read) && read == size;
    }

    template <typename T>
    bool Write(uintptr_t address, const T& value) const
    {
        SIZE_T written = 0;
        return WriteProcessMemory(hProcess, (LPVOID)address, &value, sizeof(T), &written) && written == sizeof(T);
    }

    uintptr_t ReadPtr(uintptr_t address) const { return Read<uintptr_t>(address); }

    std::string ReadString(uintptr_t address, size_t maxLen = 128) const;

    // Roblox string layout: std::string-like; if capacity >= 16 the chars live at *address
    std::string ReadRobloxString(uintptr_t address) const;
};

extern Memory g_mem;
