// NYX internal injector - LoadLibrary injection into RobloxPlayerBeta.exe
// usage: NyxInjector.exe [path\to\NyxInternal.dll]
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <cstdarg>

bool ManualMap(DWORD pid, const wchar_t* dllPath);

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

static void Out(const char* fmt, ...)
{
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    FILE* f = nullptr;
    fopen_s(&f, "nyx_injector.log", "a");
    if (f) { fputs(buf, f); fputc('\n', f); fclose(f); }
}

static int Run(int argc, wchar_t** argv);

int wmain(int argc, wchar_t** argv)
{
    int rc = Run(argc, argv);
    Out("done - press enter to close");
    getchar();
    return rc;
}

static int Run(int argc, wchar_t** argv)
{
    wchar_t dllPath[MAX_PATH];
    if (argc > 1) wcscpy_s(dllPath, argv[1]);
    else wcscpy_s(dllPath, L"NyxInternal.dll");

    // make path absolute
    wchar_t full[MAX_PATH];
    if (!GetFullPathNameW(dllPath, MAX_PATH, full, nullptr)) { Out("bad dll path\n"); return 1; }
    if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) { Out("dll not found: %ls\n", full); return 1; }

    EnableDebugPriv();

    DWORD pid = FindPid(L"RobloxPlayerBeta.exe");
    if (!pid) pid = FindPid(L"RobloxPlayer.exe");
    if (!pid) { Out("roblox not running\n"); return 1; }
    Out("roblox pid=%lu\n", pid);

    // manual map first - LoadLibrary gets unmapped by Byfron
    if (ManualMap(pid, full))
    {
        Out("manualmap ok - check %%TEMP%%\\nyx_internal.log for dll output");
        return 0;
    }
    Out("manualmap failed, trying LoadLibrary fallback...");

    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { Out("openprocess failed err=%lu\n", GetLastError()); return 1; }

    SIZE_T len = (wcslen(full) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { Out("alloc failed\n"); CloseHandle(proc); return 1; }
    if (!WriteProcessMemory(proc, remote, full, len, nullptr)) { Out("write failed\n"); CloseHandle(proc); return 1; }

    // LoadLibraryW via remote thread
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th) { Out("createremotethread failed err=%lu\n", GetLastError()); CloseHandle(proc); return 1; }
    WaitForSingleObject(th, 10000);
    DWORD code = 0; GetExitCodeThread(th, &code);
    Out(code ? "injected ok (module 0x%lx)\n" : "inject failed (dllmain returned 0 or load blocked)\n", code);

    // verify the dll actually shows up in the process module list -
    // Byfron can make LoadLibrary succeed then immediately unmap it
    bool loaded = false;
    HANDLE msnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (msnap != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W me{ sizeof(me) };
        if (Module32FirstW(msnap, &me))
            do {
                if (wcsstr(me.szModule, L"NyxInternal")) { loaded = true; break; }
            } while (Module32NextW(msnap, &me));
        CloseHandle(msnap);
    }
    Out("module check: %s\n", loaded ? "NyxInternal.dll IS loaded" : "NyxInternal.dll NOT in module list (blocked/unmapped)");
    CloseHandle(th); VirtualFreeEx(proc, remote, 0, MEM_RELEASE); CloseHandle(proc);
    return (code && loaded) ? 0 : 1;
}
