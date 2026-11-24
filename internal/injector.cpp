// NYX internal injector - LoadLibrary injection into RobloxPlayerBeta.exe
// usage: NyxInjector.exe [path\to\NyxInternal.dll]
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>

static bool EnableDebugPriv()
{
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    LUID luid;
    bool ok = LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid) != 0;
    if (ok)
    {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ok = AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr) != 0;
    }
    CloseHandle(tok);
    return ok;
}

static DWORD FindPid(const wchar_t* name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe))
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return pid;
}

int wmain(int argc, wchar_t** argv)
{
    wchar_t dllPath[MAX_PATH];
    if (argc > 1) wcscpy_s(dllPath, argv[1]);
    else wcscpy_s(dllPath, L"NyxInternal.dll");

    // make path absolute
    wchar_t full[MAX_PATH];
    if (!GetFullPathNameW(dllPath, MAX_PATH, full, nullptr)) { printf("bad dll path\n"); return 1; }
    if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) { printf("dll not found: %ls\n", full); return 1; }

    EnableDebugPriv();

    DWORD pid = FindPid(L"RobloxPlayerBeta.exe");
    if (!pid) pid = FindPid(L"RobloxPlayer.exe");
    if (!pid) { printf("roblox not running\n"); return 1; }
    printf("roblox pid=%lu\n", pid);

    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { printf("openprocess failed err=%lu\n", GetLastError()); return 1; }

    SIZE_T len = (wcslen(full) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { printf("alloc failed\n"); CloseHandle(proc); return 1; }
    if (!WriteProcessMemory(proc, remote, full, len, nullptr)) { printf("write failed\n"); CloseHandle(proc); return 1; }

    // LoadLibraryW via remote thread
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th) { printf("createremotethread failed err=%lu\n", GetLastError()); CloseHandle(proc); return 1; }
    WaitForSingleObject(th, 10000);
    DWORD code = 0; GetExitCodeThread(th, &code);
    printf(code ? "injected ok (module 0x%lx)\n" : "inject failed\n", code);
    CloseHandle(th); VirtualFreeEx(proc, remote, 0, MEM_RELEASE); CloseHandle(proc);
    return code ? 0 : 1;
}
