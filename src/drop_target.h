#ifndef EDGESTASH_DROP_TARGET_H
#define EDGESTASH_DROP_TARGET_H

#include "common.h"
#include <ole2.h>

#ifdef __cplusplus
extern "C" {
#endif

// Creates a COM IDropTarget instance implemented in C
IDropTarget* DropTarget_Create(HWND hwnd);

#ifdef __cplusplus
}
#endif

#endif // EDGESTASH_DROP_TARGET_H
