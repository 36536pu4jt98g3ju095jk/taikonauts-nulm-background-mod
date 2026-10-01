#ifndef TNMOD_SDK_H
#define TNMOD_SDK_H

#include <string.h>
#include "tnmod_api.h"

#define TNMOD_BASE_HAS(api, field) \
    ((api) != NULL && (api)->size >= offsetof(TNModApi, field) + sizeof((api)->field))

#define TNMOD_LOAD_PROC(api, variable, export_name) do { \
    FARPROC tnmod_procedure__ = (api)->getOriginalProc(export_name); \
    memcpy(&(variable), &tnmod_procedure__, sizeof(variable)); \
} while (0)

#endif
