#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000006
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Private pipe fixture, copied as conpty_bridge.exe and powershell.exe. The
 * child only waits on its injected viewer events; production bootstrap and
 * vendor ownership are exercised by the separate real-MUX recovery probe. */
int wmain(int argc, WCHAR **argv)
{
    if (argc == 2 && !wcscmp(argv[1], L"--bound-child")) {
        HMODULE module = GetModuleHandleW(L"conpty.dll");
        union { FARPROC generic; int (WINAPI *wait)(void); } function;
        function.generic = module ? GetProcAddress(module, "uurb_wait_direct") : NULL;
        return function.generic ? function.wait() : 9;
    }
    if (argc != 7) return 2;
    HMODULE module = LoadLibraryW(L"conpty.dll");
    union { FARPROC generic; HRESULT (WINAPI *create)(COORD,HANDLE,HANDLE,DWORD,HPCON *); } create;
    union { FARPROC generic; HRESULT (WINAPI *resize)(HPCON,COORD); } resize;
    union { FARPROC generic; void (WINAPI *close)(HPCON); } close;
    if (!module) return 3;
    create.generic = GetProcAddress(module, "CreatePseudoConsole");
    resize.generic = GetProcAddress(module, "ResizePseudoConsole");
    close.generic = GetProcAddress(module, "ClosePseudoConsole");
    if (!create.generic || !resize.generic || !close.generic) return 3;
    HANDLE inr = NULL, inw = NULL, outr = NULL, outw = NULL;
    if (!CreatePipe(&inr, &inw, NULL, 0) || !CreatePipe(&outr, &outw, NULL, 0)) return 4;
    COORD size = {80,24}; HPCON pc = (HPCON)(uintptr_t)1;
    HRESULT null_result = create.create(size, inr, outw, 0, NULL);
    HRESULT invalid_result = create.create(size, INVALID_HANDLE_VALUE, outw, 0, &pc);
    BOOL reset = pc == NULL;
    printf("null_pc=%08lx invalid_input=%08lx invalid_pc_reset=%d\n", (ULONG)null_result, (ULONG)invalid_result, reset);
    fflush(stdout);
    if (null_result != E_POINTER || !FAILED(invalid_result) || !reset) return 5;
    HRESULT result = create.create(size, inr, outw, 0, &pc);
    CloseHandle(inr); CloseHandle(outw);
    if (FAILED(result) || !pc) return 6;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!attributes || !InitializeProcThreadAttributeList(attributes, 1, 0, &bytes) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc, sizeof(pc), NULL, NULL)) return 7;
    WCHAR command[4096];
    swprintf(command, ARRAYSIZE(command), L"\"%ls\" --bound-child", argv[1]);
    STARTUPINFOEXW startup = {0}; PROCESS_INFORMATION child = {0};
    startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
    BOOL started = CreateProcessW(argv[1], command, NULL, NULL, FALSE,
        EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &startup.StartupInfo, &child);
    DeleteProcThreadAttributeList(attributes); HeapFree(GetProcessHeap(), 0, attributes);
    if (!started) { printf("child_start_failed=%lu\n", GetLastError()); return 8; }
    DWORD deadline = GetTickCount() + 15000;
    while (GetFileAttributesW(argv[2]) == INVALID_FILE_ATTRIBUTES) {
        if ((LONG)(GetTickCount() - deadline) >= 0) return 10;
        Sleep(10);
    }
    COORD changed = {100,31};
    HRESULT resized = resize.resize(pc, changed);
    printf("pending_auth_resize=%08lx\n", (ULONG)resized); fflush(stdout);
    HANDLE signal = CreateFileW(argv[3], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (signal == INVALID_HANDLE_VALUE) return 11;
    CloseHandle(signal);
    static const unsigned char input[] = {'i','n',0x03,0x1b,'[','A','\r',0xe4,0xb8,0xad};
    DWORD written = 0;
    BOOL sent = WriteFile(inw, input, sizeof(input), &written, NULL) && written == sizeof(input);
    HANDLE capture = CreateFileW(argv[4], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (capture == INVALID_HANDLE_VALUE) return 12;
    DWORD total = 0;
    deadline = GetTickCount() + 15000;
    while (GetFileAttributesW(L"wire-output-complete") == INVALID_FILE_ATTRIBUTES) {
        DWORD available = 0;
        if (PeekNamedPipe(outr, NULL, 0, NULL, &available, NULL) && available) {
            char buffer[4096]; DWORD got = 0;
            if (!ReadFile(outr, buffer, sizeof(buffer), &got, NULL) ||
                !WriteFile(capture, buffer, got, &written, NULL) || written != got) return 13;
            total += got;
        }
        if ((LONG)(GetTickCount() - deadline) >= 0) return 14;
        Sleep(10);
    }
    DWORD available = 0;
    while (PeekNamedPipe(outr, NULL, 0, NULL, &available, NULL) && available) {
        char buffer[4096]; DWORD got = 0;
        if (!ReadFile(outr, buffer, sizeof(buffer), &got, NULL) ||
            !WriteFile(capture, buffer, got, &written, NULL) || written != got) return 13;
        total += got;
    }
    CloseHandle(capture);
    COORD final_size = {110,33};
    HRESULT final_resize = resize.resize(pc, final_size);
    DWORD before = 0, after = 99;
    GetExitCodeProcess(child.hProcess, &before);
    CloseHandle(inw);
    BOOL exited = WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0;
    GetExitCodeProcess(child.hProcess, &after);
    DWORD close_start = GetTickCount(); close.close(pc);
    DWORD close_ms = GetTickCount() - close_start;
    CloseHandle(outr); CloseHandle(child.hThread); CloseHandle(child.hProcess);
    printf("resize=%08lx post_auth_resize=%08lx input_sent=%d output_bytes=%lu before_eof=%lu after_eof=%lu close_ms=%lu\n",
        (ULONG)resized, (ULONG)final_resize, sent, total, before, after, close_ms);
    return resized == S_OK && final_resize == S_OK && sent && before == STILL_ACTIVE && exited && after == 0 ? 0 : 15;
}
