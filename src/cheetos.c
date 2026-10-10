#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#define PATCH_MAX_SIZE 5

typedef struct {
    DWORD rva;
    BYTE expected[PATCH_MAX_SIZE];
    BYTE replacement[PATCH_MAX_SIZE];
    SIZE_T length;
    const char *name;
} Patch;

static const Patch patches[] = {
    {0x00ECFC7E, {0x75, 0x7F}, {0x90, 0x90}, 2, "LEGACY sick"},
    {0x00ED00E2, {0x75, 0x7F}, {0x90, 0x90}, 2, "PBOT1 sick"},
    {0x00ED0536, {0x72, 0x7F}, {0x90, 0x90}, 2, "WEEK7 sick"},
    {0x008F0A00, {0x48}, {0xC3}, 1, "regular misses disabled"},
    {0x008E8690, {0x48}, {0xC3}, 1, "ghost misses disabled"},
    {0x008F624C, {0xE8, 0x9F, 0x8B, 0xFE, 0xFF}, {0x90, 0x90, 0x90, 0x90, 0x90}, 5, "hold-drop penalties disabled"},
    {0x00ECF2EE, {0x74, 0x7A}, {0x90, 0x90}, 2, "forced pfc"}
};

static void log_line(const char *line) {
    WCHAR path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;

    WCHAR *end = path + length;
    while (end > path && end[-1] != L'\\' && end[-1] != L'/') --end;
    if (end == path || (size_t)(end - path) + 14 >= MAX_PATH) return;
    lstrcpyW(end, L"cheetos.log");

    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD written;
    WriteFile(file, line, lstrlenA(line), &written, NULL);
    WriteFile(file, "\r\n", 2, &written, NULL);
    CloseHandle(file);
}

static BOOL check_image(BYTE *base) {
    if (!base) return FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000)
        return FALSE;

    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
           nt->FileHeader.TimeDateStamp == 0x6A7C66FF &&
           nt->OptionalHeader.SizeOfImage == 0x050F6000 &&
           nt->OptionalHeader.AddressOfEntryPoint == 0x035B52E0 &&
           nt->OptionalHeader.SizeOfCode == 0x03635800;
}

static BOOL write_patch(BYTE *address, const BYTE *bytes, SIZE_T length) {
    DWORD old_protection;
    if (!VirtualProtect(address, length, PAGE_EXECUTE_READWRITE, &old_protection))
        return FALSE;

    memcpy(address, bytes, length);
    FlushInstructionCache(GetCurrentProcess(), address, length);

    DWORD ignored;
    VirtualProtect(address, length, old_protection, &ignored);
    return TRUE;
}

static DWORD WINAPI patch_thread(LPVOID unused) {
    (void)unused;
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    if (!check_image(base)) {
        log_line("might not be the funkin 0.8.6 itch.io build!");
        return 1;
    }

    for (size_t i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
        const Patch *patch = &patches[i];
        if (memcmp(base + patch->rva, patch->expected, patch->length) != 0) {
            log_line("might not be the funkin 0.8.6 itch build! If this is an updated build report it to the github");
            return 2;
        }
    }

    for (size_t i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
        const Patch *patch = &patches[i];
        if (!write_patch(base + patch->rva, patch->replacement, patch->length)) {
            for (size_t j = i; j > 0; --j) {
                const Patch *earlier = &patches[j - 1];
                write_patch(base + earlier->rva, earlier->expected, earlier->length);
            }
            log_line("cheetos could not be applied");
            return 3;
        }
        log_line(patch->name);
    }

    log_line("cheetos applied & enabled! happy cheating!");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(NULL, 0, patch_thread, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
