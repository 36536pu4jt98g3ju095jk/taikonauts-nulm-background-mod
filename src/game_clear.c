#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game_clear.h"

/* Gauge.IsGaugeClear(this) as compiled in TaikoNauts 2026.10.01.1.
   '?' bytes are the relative call targets. The value is the gauge percentage
   at this + 0x30; the function compares half of it against the clear border
   returned by GetNormalGaugeCount for the course at this + 0x38. */
static const unsigned char SIGNATURE[] = {
    0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9, 0xF2, 0x0F, 0x10, 0x43, 0x30,
    0xE8, 0, 0, 0, 0,
    0x8B, 0xD0, 0xC1, 0xEA, 0x1F, 0x03, 0xC2, 0xD1, 0xF8, 0x89, 0x44, 0x24, 0x2C,
    0x48, 0x8B, 0xCB,
    0xE8, 0, 0, 0, 0,
    0x39, 0x44, 0x24, 0x2C, 0x0F, 0x9F, 0xC0, 0x0F, 0xB6, 0xC0,
    0x48, 0x83, 0xC4, 0x30, 0x5B, 0xC3
};
static const unsigned char SIGNATURE_MASK[sizeof(SIGNATURE)] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1,
    1, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1
};

/* GetNormalGaugeCount(this): 0x1D for course 0, 0x22 for 1 and 2, else 0x27. */
static const unsigned char BORDER_FUNCTION[] = {
    0x8B, 0x41, 0x38, 0x85, 0xC0, 0x74, 0x16, 0xFF, 0xC8, 0xB9, 0x27, 0x00, 0x00, 0x00,
    0xBA, 0x22, 0x00, 0x00, 0x00, 0x83, 0xF8, 0x01, 0x0F, 0x46, 0xCA, 0x8B, 0xC1,
    0xEB, 0x05, 0xB8, 0x1D, 0x00, 0x00, 0x00, 0xC3
};

#define STOLEN_BYTES 13u      /* whole instructions before the first call */
#define PATCH_BYTES 18u       /* through the end of that call */
#define CALL_OFFSET 13u

typedef unsigned int (__cdecl *IsGaugeClearFn)(void *self);

static unsigned char *hook_target;
static unsigned char original_bytes[PATCH_BYTES];
static unsigned char *trampoline;
static IsGaugeClearFn original_function;
static int hook_installed;

static volatile LONG latest_result = -1;
static volatile LONG call_count;

static int signature_matches(const unsigned char *at)
{
    for (size_t i = 0; i < sizeof(SIGNATURE); ++i)
        if (SIGNATURE_MASK[i] && at[i] != SIGNATURE[i]) return 0;
    return 1;
}

static const unsigned char *call_target(const unsigned char *call_instruction)
{
    int32_t displacement;
    memcpy(&displacement, call_instruction + 1, sizeof(displacement));
    return call_instruction + 5 + displacement;
}

static void write_absolute_jump(unsigned char *destination, const void *target)
{
    uint64_t address = 0;
    destination[0] = 0xFF;
    destination[1] = 0x25;
    destination[2] = 0x00;
    destination[3] = 0x00;
    destination[4] = 0x00;
    destination[5] = 0x00;
    memcpy(&address, &target, sizeof(target));
    memcpy(destination + 6, &address, sizeof(address));
}

/* Scans every executable section of the main module for the signature and
   returns its address only when there is exactly one match. */
static unsigned char *find_function(void)
{
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    if (base == NULL) return NULL;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);

    unsigned char *found = NULL;
    int matches = 0;
    for (unsigned int s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        unsigned char *start = base + section->VirtualAddress;
        size_t length = section->Misc.VirtualSize;
        if (length < sizeof(SIGNATURE)) continue;
        for (size_t offset = 0; offset + sizeof(SIGNATURE) <= length; ++offset) {
            unsigned char *at = (unsigned char *)memchr(start + offset, SIGNATURE[0],
                                                         length - sizeof(SIGNATURE) - offset + 1);
            if (at == NULL) break;
            offset = (size_t)(at - start);
            if (signature_matches(at)) {
                found = at;
                ++matches;
            }
        }
    }
    return matches == 1 ? found : NULL;
}

static void record(unsigned int result)
{
    latest_result = (result & 0xFF) != 0 ? 1 : 0;
    InterlockedIncrement(&call_count);
}
static unsigned int __cdecl hooked_is_gauge_clear(void *self)
{
    unsigned int result = original_function != NULL ? original_function(self) : 0u;
    record(result);
    return result;
}

int game_clear_install(char *message, unsigned int message_size)
{
    DWORD old_protect;
    DWORD ignored;
    void *replacement = NULL;
    IsGaugeClearFn replacement_function = hooked_is_gauge_clear;
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);

    if (hook_installed) return 1;
    hook_target = find_function();
    if (hook_target == NULL) {
        snprintf(message, message_size,
                 "Gauge.IsGaugeClear was not found exactly once; the game's clear state is unavailable");
        return 0;
    }
    const unsigned char *border = call_target(hook_target + 34);
    if (memcmp(border, BORDER_FUNCTION, sizeof(BORDER_FUNCTION)) != 0) {
        snprintf(message, message_size,
                 "Gauge.IsGaugeClear was found but its clear border function differs; not hooked");
        hook_target = NULL;
        return 0;
    }

    memcpy(original_bytes, hook_target, PATCH_BYTES);
    const unsigned char *first_call_target = call_target(hook_target + CALL_OFFSET);
    trampoline = (unsigned char *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                                               PAGE_EXECUTE_READWRITE);
    if (trampoline == NULL) {
        snprintf(message, message_size, "Could not allocate the hook trampoline");
        hook_target = NULL;
        return 0;
    }
    /* original prologue, then the first call made absolute, then the rest of
       the function in place */
    unsigned int at = 0;
    memcpy(trampoline + at, original_bytes, STOLEN_BYTES);
    at += STOLEN_BYTES;
    uint64_t call_address = 0;
    memcpy(&call_address, &first_call_target, sizeof(call_address));
    trampoline[at++] = 0x48;
    trampoline[at++] = 0xB8;
    memcpy(trampoline + at, &call_address, sizeof(call_address));
    at += 8;
    trampoline[at++] = 0xFF;
    trampoline[at++] = 0xD0;
    write_absolute_jump(trampoline + at, hook_target + PATCH_BYTES);
    at += 14;
    while (at < 64) trampoline[at++] = 0xCC;
    FlushInstructionCache(GetCurrentProcess(), trampoline, 64);
    memcpy(&original_function, &trampoline, sizeof(original_function));

    if (!VirtualProtect(hook_target, PATCH_BYTES, PAGE_EXECUTE_READWRITE, &old_protect)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        trampoline = NULL;
        original_function = NULL;
        snprintf(message, message_size, "Could not make the game code writable");
        hook_target = NULL;
        return 0;
    }
    memcpy(&replacement, &replacement_function, sizeof(replacement));
    write_absolute_jump(hook_target, replacement);
    for (unsigned int i = 14; i < PATCH_BYTES; ++i) hook_target[i] = 0xCC;
    FlushInstructionCache(GetCurrentProcess(), hook_target, PATCH_BYTES);
    VirtualProtect(hook_target, PATCH_BYTES, old_protect, &ignored);
    hook_installed = 1;
    snprintf(message, message_size, "Hooked Gauge.IsGaugeClear at RVA 0x%X",
             (unsigned int)(hook_target - base));
    return 1;
}

void game_clear_remove(void)
{
    DWORD old_protect;
    DWORD ignored;
    if (hook_installed && hook_target != NULL &&
        VirtualProtect(hook_target, PATCH_BYTES, PAGE_EXECUTE_READWRITE, &old_protect)) {
        memcpy(hook_target, original_bytes, PATCH_BYTES);
        FlushInstructionCache(GetCurrentProcess(), hook_target, PATCH_BYTES);
        VirtualProtect(hook_target, PATCH_BYTES, old_protect, &ignored);
    }
    hook_installed = 0;
    /* The trampoline stays allocated: a game thread may still be inside it. */
}

int game_clear_latest(void)
{
    return (int)latest_result;
}
void game_clear_forget(void)
{
    latest_result = -1;
}

unsigned int game_clear_calls(void)
{
    return (unsigned int)call_count;
}
