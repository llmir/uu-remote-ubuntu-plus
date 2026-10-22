#include "freerdp-adapter.h"
#include <stdio.h>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2 || !LoadLibraryW(argv[1]))
        return 2;
    ui_exports exports;
    bool resolved = ui_resolve(&exports, GetModuleHandleW(NULL));
    printf("shared_core_exports=%s\n", resolved ? "resolved" : "rejected");
    return resolved ? 0 : 1;
}
