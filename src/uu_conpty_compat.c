#define _WIN32_WINNT 0x0A00
/* MinGW 12 gates the ConPTY declarations on Windows 10 RS5, independently
 * of the major Windows version selected by _WIN32_WINNT. */
#define NTDDI_VERSION 0x0A000006
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>
#include "uu_terminal_direct_io.h"

/* UU viewers use authenticated raw pipes. Other ConPTY users keep the existing
 * Wine input/size and raw console-output adapter. */
typedef struct { HPCON pc; HANDLE output; } console_route;
typedef struct { LPPROC_THREAD_ATTRIBUTE_LIST attributes; HPCON pc; } attribute_route;
static console_route consoles[32];
static attribute_route attributes[64];
static CRITICAL_SECTION routes_lock;
static HMODULE self_module;
static HANDLE raw_output;
static const char *inject_stage;

HRESULT WINAPI uurb_create(COORD, HANDLE, HANDLE, DWORD, HPCON *);
HRESULT WINAPI uurb_resize(HPCON, COORD);
void WINAPI uurb_close(HPCON);

static HANDLE console_output(HPCON pc)
{
    HANDLE output = NULL;
    EnterCriticalSection(&routes_lock);
    for (unsigned i = 0; i < ARRAYSIZE(consoles); i++)
        if (consoles[i].pc == pc) { output = consoles[i].output; break; }
    LeaveCriticalSection(&routes_lock);
    return output;
}

HRESULT WINAPI uurb_create(COORD size, HANDLE input, HANDLE output, DWORD flags, HPCON *pc)
{
    if (direct_controller()) return direct_create(size, input, output, flags, pc);
    HRESULT result = CreatePseudoConsole(size, input, output,
        flags & ~PSEUDOCONSOLE_INHERIT_CURSOR, pc);

    if (FAILED(result)) return result;
    /* The vendor closes its original pipe immediately after creation. Retain
     * our own reference until this console is closed. */
    HANDLE retained = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), output, GetCurrentProcess(),
        &retained, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        DWORD error = GetLastError();
        ClosePseudoConsole(*pc); *pc = NULL;
        return HRESULT_FROM_WIN32(error);
    }
    BOOL stored = FALSE;
    EnterCriticalSection(&routes_lock);
    for (unsigned i = 0; i < ARRAYSIZE(consoles); i++) if (!consoles[i].pc) {
        consoles[i].pc = *pc; consoles[i].output = retained; stored = TRUE; break;
    }
    LeaveCriticalSection(&routes_lock);
    if (!stored) { CloseHandle(retained); ClosePseudoConsole(*pc); *pc = NULL; return E_OUTOFMEMORY; }
    return result;
}

HRESULT WINAPI uurb_resize(HPCON pc, COORD size)
{
    return pc && pc == direct_io.pc ? direct_resize(size) : ResizePseudoConsole(pc, size);
}

void WINAPI uurb_close(HPCON pc)
{
    HANDLE retained = NULL;
    EnterCriticalSection(&routes_lock);
    for (unsigned i = 0; i < ARRAYSIZE(consoles); i++)
        if (consoles[i].pc == pc) {
            retained = consoles[i].output;
            memset(&consoles[i], 0, sizeof(consoles[i]));
        }
    for (unsigned i = 0; i < ARRAYSIZE(attributes); i++)
        if (attributes[i].pc == pc) memset(&attributes[i], 0, sizeof(attributes[i]));
    LeaveCriticalSection(&routes_lock);
    if (pc == direct_io.pc && pc) { direct_trace("close", 0); direct_close(); return; }
    if (retained) CloseHandle(retained);
    ClosePseudoConsole(pc);
}

DWORD WINAPI uurb_adopt_output(void *output)
{
    HANDLE handle = (HANDLE)output;
    if (GetFileType(handle) != FILE_TYPE_PIPE) return 0;
    if (raw_output) CloseHandle(raw_output);
    raw_output = handle;
    return 1;
}

static BOOL WINAPI route_attribute(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD flags,
    DWORD_PTR attribute, PVOID value, SIZE_T bytes, PVOID previous, PSIZE_T returned)
{
    BOOL result = UpdateProcThreadAttribute(list, flags, attribute, value,
        bytes, previous, returned);
    if (!result || attribute != PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE) return result;
    EnterCriticalSection(&routes_lock);
    unsigned slot = ARRAYSIZE(attributes);
    for (unsigned i = 0; i < ARRAYSIZE(attributes); i++) {
        if (attributes[i].attributes == list) { slot = i; break; }
        if (!attributes[i].attributes && slot == ARRAYSIZE(attributes)) slot = i;
    }
    if (slot < ARRAYSIZE(attributes)) {
        attributes[slot].attributes = list; attributes[slot].pc = (HPCON)value;
    }
    LeaveCriticalSection(&routes_lock);
    if (slot == ARRAYSIZE(attributes)) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    return TRUE;
}

static void WINAPI route_delete_attributes(LPPROC_THREAD_ATTRIBUTE_LIST list)
{
    EnterCriticalSection(&routes_lock);
    for (unsigned i = 0; i < ARRAYSIZE(attributes); i++)
        if (attributes[i].attributes == list)
            memset(&attributes[i], 0, sizeof(attributes[i]));
    LeaveCriticalSection(&routes_lock);
    DeleteProcThreadAttributeList(list);
}

static FARPROC WINAPI route_get_proc(HMODULE module, LPCSTR name)
{
    FARPROC actual = GetProcAddress(module, name);
    if (!actual || (ULONG_PTR)name <= 0xffff ||
        (module != GetModuleHandleW(L"kernel32.dll") &&
         module != GetModuleHandleW(L"kernelbase.dll"))) return actual;
    union { FARPROC generic; HRESULT (WINAPI *create)(COORD,HANDLE,HANDLE,DWORD,HPCON *);
        HRESULT (WINAPI *resize)(HPCON,COORD); void (WINAPI *close)(HPCON); } function;
    if (strcmp(name, "CreatePseudoConsole") == 0) function.create = uurb_create;
    else if (strcmp(name, "ResizePseudoConsole") == 0) function.resize = uurb_resize;
    else if (strcmp(name, "ClosePseudoConsole") == 0) function.close = uurb_close;
    else return actual;
    return function.generic;
}

static BOOL raw_console(HANDLE handle)
{
    DWORD mode;
    return raw_output && GetConsoleMode(handle, &mode) &&
        (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

static BOOL WINAPI route_write(HANDLE handle, LPCVOID bytes, DWORD count,
    LPDWORD written, LPOVERLAPPED overlap)
{
    return WriteFile(raw_console(handle) ? raw_output : handle,
        bytes, count, written, overlap);
}

static BOOL WINAPI route_console_write(HANDLE handle, const VOID *text, DWORD units,
    LPDWORD written, LPVOID reserved)
{
    if (!raw_console(handle)) return WriteConsoleW(handle, text, units, written, reserved);
    if (written) *written = 0;
    if (units == 0) return TRUE;
    if (units > INT_MAX) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    int count = WideCharToMultiByte(CP_UTF8, 0, text, (int)units, NULL, 0, NULL, NULL);
    char *utf8 = count > 0 ? HeapAlloc(GetProcessHeap(), 0, count) : NULL;
    if (!utf8) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    WideCharToMultiByte(CP_UTF8, 0, text, (int)units, utf8, count, NULL, NULL);
    BOOL result = TRUE;
    DWORD done = 0;
    while (done < (DWORD)count) {
        DWORD amount = 0;
        if (!WriteFile(raw_output, utf8 + done, count - done, &amount, NULL) || !amount) {
            result = FALSE; break;
        }
        done += amount;
    }
    HeapFree(GetProcessHeap(), 0, utf8);
    if (result && written) *written = units;
    return result;
}

static const WCHAR *basename(const WCHAR *path)
{
    const WCHAR *last = path;
    for (const WCHAR *p = path; *p; p++) if (*p == L'\\' || *p == L'/') last = p + 1;
    return last;
}

static BOOL selected_child(LPCWSTR application, LPCWSTR command)
{
    WCHAR first[32768];
    if (!application) {
        if (!command) return FALSE;
        while (*command == L' ' || *command == L'\t') command++;
        BOOL quoted = *command == L'"';
        if (quoted) command++;
        SIZE_T i = 0;
        while (*command && (quoted ? *command != L'"' : *command != L' ' && *command != L'\t')) {
            if (i + 1 >= ARRAYSIZE(first)) return FALSE;
            first[i++] = *command++;
        }
        first[i] = 0; application = first;
    }
    const WCHAR *name = basename(application);
    if (!_wcsicmp(name, L"uuyc-mux.exe")) return 2;
    if (!_wcsicmp(name, L"powershell.exe")) return TRUE;
    return FALSE;
}

static ULONG_PTR child_module(DWORD pid, const WCHAR *name)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W entry = {0}; entry.dwSize = sizeof(entry);
    ULONG_PTR result = 0;
    if (Module32FirstW(snapshot, &entry)) do {
        if (!_wcsicmp(entry.szModule, basename(name))) {
            result = (ULONG_PTR)entry.modBaseAddr; break;
        }
    } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return result;
}

static BOOL remote_call(HANDLE process, LPTHREAD_START_ROUTINE function, void *argument,
    DWORD *result, BOOL *still_running)
{
    *still_running = FALSE;
    HANDLE thread = CreateRemoteThread(process, NULL, 0, function, argument, 0, NULL);
    if (!thread) return FALSE;
    DWORD wait = WaitForSingleObject(thread, 5000);
    BOOL completed = wait == WAIT_OBJECT_0 && GetExitCodeThread(thread, result);
    *still_running = wait != WAIT_OBJECT_0;
    CloseHandle(thread);
    return completed;
}

static BOOL adopt_child(PROCESS_INFORMATION *child, HANDLE output, BOOL direct)
{
    WCHAR path[32768], loader_path[MAX_PATH];
    inject_stage = "module-path";
    DWORD length = GetModuleFileNameW(self_module, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return FALSE;
    SIZE_T bytes = (length + 1) * sizeof(WCHAR), copied = 0;
    BOOL still_running = FALSE;
    inject_stage = "allocate";
    void *remote_path = VirtualAllocEx(child->hProcess, NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) return FALSE;
    inject_stage = "write-path";
    if (!WriteProcessMemory(child->hProcess, remote_path, path, bytes, &copied) || copied != bytes)
        goto failed;
    union { FARPROC generic; HMODULE (WINAPI *load)(LPCWSTR); LPTHREAD_START_ROUTINE thread; } loader;
    loader.generic = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE loader_module = NULL;
    inject_stage = "loader-owner";
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)(ULONG_PTR)loader.generic, &loader_module) ||
        !GetModuleFileNameW(loader_module, loader_path, ARRAYSIZE(loader_path))) goto failed;
    DWORD result = 0;
    inject_stage = "load-library";
    if (!remote_call(child->hProcess, loader.thread, remote_path, &result, &still_running)) goto failed;
    VirtualFreeEx(child->hProcess, remote_path, 0, MEM_RELEASE);
    remote_path = NULL;
    /* Wine denies Toolhelp module snapshots before its initial loader thread
     * runs. Use Wine's common system DLL address, then
     * checks it after that thread has initialized the child. */
    inject_stage = "loader-address-check";
    if (child_module(child->dwProcessId, loader_path) != (ULONG_PTR)loader_module) goto failed;
    inject_stage = "loaded-child-base";
    ULONG_PTR base = child_module(child->dwProcessId, path);
    if (!base) goto failed;
    if (direct) {
        struct direct_child_events events = {0};
        inject_stage = "duplicate-viewer-events";
        if (!DuplicateHandle(GetCurrentProcess(), direct_io.ready, child->hProcess, &events.ready,
            0, FALSE, DUPLICATE_SAME_ACCESS) ||
            !DuplicateHandle(GetCurrentProcess(), direct_io.done, child->hProcess, &events.done,
            0, FALSE, DUPLICATE_SAME_ACCESS)) goto failed;
        inject_stage = "bind-viewer-events";
        remote_path = VirtualAllocEx(child->hProcess, NULL, sizeof(events), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote_path || !WriteProcessMemory(child->hProcess, remote_path, &events, sizeof(events), &copied) ||
            copied != sizeof(events)) goto failed;
        union { DWORD (WINAPI *bind)(void *); LPTHREAD_START_ROUTINE thread; ULONG_PTR address; } binder;
        binder.bind = uurb_bind_direct;
        binder.address = base + binder.address - (ULONG_PTR)self_module;
        if (!remote_call(child->hProcess, binder.thread, remote_path, &result, &still_running) || !result) goto failed;
        VirtualFreeEx(child->hProcess, remote_path, 0, MEM_RELEASE);
        return TRUE;
    }
    if (!output) return TRUE;
    HANDLE duplicated = NULL;
    inject_stage = "duplicate-output";
    if (!DuplicateHandle(GetCurrentProcess(), output, child->hProcess, &duplicated,
        0, FALSE, DUPLICATE_SAME_ACCESS)) goto failed;
    union { DWORD (WINAPI *adopt)(void *); LPTHREAD_START_ROUTINE thread; ULONG_PTR address; } adopter;
    adopter.adopt = uurb_adopt_output;
    adopter.address = base + adopter.address - (ULONG_PTR)self_module;
    inject_stage = "adopt-output";
    if (remote_call(child->hProcess, adopter.thread, duplicated, &result, &still_running) && result == 1)
        return TRUE;
failed:
    if (remote_path && !still_running)
        VirtualFreeEx(child->hProcess, remote_path, 0, MEM_RELEASE);
    /* A running remote thread retains its argument. The caller terminates
     * this owned child on failure, reclaiming its memory and duplicated pipe. */
    return FALSE;
}

static BOOL WINAPI route_create_process(LPCWSTR application, LPWSTR command,
    LPSECURITY_ATTRIBUTES process_security, LPSECURITY_ATTRIBUTES thread_security,
    BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR directory,
    LPSTARTUPINFOW startup, LPPROCESS_INFORMATION child)
{
    HANDLE output = raw_output;
    BOOL direct = FALSE;
    if ((flags & EXTENDED_STARTUPINFO_PRESENT) && startup &&
        startup->cb >= sizeof(STARTUPINFOEXW)) {
        LPPROC_THREAD_ATTRIBUTE_LIST list = ((STARTUPINFOEXW *)startup)->lpAttributeList;
        HPCON pc = NULL;
        EnterCriticalSection(&routes_lock);
        for (unsigned i = 0; i < ARRAYSIZE(attributes); i++)
            if (attributes[i].attributes == list) { pc = attributes[i].pc; break; }
        LeaveCriticalSection(&routes_lock);
        if (pc == direct_io.pc && pc) { direct = TRUE; output = NULL; }
        else if (pc) output = console_output(pc);
    }
    BOOL selected = selected_child(application, command);
    if (!selected || (!output && !direct && selected != 2))
        return CreateProcessW(application, command, process_security, thread_security,
            inherit, flags, environment, directory, startup, child);

    if (!CreateProcessW(application, command, process_security, thread_security,
        inherit, flags | CREATE_SUSPENDED, environment, directory, startup, child)) {
        return FALSE;
    }
    if (!adopt_child(child, output, direct)) {

        fprintf(stderr, "terminal-output-attach-failed stage=%s error=%lu\n", inject_stage, GetLastError());
        TerminateProcess(child->hProcess, ERROR_DLL_INIT_FAILED);
        WaitForSingleObject(child->hProcess, 1000);
        CloseHandle(child->hThread); CloseHandle(child->hProcess);
        memset(child, 0, sizeof(*child));
        SetLastError(ERROR_DLL_INIT_FAILED);
        return FALSE;
    }
    if (!(flags & CREATE_SUSPENDED) && ResumeThread(child->hThread) == (DWORD)-1) {
        DWORD error = GetLastError();
        TerminateProcess(child->hProcess, error);
        WaitForSingleObject(child->hProcess, 1000);
        CloseHandle(child->hThread); CloseHandle(child->hProcess);
        memset(child, 0, sizeof(*child));
        SetLastError(error);
        return FALSE;
    }

    return TRUE;
}

static ULONG_PTR hook_address(const char *name)
{
    if (!strcmp(name, "GetProcAddress")) return (ULONG_PTR)route_get_proc;
    if (!strcmp(name, "UpdateProcThreadAttribute")) return (ULONG_PTR)route_attribute;
    if (!strcmp(name, "DeleteProcThreadAttributeList")) return (ULONG_PTR)route_delete_attributes;
    if (!strcmp(name, "CreateProcessW")) return (ULONG_PTR)route_create_process;
    if (!strcmp(name, "WriteFile")) return (ULONG_PTR)route_write;
    if (!strcmp(name, "WriteConsoleW")) return (ULONG_PTR)route_console_write;
    return 0;
}

static void patch_main_imports(void)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return;
    IMAGE_IMPORT_DESCRIPTOR *imports = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva);
    for (; imports->Name; imports++) {
        const char *library = (const char *)(base + imports->Name);
        if (_stricmp(library, "kernel32.dll") && _stricmp(library, "kernelbase.dll")) continue;
        if (!imports->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + imports->OriginalFirstThunk);
        IMAGE_THUNK_DATA *slots = (IMAGE_THUNK_DATA *)(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *import = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            ULONG_PTR replacement = hook_address((const char *)import->Name);
            DWORD previous;
            if (replacement && VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &previous)) {
                slots->u1.Function = replacement;
                VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), previous, &previous);
            }
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = instance;
        InitializeCriticalSection(&routes_lock);
        DisableThreadLibraryCalls(instance);
        patch_main_imports();

    }
    return TRUE;
}
