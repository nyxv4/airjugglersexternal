// manual-map DLL injection - reflective load, no LoadLibrary, no
// module list entry. this is what survives Byfron where plain
// CreateRemoteThread+LoadLibrary gets unmapped instantly.
#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include <cstdint>

static uintptr_t RemoteModuleBase(DWORD pid, const wchar_t* name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W me{ sizeof(me) };
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me))
        do {
            if (_wcsicmp(me.szModule, name) == 0) { base = (uintptr_t)me.modBaseAddr; break; }
        } while (Module32NextW(snap, &me));
    CloseHandle(snap);
    return base;
}

static std::string WideToNarrow(const wchar_t* w)
{
    std::string s; while (*w) { s += (char)*w; w++; } return s;
}

static std::wstring NarrowToWide(const char* n)
{
    std::wstring s; while (*n) { s += (wchar_t)*n; n++; } return s;
}

// load a system dll into the target via LoadLibraryW (allowed for
// Microsoft-signed modules), return its remote base
static uintptr_t EnsureRemoteModule(HANDLE proc, DWORD pid, const wchar_t* name)
{
    uintptr_t base = RemoteModuleBase(pid, name);
    if (base) return base;

    wchar_t full[MAX_PATH];
    if (!GetSystemDirectoryW(full, MAX_PATH)) return 0;
    wcscat_s(full, L"\\"); wcscat_s(full, name);

    SIZE_T len = (wcslen(full) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return 0;
    WriteProcessMemory(proc, remote, full, len, nullptr);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (th) { WaitForSingleObject(th, 5000); CloseHandle(th); }
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    return RemoteModuleBase(pid, name);
}

bool ManualMap(DWORD pid, const wchar_t* dllPath)
{
    // read dll
    HANDLE hf = CreateFileW(dllPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hf == INVALID_HANDLE_VALUE) { printf("dll open failed %lu\n", GetLastError()); return false; }
    DWORD fsize = GetFileSize(hf, nullptr);
    std::vector<uint8_t> file(fsize);
    DWORD got = 0; ReadFile(hf, file.data(), fsize, &got, nullptr); CloseHandle(hf);
    if (got != fsize) return false;

    auto* dos = (IMAGE_DOS_HEADER*)file.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = (IMAGE_NT_HEADERS64*)(file.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    DWORD imgSize = nt->OptionalHeader.SizeOfImage;
    uintptr_t prefBase = nt->OptionalHeader.ImageBase;
    DWORD entryRva = nt->OptionalHeader.AddressOfEntryPoint;
    DWORD relocRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    DWORD relocSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
    DWORD impRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;

    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { printf("openprocess failed %lu\n", GetLastError()); return false; }

    // allocate + copy image
    LPVOID remoteBase = VirtualAllocEx(proc, nullptr, imgSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteBase) { printf("remote alloc failed\n"); CloseHandle(proc); return false; }
    uintptr_t rbase = (uintptr_t)remoteBase;

    // headers
    WriteProcessMemory(proc, remoteBase, file.data(), nt->OptionalHeader.SizeOfHeaders, nullptr);
    // sections
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
    {
        if (!sec->SizeOfRawData) continue;
        WriteProcessMemory(proc, (void*)(rbase + sec->VirtualAddress),
            file.data() + sec->PointerToRawData, sec->SizeOfRawData, nullptr);
    }

    // relocations
    int64_t delta = (int64_t)rbase - (int64_t)prefBase;
    if (delta && relocRva)
    {
        auto* blk = (IMAGE_BASE_RELOCATION*)(file.data() + 0); // rva->file offset needed; use section map below
        // walk relocation blocks via rva->offset mapping
        auto rva2off = [&](DWORD rva) -> DWORD {
            auto* s = IMAGE_FIRST_SECTION(nt);
            for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
                if (rva >= s->VirtualAddress && rva < s->VirtualAddress + s->Misc.VirtualSize)
                    return s->PointerToRawData + (rva - s->VirtualAddress);
            return rva < nt->OptionalHeader.SizeOfHeaders ? rva : 0;
        };
        DWORD off = rva2off(relocRva), end = off + relocSize;
        while (off && off < end)
        {
            auto* b = (IMAGE_BASE_RELOCATION*)(file.data() + off);
            DWORD cnt = (b->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
            auto* ent = (uint16_t*)(b + 1);
            for (DWORD i = 0; i < cnt; i++)
            {
                if ((ent[i] >> 12) == IMAGE_REL_BASED_DIR64)
                {
                    DWORD rva = b->VirtualAddress + (ent[i] & 0xFFF);
                    DWORD fo = rva2off(rva);
                    if (!fo) continue;
                    int64_t val = *(int64_t*)(file.data() + fo) + delta;
                    WriteProcessMemory(proc, (void*)(rbase + rva), &val, 8, nullptr);
                }
            }
            off += b->SizeOfBlock;
        }
    }

    // imports
    if (impRva)
    {
        DWORD impOff = 0;
        auto* s = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
            if (impRva >= s->VirtualAddress && impRva < s->VirtualAddress + s->Misc.VirtualSize)
                impOff = s->PointerToRawData + (impRva - s->VirtualAddress);
        if (!impOff) impOff = impRva;

        auto* desc = (IMAGE_IMPORT_DESCRIPTOR*)(file.data() + impOff);
        for (; desc->Name; desc++)
        {
            const char* dllName = (const char*)(file.data() + desc->Name); // name rva usually in headers region; fix via rva2off? names live in .rdata - use rva2off
            // proper: name is an RVA
            DWORD nameRva = desc->Name;
            DWORD nameOff = 0;
            auto* s2 = IMAGE_FIRST_SECTION(nt);
            for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s2++)
                if (nameRva >= s2->VirtualAddress && nameRva < s2->VirtualAddress + s2->Misc.VirtualSize)
                    nameOff = s2->PointerToRawData + (nameRva - s2->VirtualAddress);
            if (!nameOff) nameOff = nameRva;
            dllName = (const char*)(file.data() + nameOff);

            // remote base for this dependency
            std::wstring wname = NarrowToWide(dllName);
            uintptr_t depRemote = EnsureRemoteModule(proc, pid, wname.c_str());
            if (!depRemote)
            {
                // dependency not a system dll we can load - try snapshot match anyway
                depRemote = RemoteModuleBase(pid, wname.c_str());
            }
            if (!depRemote) { printf("import module missing: %s\n", dllName); continue; }

            // local copy for export RVA resolution
            HMODULE local = LoadLibraryA(dllName);
            if (!local) { printf("local load failed: %s\n", dllName); continue; }
            uintptr_t localBase = (uintptr_t)local;

            // thunk arrays: OriginalFirstThunk (names) or FirstThunk (ordinals)
            DWORD oftRva = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
            DWORD ftRva = desc->FirstThunk;
            auto rva2off2 = [&](DWORD rva) -> DWORD {
                auto* s3 = IMAGE_FIRST_SECTION(nt);
                for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s3++)
                    if (rva >= s3->VirtualAddress && rva < s3->VirtualAddress + s3->Misc.VirtualSize)
                        return s3->PointerToRawData + (rva - s3->VirtualAddress);
                return rva < nt->OptionalHeader.SizeOfHeaders ? rva : 0;
            };
            DWORD oftOff = rva2off2(oftRva), ftOff = rva2off2(ftRva);
            if (!oftOff || !ftOff) continue;

            auto* origThk = (IMAGE_THUNK_DATA64*)(file.data() + oftOff);
            for (int i = 0; origThk[i].u1.AddressOfData; i++)
            {
                uintptr_t remoteFunc = 0;
                if (origThk[i].u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                {
                    FARPROC p = GetProcAddress(local, (LPCSTR)(origThk[i].u1.Ordinal & 0xFFFF));
                    if (p) remoteFunc = depRemote + ((uintptr_t)p - localBase);
                }
                else
                {
                    DWORD hintRva = (DWORD)origThk[i].u1.AddressOfData;
                    DWORD hintOff = rva2off2(hintRva);
                    if (!hintOff) continue;
                    auto* ibn = (IMAGE_IMPORT_BY_NAME*)(file.data() + hintOff);
                    FARPROC p = GetProcAddress(local, ibn->Name);
                    if (p) remoteFunc = depRemote + ((uintptr_t)p - localBase);
                }
                if (remoteFunc)
                    WriteProcessMemory(proc, (void*)(rbase + ftRva + i * 8), &remoteFunc, 8, nullptr);
            }
        }
    }

    // wipe PE headers (opsec - nothing should scan them now)
    std::vector<uint8_t> zeros(nt->OptionalHeader.SizeOfHeaders, 0);
    WriteProcessMemory(proc, remoteBase, zeros.data(), zeros.size(), nullptr);

    // call DllMain via shellcode
    uintptr_t entry = rbase + entryRva;
    uint8_t sc[64]; int n = 0;
    // mov rcx, rbase ; mov edx,1 ; xor r8d,r8d ; mov rax,entry ; call rax ; ret
    sc[n++] = 0x48; sc[n++] = 0xB9; memcpy(sc + n, &rbase, 8); n += 8;
    sc[n++] = 0xBA; sc[n++] = 1; sc[n++] = 0; sc[n++] = 0; sc[n++] = 0;
    sc[n++] = 0x4D; sc[n++] = 0x31; sc[n++] = 0xC0;
    sc[n++] = 0x48; sc[n++] = 0xB8; memcpy(sc + n, &entry, 8); n += 8;
    sc[n++] = 0xFF; sc[n++] = 0xD0;
    sc[n++] = 0xC3;

    LPVOID scRemote = VirtualAllocEx(proc, nullptr, sizeof(sc), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(proc, scRemote, sc, n, nullptr);
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, (LPTHREAD_START_ROUTINE)scRemote, nullptr, 0, nullptr);
    if (!th) { printf("entry thread failed %lu\n", GetLastError()); CloseHandle(proc); return false; }
    WaitForSingleObject(th, 10000);
    DWORD rc = 0; GetExitCodeThread(th, &rc);
    printf("manualmap entry returned %lu\n", rc);
    CloseHandle(th);
    VirtualFreeEx(proc, scRemote, 0, MEM_RELEASE);
    CloseHandle(proc);
    return rc != 0;
}
