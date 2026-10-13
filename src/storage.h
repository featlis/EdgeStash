#ifndef EDGESTASH_STORAGE_H
#define EDGESTASH_STORAGE_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initializes SQLite database and directories in %LOCALAPPDATA%\EdgeStash
bool Storage_Init(const wchar_t* custom_base_dir);
void Storage_Shutdown(void);

// Add items
int64_t Storage_AddItem(ItemType type,
                        const wchar_t* title,
                        const wchar_t* content,
                        const wchar_t* file_path,
                        const wchar_t* thumb_path,
                        int64_t file_size,
                        int is_pinned);

// Query items (sorted by is_pinned DESC, created_at DESC)
int Storage_GetItems(StashItem** out_items, int* out_count);
void Storage_FreeItems(StashItem* items);

// Individual item operations
bool Storage_DeleteItem(int64_t id);
bool Storage_TogglePin(int64_t id);
bool Storage_GetItemById(int64_t id, StashItem* out_item);

// Batch operations
int Storage_PurgeExpiredItems(void);
bool Storage_ClearAll(bool only_unpinned);

// Path helpers
const wchar_t* Storage_GetBaseDir(void);
const wchar_t* Storage_GetCacheDir(void);
const wchar_t* Storage_GetThumbsDir(void);

#ifdef __cplusplus
}
#endif

#endif // EDGESTASH_STORAGE_H
