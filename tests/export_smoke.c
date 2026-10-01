#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    HMODULE mod = LoadLibraryA(argv[1]);
    if (mod == NULL) {
        printf("LoadLibrary failed: %lu\n", GetLastError());
        return 3;
    }
    int valid = GetProcAddress(mod, "TaikoNautsModInit") != NULL;
    FreeLibrary(mod);
    if (!valid) {
        puts("TaikoNautsModInit export missing");
        return 4;
    }
    puts("Export smoke passed");
    return 0;
}
