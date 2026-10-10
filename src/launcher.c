#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include <string.h>
#include "patch_verify.h"

static BOOL path_next_to_launcher(WCHAR *output, DWORD capacity, const WCHAR *file) {
    DWORD length = GetModuleFileNameW(NULL, output, capacity);
    if (!length || length >= capacity) return FALSE;
    WCHAR *end = output + length;
    while (end > output && end[-1] != L'\\' && end[-1] != L'/') --end;
    if (end == output || (size_t)(end - output) + lstrlenW(file) + 1 > capacity)
        return FALSE;
    lstrcpyW(end, file);
    return TRUE;
}

static HMODULE remote_module(DWORD pid, const WCHAR *module_name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return NULL;
    MODULEENTRY32W entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    HMODULE found = NULL;
    if (Module32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, module_name) == 0) {
                found = entry.hModule;
                break;
            }
        } while (Module32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return found;
}

static BOOL inject_dll(HANDLE process, DWORD pid, const WCHAR *dll_path) {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC load_library = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : NULL;
    if (!load_library) return FALSE;

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((const void *)load_library, &mbi, sizeof(mbi))) return FALSE;

    WCHAR local_provider_path[MAX_PATH];
    DWORD n = GetModuleFileNameW((HMODULE)mbi.AllocationBase, local_provider_path, MAX_PATH);
    if (!n || n >= MAX_PATH) return FALSE;
    const WCHAR *provider_name = wcsrchr(local_provider_path, L'\\');
    provider_name = provider_name ? provider_name + 1 : local_provider_path;

    HMODULE provider = NULL;
    for (int i = 0; i < 200; ++i) {
        provider = remote_module(pid, provider_name);
        if (provider) break;
        if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) return FALSE;
        Sleep(50);
    }
    if (!provider) return FALSE;

    uintptr_t offset = (uintptr_t)load_library - (uintptr_t)mbi.AllocationBase;
    LPTHREAD_START_ROUTINE remote_load_library =
        (LPTHREAD_START_ROUTINE)((BYTE *)provider + offset);

    SIZE_T bytes = ((SIZE_T)lstrlenW(dll_path) + 1) * sizeof(WCHAR);
    LPVOID remote_path = VirtualAllocEx(process, NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) return FALSE;

    BOOL success = FALSE;
    SIZE_T written = 0;
    if (WriteProcessMemory(process, remote_path, dll_path, bytes, &written) && written == bytes) {
        HANDLE thread = CreateRemoteThread(process, NULL, 0, remote_load_library,
                                           remote_path, 0, NULL);
        if (thread) {
            DWORD wait_result = WaitForSingleObject(thread, 10000);
            CloseHandle(thread);
            if (wait_result == WAIT_OBJECT_0) {
                for (int i = 0; i < 100; ++i) {
                    if (remote_module(pid, L"FunkinCheetos.dll")) {
                        success = TRUE;
                        break;
                    }
                    Sleep(50);
                }
            }
        }
    }
    if (success) VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    return success;
}

static BOOL verify_active_patches(HANDLE process, DWORD pid) {
    BYTE *base = (BYTE *)remote_module(pid, L"Funkin.exe");
    if (!base) {
        fwprintf(stderr, L"is Funkin running??\n");
        return FALSE;
    }

    for (int attempt = 0; attempt < 150; ++attempt) {
        size_t applied = 0;
        for (size_t i = 0; i < VERIFY_PATCH_COUNT; ++i) {
            const VerifyPatch *patch = &verify_patches[i];
            BYTE actual[5];
            SIZE_T bytes_read = 0;
            if (ReadProcessMemory(process, base + patch->rva, actual,
                                  patch->length, &bytes_read) &&
                bytes_read == patch->length &&
                memcmp(actual, patch->replacement, patch->length) == 0)
                ++applied;
        }
        if (applied == VERIFY_PATCH_COUNT) return TRUE;
        if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) break;
        Sleep(20);
    }

    for (size_t i = 0; i < VERIFY_PATCH_COUNT; ++i) {
        const VerifyPatch *patch = &verify_patches[i];
        BYTE actual[5];
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(process, base + patch->rva, actual,
                               patch->length, &bytes_read) ||
            bytes_read != patch->length) {
            fwprintf(stderr, L"Cheetos %u: cannot read RVA 0x%08lX.\n",
                     (unsigned int)(i + 1), (unsigned long)patch->rva);
        } else if (memcmp(actual, patch->replacement, patch->length) != 0) {
            BOOL original = memcmp(actual, patch->expected, patch->length) == 0;
            fwprintf(stderr, L"Cheetos %u: %ls at RVA 0x%08lX.\n",
                     (unsigned int)(i + 1),
                     original ? L"still contains original bytes" : L"unexpected bytes",
                     (unsigned long)patch->rva);
        }
    }
    return FALSE;
}

int wmain(void) {
    WCHAR exe_path[MAX_PATH], dll_path[MAX_PATH], work_directory[MAX_PATH];
    if (!path_next_to_launcher(exe_path, MAX_PATH, L"Funkin.exe") ||
        !path_next_to_launcher(dll_path, MAX_PATH, L"FunkinCheetos.dll") ||
        !path_next_to_launcher(work_directory, MAX_PATH, L"")) {
        fwprintf(stderr, L"Doesn't seem to be a FNF dir!'\n");
        return 1;
    }

    if (GetFileAttributesW(exe_path) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(dll_path) == INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"Make sure ze dll & launcher is next to Funkin.exe!\n");
        return 2;
    }

    STARTUPINFOW startup;
    PROCESS_INFORMATION info;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&info, sizeof(info));
    startup.cb = sizeof(startup);

    WCHAR command_line[MAX_PATH * 2];
    if (lstrlenW(exe_path) + 3 > MAX_PATH * 2) return 3;
    wsprintfW(command_line, L"\"%s\"", exe_path);

    if (!CreateProcessW(exe_path, command_line, NULL, NULL, FALSE, 0, NULL,
                        work_directory, &startup, &info)) {
        fwprintf(stderr, L"Funkin failed to start (or something)");
        return 4;
    }

    BOOL loaded = inject_dll(info.hProcess, info.dwProcessId, dll_path);
    if (loaded) loaded = verify_active_patches(info.hProcess, info.dwProcessId);
    if (loaded) {
        wprintf(L"cheetos loaded! have fun cheating! | FunkinCheetos made with love by @hrioshi_ on twitter <3\n");
    } else {
        fwprintf(stderr, L"Funkin started but cheetos failed to load </3");
    }

    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return loaded ? 0 : 5;
}
