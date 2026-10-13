#ifndef EDGESTASH_DROP_SOURCE_H
#define EDGESTASH_DROP_SOURCE_H

#include "common.h"
#include <ole2.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initiates OLE drag-and-drop to external applications (Explorer, Discord, Browser, etc.)
DWORD DropSource_StartDrag(const StashItem* item);

#ifdef __cplusplus
}
#endif

#endif // EDGESTASH_DROP_SOURCE_H
