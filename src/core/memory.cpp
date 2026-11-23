#include "memory.h"
#include <TlHelp32.h>
#include <Psapi.h>

Memory g_mem;

bool Memory::Attach(const wchar_t* processName)
{
    Detach();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe{ sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe))
    {
        do {
            if (_wcsicmp(pe.szExeFile, processName) == 0) { pid = pe.th32ProcessID; found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (!found) return false;

    hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) return false;

    // module base
    HANDLE msnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (msnap != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W me{ sizeof(me) };
        if (Module32FirstW(msnap, &me)) { base = (uintptr_t)me.modBaseAddr; moduleSize = me.modBaseSize; }
        CloseHandle(msnap);
    }
    if (base == 0)
    {
        HMODULE mods[1024]; DWORD needed = 0;
        if (EnumProcessModules(hProcess, mods, sizeof(mods), &needed))
        {
            base = (uintptr_t)mods[0];
            MODULEINFO mi{};
            if (GetModuleInformation(hProcess, mods[0], &mi, sizeof(mi))) moduleSize = mi.SizeOfImage;
        }
    }
    return base != 0;
}

void Memory::Detach()
{
    if (hProcess) { CloseHandle(hProcess); hProcess = nullptr; }
    pid = 0; base = 0; moduleSize = 0;
}

std::string Memory::ReadString(uintptr_t address, size_t maxLen) const
{
    std::string out;
    out.reserve(64);
    for (size_t i = 0; i < maxLen; i++)
    {
        char c = Read<char>(address + i);
        if (c == 0) break;
        out += c;
    }
    return out;
}

std::string Memory::ReadRobloxString(uintptr_t address) const
{
    if (address == 0) return "";
    int len = Read<int>(address + 0x18);
    uintptr_t ptr = address;
    if (len >= 16) ptr = ReadPtr(address);
    if (ptr == 0 || len <= 0 || len > 200) return "";
    std::string out;
    out.resize(len);
    SIZE_T got = 0;
    ReadProcessMemory(hProcess, (LPCVOID)ptr, out.data(), len, &got);
    out.resize(got);
    while (!out.empty() && out.back() == 0) out.pop_back();
    return out;
}
