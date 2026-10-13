#include "storage.h"
#include "sqlite3.h"

static sqlite3* g_db = NULL;
static CRITICAL_SECTION g_dbLock;
static wchar_t g_baseDir[MAX_PATH] = { 0 };
static wchar_t g_cacheDir[MAX_PATH] = { 0 };
static wchar_t g_thumbsDir[MAX_PATH] = { 0 };
static bool g_initialized = false;

static void EnsureDirExists(const wchar_t* path) {
    CreateDirectoryW(path, NULL);
}

static char* WCharToUtf8(const wchar_t* wstr) {
    if (!wstr) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char* u8 = (char*)malloc((size_t)len);
    if (u8) {
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, u8, len, NULL, NULL);
    }
    return u8;
}

static void Utf8ToWChar(const char* u8, wchar_t* out_wstr, size_t max_chars) {
    if (!out_wstr || max_chars == 0) return;
    out_wstr[0] = L'\0';
    if (!u8) return;
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, out_wstr, (int)max_chars);
    out_wstr[max_chars - 1] = L'\0';
}

const wchar_t* Storage_GetBaseDir(void) {
    return g_baseDir;
}

const wchar_t* Storage_GetCacheDir(void) {
    return g_cacheDir;
}

const wchar_t* Storage_GetThumbsDir(void) {
    return g_thumbsDir;
}

bool Storage_Init(const wchar_t* custom_base_dir) {
    if (g_initialized) return true;

    InitializeCriticalSection(&g_dbLock);

    if (custom_base_dir && custom_base_dir[0] != L'\0') {
        wcsncpy_s(g_baseDir, MAX_PATH, custom_base_dir, _TRUNCATE);
    } else {
        wchar_t localAppData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
            swprintf_s(g_baseDir, MAX_PATH, L"%s\\EdgeStash", localAppData);
        } else {
            wcscpy_s(g_baseDir, MAX_PATH, L".\\EdgeStash");
        }
    }

    EnsureDirExists(g_baseDir);

    swprintf_s(g_cacheDir, MAX_PATH, L"%s\\cache", g_baseDir);
    EnsureDirExists(g_cacheDir);

    swprintf_s(g_thumbsDir, MAX_PATH, L"%s\\thumbs", g_baseDir);
    EnsureDirExists(g_thumbsDir);

    wchar_t dbPath[MAX_PATH];
    swprintf_s(dbPath, MAX_PATH, L"%s\\metadata.db", g_baseDir);

    char* dbPathUtf8 = WCharToUtf8(dbPath);
    if (!dbPathUtf8) {
        DeleteCriticalSection(&g_dbLock);
        return false;
    }

    int rc = sqlite3_open_v2(dbPathUtf8, &g_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             NULL);
    free(dbPathUtf8);

    if (rc != SQLITE_OK) {
        if (g_db) {
            sqlite3_close(g_db);
            g_db = NULL;
        }
        DeleteCriticalSection(&g_dbLock);
        return false;
    }

    // Performance & durability PRAGMAs
    sqlite3_exec(g_db, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL);
    sqlite3_exec(g_db, "PRAGMA synchronous = NORMAL;", NULL, NULL, NULL);
    sqlite3_exec(g_db, "PRAGMA temp_store = MEMORY;", NULL, NULL, NULL);

    // Schema creation
    const char* schema =
        "CREATE TABLE IF NOT EXISTS items ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type INTEGER NOT NULL,"
        "  title TEXT NOT NULL,"
        "  content TEXT,"
        "  file_path TEXT,"
        "  thumb_path TEXT,"
        "  file_size INTEGER DEFAULT 0,"
        "  created_at INTEGER NOT NULL,"
        "  expires_at INTEGER NOT NULL,"
        "  is_pinned INTEGER DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_expires_at ON items(expires_at);"
        "CREATE INDEX IF NOT EXISTS idx_pinned_created ON items(is_pinned, created_at);";

    char* errMsg = NULL;
    rc = sqlite3_exec(g_db, schema, NULL, NULL, &errMsg);
    if (rc != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        sqlite3_close(g_db);
        g_db = NULL;
        DeleteCriticalSection(&g_dbLock);
        return false;
    }

    g_initialized = true;
    return true;
}

void Storage_Shutdown(void) {
    if (!g_initialized) return;

    EnterCriticalSection(&g_dbLock);
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
    LeaveCriticalSection(&g_dbLock);

    DeleteCriticalSection(&g_dbLock);
    g_initialized = false;
}

int64_t Storage_AddItem(ItemType type,
                        const wchar_t* title,
                        const wchar_t* content,
                        const wchar_t* file_path,
                        const wchar_t* thumb_path,
                        int64_t file_size,
                        int is_pinned) {
    if (!g_initialized || !g_db) return -1;

    EnterCriticalSection(&g_dbLock);

    const char* sql =
        "INSERT INTO items (type, title, content, file_path, thumb_path, file_size, created_at, expires_at, is_pinned) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LeaveCriticalSection(&g_dbLock);
        return -1;
    }

    char* uTitle = WCharToUtf8(title ? title : L"");
    char* uContent = WCharToUtf8(content ? content : L"");
    char* uFilePath = WCharToUtf8(file_path ? file_path : L"");
    char* uThumbPath = WCharToUtf8(thumb_path ? thumb_path : L"");

    int64_t now = (int64_t)time(NULL);
    int64_t expires = now + 86400; // 24 hours TTL

    sqlite3_bind_int(stmt, 1, (int)type);
    sqlite3_bind_text(stmt, 2, uTitle ? uTitle : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, uContent ? uContent : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, uFilePath ? uFilePath : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, uThumbPath ? uThumbPath : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, file_size);
    sqlite3_bind_int64(stmt, 7, now);
    sqlite3_bind_int64(stmt, 8, expires);
    sqlite3_bind_int(stmt, 9, is_pinned ? 1 : 0);

    rc = sqlite3_step(stmt);
    int64_t newId = -1;
    if (rc == SQLITE_DONE) {
        newId = (int64_t)sqlite3_last_insert_rowid(g_db);
    }

    sqlite3_finalize(stmt);

    if (uTitle) free(uTitle);
    if (uContent) free(uContent);
    if (uFilePath) free(uFilePath);
    if (uThumbPath) free(uThumbPath);

    LeaveCriticalSection(&g_dbLock);
    return newId;
}

int Storage_GetItems(StashItem** out_items, int* out_count) {
    if (!out_items || !out_count) return 0;
    *out_items = NULL;
    *out_count = 0;
    if (!g_initialized || !g_db) return 0;

    EnterCriticalSection(&g_dbLock);

    const char* countSql = "SELECT COUNT(*) FROM items;";
    sqlite3_stmt* countStmt = NULL;
    int count = 0;
    if (sqlite3_prepare_v2(g_db, countSql, -1, &countStmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(countStmt) == SQLITE_ROW) {
            count = sqlite3_column_int(countStmt, 0);
        }
        sqlite3_finalize(countStmt);
    }

    if (count == 0) {
        LeaveCriticalSection(&g_dbLock);
        return 0;
    }

    StashItem* items = (StashItem*)calloc((size_t)count, sizeof(StashItem));
    if (!items) {
        LeaveCriticalSection(&g_dbLock);
        return 0;
    }

    const char* sql =
        "SELECT id, type, title, content, file_path, thumb_path, file_size, created_at, expires_at, is_pinned "
        "FROM items ORDER BY is_pinned DESC, created_at DESC;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free(items);
        LeaveCriticalSection(&g_dbLock);
        return 0;
    }

    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < count) {
        items[idx].id = sqlite3_column_int64(stmt, 0);
        items[idx].type = (ItemType)sqlite3_column_int(stmt, 1);

        const char* t = (const char*)sqlite3_column_text(stmt, 2);
        Utf8ToWChar(t, items[idx].title, 260);

        const char* c = (const char*)sqlite3_column_text(stmt, 3);
        Utf8ToWChar(c, items[idx].content, 2048);

        const char* f = (const char*)sqlite3_column_text(stmt, 4);
        Utf8ToWChar(f, items[idx].file_path, MAX_PATH);

        const char* th = (const char*)sqlite3_column_text(stmt, 5);
        Utf8ToWChar(th, items[idx].thumb_path, MAX_PATH);

        items[idx].file_size = sqlite3_column_int64(stmt, 6);
        items[idx].created_at = sqlite3_column_int64(stmt, 7);
        items[idx].expires_at = sqlite3_column_int64(stmt, 8);
        items[idx].is_pinned = sqlite3_column_int(stmt, 9);

        idx++;
    }

    sqlite3_finalize(stmt);
    LeaveCriticalSection(&g_dbLock);

    *out_items = items;
    *out_count = idx;
    return idx;
}

void Storage_FreeItems(StashItem* items) {
    if (items) {
        free(items);
    }
}

bool Storage_GetItemById(int64_t id, StashItem* out_item) {
    if (!out_item || !g_initialized || !g_db) return false;
    memset(out_item, 0, sizeof(StashItem));

    EnterCriticalSection(&g_dbLock);
    const char* sql =
        "SELECT id, type, title, content, file_path, thumb_path, file_size, created_at, expires_at, is_pinned "
        "FROM items WHERE id = ?;";

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LeaveCriticalSection(&g_dbLock);
        return false;
    }

    sqlite3_bind_int64(stmt, 1, id);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out_item->id = sqlite3_column_int64(stmt, 0);
        out_item->type = (ItemType)sqlite3_column_int(stmt, 1);

        const char* t = (const char*)sqlite3_column_text(stmt, 2);
        Utf8ToWChar(t, out_item->title, 260);

        const char* c = (const char*)sqlite3_column_text(stmt, 3);
        Utf8ToWChar(c, out_item->content, 2048);

        const char* f = (const char*)sqlite3_column_text(stmt, 4);
        Utf8ToWChar(f, out_item->file_path, MAX_PATH);

        const char* th = (const char*)sqlite3_column_text(stmt, 5);
        Utf8ToWChar(th, out_item->thumb_path, MAX_PATH);

        out_item->file_size = sqlite3_column_int64(stmt, 6);
        out_item->created_at = sqlite3_column_int64(stmt, 7);
        out_item->expires_at = sqlite3_column_int64(stmt, 8);
        out_item->is_pinned = sqlite3_column_int(stmt, 9);
        found = true;
    }

    sqlite3_finalize(stmt);
    LeaveCriticalSection(&g_dbLock);
    return found;
}

static void DeleteFileIfExists(const wchar_t* path) {
    if (path && path[0] != L'\0') {
        // Only delete if it belongs to our cache/thumbs directory
        if (wcsstr(path, g_baseDir) != NULL) {
            DeleteFileW(path);
        }
    }
}

bool Storage_DeleteItem(int64_t id) {
    if (!g_initialized || !g_db) return false;

    StashItem item;
    if (Storage_GetItemById(id, &item)) {
        DeleteFileIfExists(item.file_path);
        DeleteFileIfExists(item.thumb_path);
    }

    EnterCriticalSection(&g_dbLock);
    const char* sql = "DELETE FROM items WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    bool success = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            success = true;
        }
        sqlite3_finalize(stmt);
    }
    LeaveCriticalSection(&g_dbLock);
    return success;
}

bool Storage_TogglePin(int64_t id) {
    if (!g_initialized || !g_db) return false;

    EnterCriticalSection(&g_dbLock);
    const char* sql = "UPDATE items SET is_pinned = 1 - is_pinned WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    bool success = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            success = true;
        }
        sqlite3_finalize(stmt);
    }
    LeaveCriticalSection(&g_dbLock);
    return success;
}

int Storage_PurgeExpiredItems(void) {
    if (!g_initialized || !g_db) return 0;

    int64_t now = (int64_t)time(NULL);

    EnterCriticalSection(&g_dbLock);

    const char* selectSql = "SELECT id, file_path, thumb_path FROM items WHERE is_pinned = 0 AND expires_at <= ?;";
    sqlite3_stmt* selectStmt = NULL;
    if (sqlite3_prepare_v2(g_db, selectSql, -1, &selectStmt, NULL) != SQLITE_OK) {
        LeaveCriticalSection(&g_dbLock);
        return 0;
    }

    sqlite3_bind_int64(selectStmt, 1, now);

    int count = 0;
    while (sqlite3_step(selectStmt) == SQLITE_ROW) {
        const char* f = (const char*)sqlite3_column_text(selectStmt, 1);
        const char* th = (const char*)sqlite3_column_text(selectStmt, 2);

        wchar_t wf[MAX_PATH];
        wchar_t wth[MAX_PATH];
        Utf8ToWChar(f, wf, MAX_PATH);
        Utf8ToWChar(th, wth, MAX_PATH);

        DeleteFileIfExists(wf);
        DeleteFileIfExists(wth);
        count++;
    }
    sqlite3_finalize(selectStmt);

    if (count > 0) {
        const char* deleteSql = "DELETE FROM items WHERE is_pinned = 0 AND expires_at <= ?;";
        sqlite3_stmt* deleteStmt = NULL;
        if (sqlite3_prepare_v2(g_db, deleteSql, -1, &deleteStmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(deleteStmt, 1, now);
            sqlite3_step(deleteStmt);
            sqlite3_finalize(deleteStmt);
        }
    }

    LeaveCriticalSection(&g_dbLock);
    return count;
}

bool Storage_ClearAll(bool only_unpinned) {
    if (!g_initialized || !g_db) return false;

    EnterCriticalSection(&g_dbLock);

    const char* selectSql = only_unpinned ?
        "SELECT id, file_path, thumb_path FROM items WHERE is_pinned = 0;" :
        "SELECT id, file_path, thumb_path FROM items;";

    sqlite3_stmt* selectStmt = NULL;
    if (sqlite3_prepare_v2(g_db, selectSql, -1, &selectStmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(selectStmt) == SQLITE_ROW) {
            const char* f = (const char*)sqlite3_column_text(selectStmt, 1);
            const char* th = (const char*)sqlite3_column_text(selectStmt, 2);

            wchar_t wf[MAX_PATH];
            wchar_t wth[MAX_PATH];
            Utf8ToWChar(f, wf, MAX_PATH);
            Utf8ToWChar(th, wth, MAX_PATH);

            DeleteFileIfExists(wf);
            DeleteFileIfExists(wth);
        }
        sqlite3_finalize(selectStmt);
    }

    const char* deleteSql = only_unpinned ?
        "DELETE FROM items WHERE is_pinned = 0;" :
        "DELETE FROM items;";

    sqlite3_exec(g_db, deleteSql, NULL, NULL, NULL);

    LeaveCriticalSection(&g_dbLock);
    return true;
}
