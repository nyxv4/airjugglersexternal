// manual-map DLL injection - reflective load, no LoadLibrary, no
// module list entry. this is what survives Byfron where plain
// CreateRemoteThread+LoadLibrary gets unmapped instantly.
#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include <cstdint>

// hyperion kills remotely-created threads in robloxplayerbeta before they
// run any user code (exit code 0xC000071C, flag never set). the standard
// bypass is thread hijacking: suspend an existing roblox thread, point it
// at our stub, and let it run the payload then jump back - no new thread.
struct HijackCtx { DWORD tid; HANDLE th; CONTEXT ctx; };

// returns 1 = entry ran, 0 = armed but timed out (stubs still live, do
// NOT fall back to remote thread - would double-run entry), -1 = could
// not arm any thread (safe to try other methods)
static int RunViaHijack(HANDLE proc, DWORD pid, uintptr_t rbase,
    uintptr_t entry, uintptr_t flagAddr, const std::vector<uintptr_t>& tlsCbs)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;

    std::vector<HijackCtx> threads;
    THREADENTRY32 te{ sizeof(te) };
    if (Thread32First(snap, &te))
        do {
            if (te.th32OwnerProcessID == pid)
                threads.push_back({ te.th32ThreadID, nullptr, {} });
        } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    printf("[mm] hijack: %zu threads in target\n", threads.size());

    // arm up to 8 threads. threads blocked in long kernel waits (sleep,
    // vsync) won't reach user mode for a while, so arm several and let
    // whichever wakes first claim the job via a lock cmpxchg on flagAddr.
    // flag layout: dword @flagAddr = claim bits (bit0 = claimed),
    // byte @flagAddr+8 = stage (2 = inside entry, 3 = entry returned)
    struct Arm { HANDLE th; CONTEXT ctx; LPVOID stub; };
    std::vector<Arm> arms;
    int armed = 0;

    for (auto& t : threads)
    {
        if (armed >= 8) break;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                               THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t.tid);
        if (!th) continue;
        if (SuspendThread(th) == (DWORD)-1) { CloseHandle(th); continue; }
        CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_FULL;
        if (!GetThreadContext(th, &ctx) || !ctx.Rip) { ResumeThread(th); CloseHandle(th); continue; }

        // stub: save all regs, claim via lock bts, stage=2, call tlsCbs
        // + entry, stage=3, restore regs, jmp original Rip
        uint8_t sc[640]; int n = 0;
        auto emit = [&](std::initializer_list<uint8_t> b) { for (auto x : b) sc[n++] = x; };
        auto emitMovRax = [&](uint64_t v) { emit({0x48,0xB8}); memcpy(sc+n,&v,8); n+=8; };
        emit({0x50,0x51,0x52,0x53,0x55,0x56,0x57});           // push rax..rdi
        emit({0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53});     // push r8..r11
        emit({0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57});     // push r12..r15
        emit({0x9C});                                          // pushfq
        emit({0x48,0x89,0xE5});                                // mov rbp,rsp
        emit({0x48,0x83,0xE4,0xF0});                           // and rsp,-16
        emit({0x48,0x81,0xEC,0xE0,0x00,0x00,0x00});             // sub rsp,0xE0
        // save nonvolatile xmm6-xmm15 - hijacked threads (render/game
        // code) keep live values there; clobbering them crashes roblox
        // movdqa [rsp+0x20+i*0x10], xmm(6+i)  /  movdqa xmm,[rsp+...]
        auto emitXmm = [&](int xmm, int off, bool store) {
            emit({0x66});
            if (xmm >= 8) emit({0x44});                        // REX.R
            emit({0x0F, (uint8_t)(store ? 0x7F : 0x6F)});
            emit({(uint8_t)(0x84 | ((xmm & 7) << 3)), 0x24});   // modrm+sib [rsp+disp32]
            int32_t d = off; emit({(uint8_t)d,(uint8_t)(d>>8),(uint8_t)(d>>16),(uint8_t)(d>>24)});
        };
        for (int i = 0; i < 10; i++) emitXmm(6 + i, 0x20 + i * 0x10, true);
        // lock bts dword[flagAddr],0 ; jc loser (0F 82 rel32, patch later)
        emitMovRax(flagAddr);
        emit({0xBA,0x01,0x00,0x00,0x00});                      // mov edx,1
        emit({0xF0,0x0F,0xAB,0x10});                           // lock bts [rax],edx
        int jcPos = n; emit({0x0F,0x82,0,0,0,0});              // jc -> loser
        emitMovRax(flagAddr + 8); emit({0xC6,0x00,0x02});      // stage=2
        auto emitDllCall = [&](uintptr_t fn) {
            emit({0x48,0xB9}); { uint64_t v = rbase; memcpy(sc+n,&v,8); n+=8; }
            emit({0xBA,0x01,0x00,0x00,0x00});
            emit({0x4D,0x31,0xC0});
            emitMovRax(fn); emit({0xFF,0xD0});
        };
        for (uintptr_t cb : tlsCbs) emitDllCall(cb);
        emitDllCall(entry);
        emitMovRax(flagAddr + 8); emit({0xC6,0x00,0x03});      // stage=3
        int loserPos = n;
        // patch jc rel32: target = loserPos, src end = jcPos+6
        int32_t rel = loserPos - (jcPos + 6);
        memcpy(sc + jcPos + 2, &rel, 4);
        for (int i = 9; i >= 0; i--) emitXmm(6 + i, 0x20 + i * 0x10, false); // restore xmm15..xmm6
        emit({0x48,0x89,0xEC});                                // mov rsp,rbp
        emit({0x9D});                                          // popfq
        emit({0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C});       // pop r15..r12
        emit({0x41,0x5B,0x41,0x5A,0x41,0x59,0x41,0x58});       // pop r11..r8
        emit({0x5F,0x5E,0x5D,0x5B,0x5A,0x59,0x58});            // pop rdi..rax
        emitMovRax(ctx.Rip); emit({0xFF,0xE0});                // jmp origRip

        LPVOID stubMem = VirtualAllocEx(proc, nullptr, sizeof(sc), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!stubMem) { ResumeThread(th); CloseHandle(th); continue; }
        WriteProcessMemory(proc, stubMem, sc, n, nullptr);
        DWORD old = 0;
        VirtualProtectEx(proc, stubMem, sizeof(sc), PAGE_EXECUTE_READ, &old);

        CONTEXT nc = ctx;
        nc.Rip = (DWORD64)stubMem;
        nc.ContextFlags = CONTEXT_CONTROL;
        if (!SetThreadContext(th, &nc)) { ResumeThread(th); VirtualFreeEx(proc, stubMem, 0, MEM_RELEASE); CloseHandle(th); continue; }
        ResumeThread(th);
        arms.push_back({ th, ctx, stubMem });
        armed++;
    }
    printf("[mm] hijack armed %d threads\n", armed);
    if (!armed) return -1;

    // poll stage byte up to 20s - whichever armed thread wakes first wins
    bool ok = false;
    for (int i = 0; i < 400; i++)
    {
        uint8_t stage = 0;
        ReadProcessMemory(proc, (void*)(flagAddr + 8), &stage, 1, nullptr);
        if (stage == 3) { ok = true; break; }
        Sleep(50);
    }
    printf("[mm] hijack result: %s\n", ok ? "entry returned" : "timeout");
    // NOTE: stub pages are intentionally left allocated - armed threads
    // that haven't woken yet still need them to resume cleanly
    for (auto& a : arms) CloseHandle(a.th);
    return ok ? 1 : 0;
}

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
    if (!remote) { printf("[mm] ensuremod alloc fail %ls\n", name); return 0; }
    WriteProcessMemory(proc, remote, full, len, nullptr);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (th) { WaitForSingleObject(th, 8000); DWORD rc = 0; GetExitCodeThread(th, &rc); printf("[mm] ensuremod loadlib %ls rc=0x%lx\n", name, rc); CloseHandle(th); }
    else printf("[mm] ensuremod thread fail %ls err=%lu\n", name, GetLastError());
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    uintptr_t b = RemoteModuleBase(pid, name);
    printf("[mm] ensuremod %ls -> 0x%llx (path %ls)\n", name, (unsigned long long)b, full);
    return b;
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

    // allocate RW first - sections get real protections after all writes.
    // a giant RWX image is the #1 thing memory scanners flag
    LPVOID remoteBase = VirtualAllocEx(proc, nullptr, imgSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
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
    printf("[mm] rbase=0x%llx pref=0x%llx delta=%lld imgSize=0x%lx entryRva=0x%lx\n",
        (unsigned long long)rbase, (unsigned long long)prefBase, (long long)delta, imgSize, entryRva);
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
            if (!oftOff || !ftOff) { printf("[mm] thunk rva->off failed %s\n", dllName); continue; }

            // resolve an export to its REMOTE address. GetProcAddress
            // follows forwarders (kernel32!CreateFileA -> KernelBase) so
            // the address may live in a DIFFERENT module than depRemote -
            // find the real owning module locally, then its remote twin.
            auto resolveRemote = [&](FARPROC p) -> uintptr_t {
                if (!p) return 0;
                HMODULE owner = nullptr;
                if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        (LPCSTR)p, &owner) || !owner)
                    return depRemote + ((uintptr_t)p - localBase);
                char path[MAX_PATH];
                GetModuleFileNameA(owner, path, MAX_PATH);
                const char* base = strrchr(path, '\\');
                base = base ? base + 1 : path;
                uintptr_t rbase2 = RemoteModuleBase(pid, NarrowToWide(base).c_str());
                if (!rbase2) rbase2 = EnsureRemoteModule(proc, pid, NarrowToWide(base).c_str());
                if (!rbase2) return 0;
                return rbase2 + ((uintptr_t)p - (uintptr_t)owner);
            };

            int resolved = 0, total = 0;
            auto* origThk = (IMAGE_THUNK_DATA64*)(file.data() + oftOff);
            for (int i = 0; origThk[i].u1.AddressOfData; i++)
            {
                total++;
                uintptr_t remoteFunc = 0;
                if (origThk[i].u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                {
                    remoteFunc = resolveRemote(GetProcAddress(local, (LPCSTR)(origThk[i].u1.Ordinal & 0xFFFF)));
                }
                else
                {
                    DWORD hintRva = (DWORD)origThk[i].u1.AddressOfData;
                    DWORD hintOff = rva2off2(hintRva);
                    if (!hintOff) continue;
                    auto* ibn = (IMAGE_IMPORT_BY_NAME*)(file.data() + hintOff);
                    remoteFunc = resolveRemote(GetProcAddress(local, ibn->Name));
                    printf("[mm]   %s -> 0x%llx\n", ibn->Name, (unsigned long long)remoteFunc);
                }
                if (remoteFunc)
                {
                    WriteProcessMemory(proc, (void*)(rbase + ftRva + i * 8), &remoteFunc, 8, nullptr);
                    resolved++;
                }
            }
            printf("[mm] imports %s: dep=0x%llx %d/%d resolved\n", dllName,
                (unsigned long long)depRemote, resolved, total);
            // verify: read back IAT slots remotely
            for (int i = 0; i < 8; i++)
            {
                uintptr_t iatVal = 0;
                ReadProcessMemory(proc, (void*)(rbase + ftRva + i * 8), &iatVal, 8, nullptr);
                printf("[mm]   iat[%d] remote=0x%llx\n", i, (unsigned long long)iatVal);
            }
        }
    }

    // apply real section protections now that all writes are done -
    // code RX, data RW, headers R. no RWX anywhere
    {
        DWORD old = 0;
        VirtualProtectEx(proc, remoteBase, nt->OptionalHeader.SizeOfHeaders, PAGE_READONLY, &old);
        auto* s = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        {
            bool e = s->Characteristics & IMAGE_SCN_MEM_EXECUTE;
            bool w = s->Characteristics & IMAGE_SCN_MEM_WRITE;
            bool r = s->Characteristics & IMAGE_SCN_MEM_READ;
            DWORD prot = e ? (w ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ)
                           : (w ? PAGE_READWRITE : (r ? PAGE_READONLY : PAGE_NOACCESS));
            SIZE_T sz = s->Misc.VirtualSize > s->SizeOfRawData ? s->Misc.VirtualSize : s->SizeOfRawData;
            VirtualProtectEx(proc, (void*)(rbase + s->VirtualAddress), sz, prot, &old);
        }
        printf("[mm] section protections applied\n");
    }

    // register the image in the PEB module lists - DISABLED by default:
    // a partial LDR_DATA_TABLE_ENTRY crashes loader walks. opt back in
    // with MM_PEB_ENTRY=1 once the entry is fully populated
    if (getenv("MM_PEB_ENTRY"))
    {
        typedef LONG NTSTATUS;
        struct PBI { PVOID Reserved1; PVOID PebBaseAddress; PVOID Reserved2[2]; ULONG_PTR UniqueProcessId; PVOID Reserved3; };
        auto NtQIP = (NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG))
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
        PBI pbi{};
        if (NtQIP && NtQIP(proc, 0, &pbi, sizeof(pbi), nullptr) == 0 && pbi.PebBaseAddress)
        {
            uintptr_t peb = (uintptr_t)pbi.PebBaseAddress;
            uintptr_t ldr = 0;
            ReadProcessMemory(proc, (void*)(peb + 0x18), &ldr, 8, nullptr);
            if (ldr)
            {
                // LDR_DATA_TABLE_ENTRY: links@0/0x10/0x20, base@0x30,
                // entry@0x38, size@0x40, FullDllName@0x48, BaseDllName@0x58
                LPVOID entMem = VirtualAllocEx(proc, nullptr, 0x400, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (entMem)
                {
                    uintptr_t ent = (uintptr_t)entMem;
                    // unicode strings live at ent+0x100
                    wchar_t fullPath[] = L"C:\\Windows\\System32\\NyxInternal.dll";
                    wchar_t baseName[] = L"NyxInternal.dll";
                    WriteProcessMemory(proc, (void*)(ent + 0x100), fullPath, sizeof(fullPath), nullptr);
                    WriteProcessMemory(proc, (void*)(ent + 0x180), baseName, sizeof(baseName), nullptr);
                    uint64_t vals[16] = {};
                    vals[6] = rbase;                 // DllBase @0x30
                    vals[7] = rbase + entryRva;       // EntryPoint @0x38
                    vals[8] = imgSize;                // SizeOfImage @0x40
                    WriteProcessMemory(proc, (void*)ent, vals, sizeof(vals), nullptr);
                    // UNICODE_STRING FullDllName @0x48 / BaseDllName @0x58:
                    // len u16, maxlen u16, pad u32, buf u64
                    auto putUS = [&](int q, USHORT len, uintptr_t buf) {
                        uint8_t b[16] = {};
                        *(USHORT*)b = len; *(USHORT*)(b + 2) = len + 2;
                        *(uintptr_t*)(b + 8) = buf;
                        WriteProcessMemory(proc, (void*)(ent + q * 8), b, 16, nullptr);
                    };
                    putUS(9, (USHORT)(wcslen(fullPath) * 2), ent + 0x100);   // @0x48
                    putUS(11, (USHORT)(wcslen(baseName) * 2), ent + 0x180);  // @0x58

                    // insert at head of all three lists: InLoadOrder@ldr+0x10
                    // (entry field +0x00), InMemoryOrder@ldr+0x20 (field +0x10),
                    // InInitOrder@ldr+0x30 (field +0x20)
                    struct L { DWORD listOff; DWORD fieldOff; } lists[3] = {
                        {0x10, 0x00}, {0x20, 0x10}, {0x30, 0x20}
                    };
                    for (auto& L : lists)
                    {
                        uintptr_t head = ldr + L.listOff;
                        uint64_t first = 0;
                        ReadProcessMemory(proc, (void*)head, &first, 8, nullptr);
                        uintptr_t linkVA = ent + L.fieldOff;
                        // our Flink/Blink
                        WriteProcessMemory(proc, (void*)linkVA, &first, 8, nullptr);
                        WriteProcessMemory(proc, (void*)(linkVA + 8), &head, 8, nullptr);
                        if (first) WriteProcessMemory(proc, (void*)(first + 8), &linkVA, 8, nullptr);
                        WriteProcessMemory(proc, (void*)head, &linkVA, 8, nullptr);
                    }
                    printf("[mm] peb module list entry added ent=0x%llx\n", (unsigned long long)ent);
                }
            }
        }
        else printf("[mm] peb lookup failed\n");
    }

    // NOTE: headers are NOT wiped before entry - CRT startup reads its own
    // PE headers (security cookie / load config); wiping them hangs init.
    // wipe after entry returns instead (still anon memory to scanners)

    // TLS callbacks must run BEFORE the entry point - CRT dlls use them
    // for per-thread/per-process init; skipping them hangs CRTStartup
    std::vector<uintptr_t> tlsCbs;
    {
        DWORD tlsRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress;
        if (tlsRva)
        {
            // IMAGE_TLS_DIRECTORY64 fields are VAs - convert to remote addrs
            auto rd64 = [&](DWORD rva) -> uint64_t {
                uint64_t v = 0;
                ReadProcessMemory(proc, (void*)(rbase + rva), &v, 8, nullptr);
                return v;
            };
            uint64_t cbsVa = rd64(tlsRva + 0x18); // AddressOfCallBacks
            if (cbsVa)
            {
                uintptr_t cbsRemote = rbase + (uintptr_t)(cbsVa - prefBase);
                for (int i = 0; i < 16; i++)
                {
                    uint64_t cb = 0;
                    ReadProcessMemory(proc, (void*)(cbsRemote + i * 8), &cb, 8, nullptr);
                    if (!cb) break;
                    tlsCbs.push_back(rbase + (uintptr_t)(cb - prefBase));
                }
            }
        }
        printf("[mm] tls callbacks: %zu\n", tlsCbs.size());
    }

    // debug flag: shellcode writes stage markers here so we can tell
    // whether it ran at all vs hung inside the entry call
    uint8_t* flagRemote = nullptr;
    LPVOID flagMem = VirtualAllocEx(proc, nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uintptr_t flagAddr = (uintptr_t)flagMem;

    // call entry (and TLS callbacks first) via shellcode. the prologue
    // aligns the stack to the ABI (16-byte) - without it callees using
    // movaps fault instantly
    uintptr_t entry = rbase + entryRva;
    uint8_t sc[512]; int n = 0;
    if (getenv("MM_LOOP_TEST"))
    {
        // diagnostic: write flag then spin forever - proves the remote
        // thread executes our shellcode at all
        sc[n++] = 0x48; sc[n++] = 0xB8; memcpy(sc + n, &flagAddr, 8); n += 8;
        sc[n++] = 0xC6; sc[n++] = 0x00; sc[n++] = 0x01;
        sc[n++] = 0xEB; sc[n++] = 0xFE;
    }
    else
    {
    sc[n++] = 0x55;                                   // push rbp
    sc[n++] = 0x48; sc[n++] = 0x89; sc[n++] = 0xE5;   // mov rbp, rsp
    sc[n++] = 0x48; sc[n++] = 0x83; sc[n++] = 0xE4; sc[n++] = 0xF0; // and rsp,-16
    sc[n++] = 0x48; sc[n++] = 0x83; sc[n++] = 0xEC; sc[n++] = 0x20; // sub rsp,0x20
    // mov rax,flagAddr ; mov byte[rax],1
    sc[n++] = 0x48; sc[n++] = 0xB8; memcpy(sc + n, &flagAddr, 8); n += 8;
    sc[n++] = 0xC6; sc[n++] = 0x00; sc[n++] = 0x01;
    auto emitCall = [&](uintptr_t fn) {
        sc[n++] = 0x48; sc[n++] = 0xB9; memcpy(sc + n, &rbase, 8); n += 8; // mov rcx,rbase
        sc[n++] = 0xBA; sc[n++] = 1; sc[n++] = 0; sc[n++] = 0; sc[n++] = 0; // mov edx,1
        sc[n++] = 0x4D; sc[n++] = 0x31; sc[n++] = 0xC0;                     // xor r8,r8
        sc[n++] = 0x48; sc[n++] = 0xB8; memcpy(sc + n, &fn, 8); n += 8;      // mov rax,fn
        sc[n++] = 0xFF; sc[n++] = 0xD0;                                     // call rax
    };
    for (uintptr_t cb : tlsCbs) emitCall(cb);
    emitCall(entry);
    // mov rsp,rbp ; pop rbp ; ret
    sc[n++] = 0x48; sc[n++] = 0x89; sc[n++] = 0xEC;
    sc[n++] = 0x5D;
    sc[n++] = 0xC3;
    // unreachable: ret2 -> ExitThread(0)
    sc[n++] = 0x31; sc[n++] = 0xC9;                   // xor ecx,ecx
    sc[n++] = 0x48; sc[n++] = 0xB8; { uintptr_t et = (uintptr_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitThread"); memcpy(sc + n, &et, 8); n += 8; } // mov rax,ExitThread
    sc[n++] = 0xFF; sc[n++] = 0xE0;                   // jmp rax
    } // end !MM_LOOP_TEST

    // hyperion blocks CreateRemoteThread in roblox - try thread hijack
    // first (runs payload on an existing roblox thread, then resumes it)
    int hijackRes = -1;
    if (!getenv("MM_NO_HIJACK"))
        hijackRes = RunViaHijack(proc, pid, rbase, entry, flagAddr, tlsCbs);
    printf("[mm] hijack result: %d\n", hijackRes);

    DWORD rc = (hijackRes == 1) ? 1 : 0;
    HANDLE th = nullptr;
    if (hijackRes != 1 && hijackRes != 0)
    {
        LPVOID scRemote = VirtualAllocEx(proc, nullptr, sizeof(sc), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        WriteProcessMemory(proc, scRemote, sc, n, nullptr);
        th = CreateRemoteThread(proc, nullptr, 0, (LPTHREAD_START_ROUTINE)scRemote, nullptr, 0, nullptr);
        if (!th) { printf("entry thread failed %lu\n", GetLastError()); CloseHandle(proc); return false; }
        WaitForSingleObject(th, 10000);
        GetExitCodeThread(th, &rc);
        VirtualFreeEx(proc, scRemote, 0, MEM_RELEASE);
    }
    uint8_t flag = 0;
    ReadProcessMemory(proc, flagMem, &flag, 1, nullptr);
    printf("manualmap entry returned %lu (shellcode flag=%u)\n", rc, flag);

    // now safe to wipe headers - entry already ran. headers are READONLY
    // now, so flip to RW, zero, flip back
    {
        DWORD old = 0;
        VirtualProtectEx(proc, remoteBase, nt->OptionalHeader.SizeOfHeaders, PAGE_READWRITE, &old);
        std::vector<uint8_t> zeros(nt->OptionalHeader.SizeOfHeaders, 0);
        WriteProcessMemory(proc, remoteBase, zeros.data(), zeros.size(), nullptr);
        VirtualProtectEx(proc, remoteBase, nt->OptionalHeader.SizeOfHeaders, PAGE_READONLY, &old);
    }
    if (th) CloseHandle(th);
    if (flagMem) VirtualFreeEx(proc, flagMem, 0, MEM_RELEASE);
    CloseHandle(proc);
    return rc != 0;
}
