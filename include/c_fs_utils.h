// begin file include/c_fs_utils.h
#ifndef C_FS_UTILS_H
#define C_FS_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(_WIN32)
    #define CFS_PLATFORM_WINDOWS 1
    #define Rectangle w__Rectangle
    #define LoadImage w__LoadImage
    #define DrawText w__DrawText
    #define DrawTextEx w__DrawTextEx
    #define ShowCursor w__ShowCursor
    #define AdapterType w__AdapterType
    #include <windows.h>
    #undef AdapterType
    #undef ShowCursor
    #undef LoadImage
    #undef DrawTextEx
    #undef DrawText
    #undef Rectangle
    #include <io.h>
    #define CFS_PATH_SEPARATOR '\\'
    #define CFS_MAX_PATH MAX_PATH
#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    #define CFS_PLATFORM_POSIX 1
    #include <unistd.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <limits.h>
    #define CFS_PATH_SEPARATOR '/'
    #define CFS_MAX_PATH PATH_MAX
#else
    #error "c_fs_utils: Unsupported platform"
#endif

// The size of the inline buffer for cfs_path.
// Paths shorter than this (including the null terminator) will not cause a heap allocation.
#define CFS_INLINE_BUFFER_SIZE 24

/**
 * @struct cfs_path
 * @brief A string structure optimized for file paths.
 * 
 * Implements the Small String Optimization (SSO). For short paths, the string data
 * is stored directly within the struct's `buffer` array, avoiding heap allocation.
 * For longer paths, memory is allocated on the heap, and the `str` pointer points to it.
 * 
 * - If `p->str == p->buffer`, the path is using the inline storage.
 * - If `p->str != p->buffer`, the path is using heap-allocated storage.
 * 
 * This design provides the performance of stack allocation for common, short paths
 * while seamlessly handling paths of any length.
 */
typedef struct cfs_path {
    char* str;      // Pointer to the beginning of the string data. This is ALWAYS the correct pointer to use.
    size_t len;     // Current length of the string (excluding null terminator).
    size_t capacity;// Current allocated capacity (including null terminator).
    char buffer[CFS_INLINE_BUFFER_SIZE]; // Inline buffer for small strings.
} cfs_path;

typedef struct cfs_path_list {
    uint32_t pathCount;
    cfs_path* paths;
} cfs_path_list;

// --- cfs_path Management ---
static inline void cfs_path_init(cfs_path* p);
static inline void cfs_path_free_storage(cfs_path* p);
static inline bool cfs_path_set(cfs_path* p, const char* src);
static inline const char* cfs_path_c_str(const cfs_path* p);

// --- Filesystem Operations ---
static inline bool cfs_get_working_directory(cfs_path* working_dir);
static inline bool cfs_get_absolute_path(const char* path, cfs_path* absolute_path);
static inline bool cfs_exists(const char* path);
static inline bool cfs_is_directory(const char* path);
static inline bool cfs_list_directory(const char* path, cfs_path_list* list);
static inline void cfs_free_path_list(cfs_path_list* list);


// -----------------------------------------------------------------------------
//                          IMPLEMENTATIONS
// -----------------------------------------------------------------------------

/**
 * @brief Initializes a cfs_path to a valid, empty state using the inline buffer.
 */
static inline void cfs_path_init(cfs_path* p) {
    if (!p) return;
    p->str = p->buffer;
    p->len = 0;
    p->capacity = CFS_INLINE_BUFFER_SIZE;
    p->buffer[0] = '\0';
}

/**
 * @brief Frees any heap-allocated memory used by the cfs_path and resets it.
 * Does nothing if the path is using its inline buffer.
 */
static inline void cfs_path_free_storage(cfs_path* p) {
    if (p && p->str != p->buffer) {
        free(p->str);
    }
    // Always re-initialize to a valid state.
    cfs_path_init(p);
}

/**
 * @brief Sets the content of a cfs_path from a C-style string.
 * This function handles all memory management, allocating, reallocating, or
 * freeing memory as needed to accommodate the new string.
 * @return true on success, false on memory allocation failure.
 */
static inline bool cfs_path_set(cfs_path* p, const char* src) {
    if (!p || !src) return false;

    const size_t src_len = strlen(src);
    const size_t required_cap = src_len + 1;
    const bool was_on_heap = (p->str != p->buffer);

    // Case 1: The new string fits in the inline buffer.
    if (required_cap <= CFS_INLINE_BUFFER_SIZE) {
        // If we were previously on the heap, free that memory.
        if (was_on_heap) {
            free(p->str);
        }
        p->str = p->buffer;
        p->capacity = CFS_INLINE_BUFFER_SIZE;
    }
    // Case 2: The new string requires heap allocation.
    else {
        // If we were already on the heap, try to realloc.
        if (was_on_heap) {
            // Only realloc if we actually need more space.
            if (required_cap > p->capacity) {
                char* new_str = (char*)realloc(p->str, required_cap);
                if (!new_str) return false; // Realloc failed
                p->str = new_str;
                p->capacity = required_cap;
            }
        }
        // If we were using the inline buffer, we must malloc.
        else {
            char* new_str = (char*)malloc(required_cap);
            if (!new_str) return false; // Malloc failed
            p->str = new_str;
            p->capacity = required_cap;
        }
    }

    // Copy the string data and update the length.
    memcpy(p->str, src, required_cap);
    p->len = src_len;
    return true;
}

/**
 * @brief Returns a const C-style string pointer to the path's data.
 * @return A valid, null-terminated C-string. This pointer is valid until the
 *         next call to a non-const function for this cfs_path instance.
 *         Returns an empty string "" if p is NULL.
 */
static inline const char* cfs_path_c_str(const cfs_path* p) {
    // This is correct because p->str is ALWAYS the pointer to the valid
    // string data, regardless of whether it's in the inline buffer or on the heap.
    return p ? p->str : "";
}

static inline bool cfs_exists(const char* path) {
#if CFS_PLATFORM_WINDOWS
    return _access(path, 0) == 0;
#elif CFS_PLATFORM_POSIX
    return access(path, F_OK) == 0;
#endif
}
static inline bool cfs_path_exists(const cfs_path* p) {
    if (!p) return false;
    return cfs_exists(cfs_path_c_str(p));
}

static inline bool cfs_is_directory(const char* path) {
#if CFS_PLATFORM_WINDOWS
    DWORD attrib = GetFileAttributesA(path);
    return (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY));
#elif CFS_PLATFORM_POSIX
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
#endif
}

static inline bool cfs_get_absolute_path(cfs_path* absolute_path, const char* path) {
    if (!path || !absolute_path) return false;
    cfs_path_init(absolute_path);
    char buffer[CFS_MAX_PATH];

#if CFS_PLATFORM_WINDOWS
    DWORD len = GetFullPathNameA(path, CFS_MAX_PATH, buffer, NULL);
    if (len == 0 || len > CFS_MAX_PATH) {
        return false;
    }
#elif CFS_PLATFORM_POSIX
    char* result = realpath(path, buffer);
    if (result == NULL) {
        return false;
    }
#endif
    return cfs_path_set(absolute_path, buffer);
}

static inline bool cfs_get_working_directory(cfs_path* working_dir) {
    if (!working_dir) return false;
    cfs_path_init(working_dir);
    char buffer[CFS_MAX_PATH];

#if CFS_PLATFORM_WINDOWS
    DWORD len = GetCurrentDirectoryA(CFS_MAX_PATH, buffer);
    if (len == 0 || len > CFS_MAX_PATH) {
        return false;
    }
#elif CFS_PLATFORM_POSIX
    char* result = getcwd(buffer, CFS_MAX_PATH);
    if (result == NULL) {
        return false;
    }
#endif
    return cfs_path_set(working_dir, buffer);
}

static inline bool cfs_list_directory(const char* path, cfs_path_list* list) {
    if (!path || !list || !cfs_is_directory(path)) {
        if(list) {
            list->pathCount = 0;
            list->paths = NULL;
        }
        return false;
    }

    list->pathCount = 0;
    list->paths = NULL;
    size_t capacity = 8;
    list->paths = (cfs_path*)malloc(capacity * sizeof(cfs_path));
    if (!list->paths) return false;

    size_t path_len = strlen(path);
    bool needs_sep = (path_len > 0 && path[path_len - 1] != CFS_PATH_SEPARATOR);

#if CFS_PLATFORM_WINDOWS
    char search_path[CFS_MAX_PATH];
    snprintf(search_path, CFS_MAX_PATH, "%s\\*", path);

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);

    if (hFind == INVALID_HANDLE_VALUE) {
        free(list->paths);
        list->paths = NULL;
        return false;
    }

    do {
        const char* name = find_data.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        if (list->pathCount >= capacity) {
            capacity *= 2;
            cfs_path* new_paths = (cfs_path*)realloc(list->paths, capacity * sizeof(cfs_path));
            if (!new_paths) goto list_fail;
            list->paths = new_paths;
        }
        
        cfs_path_init(&list->paths[list->pathCount]);
        char full_path_buffer[CFS_MAX_PATH];
        snprintf(full_path_buffer, CFS_MAX_PATH, "%s%s%s", path, needs_sep ? "\\" : "", name);

        if (!cfs_path_set(&list->paths[list->pathCount], full_path_buffer)) {
            goto list_fail;
        }
        list->pathCount++;

    } while (FindNextFileA(hFind, &find_data) != 0);

    FindClose(hFind);

#elif CFS_PLATFORM_POSIX
    DIR* dir = opendir(path);
    if (!dir) {
        free(list->paths);
        list->paths = NULL;
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        if (list->pathCount >= capacity) {
            capacity *= 2;
            cfs_path* new_paths = (cfs_path*)realloc(list->paths, capacity * sizeof(cfs_path));
            if (!new_paths) goto list_fail;
            list->paths = new_paths;
        }
        
        cfs_path_init(&list->paths[list->pathCount]);
        char full_path_buffer[CFS_MAX_PATH];
        snprintf(full_path_buffer, CFS_MAX_PATH, "%s%s%s", path, needs_sep ? "/" : "", name);

        if(!cfs_path_set(&list->paths[list->pathCount], full_path_buffer)) {
            goto list_fail;
        }
        list->pathCount++;
    }
    closedir(dir);
#endif

    return true;

list_fail:
#if CFS_PLATFORM_WINDOWS
    FindClose(hFind);
#elif CFS_PLATFORM_POSIX
    if (dir) closedir(dir);
#endif
    cfs_free_path_list(list);
    return false;
}

static inline void cfs_free_path_list(cfs_path_list* list) {
    if (!list || !list->paths) return;
    for (uint32_t i = 0; i < list->pathCount; ++i) {
        cfs_path_free_storage(&list->paths[i]);
    }
    free(list->paths);
    list->paths = NULL;
    list->pathCount = 0;
}

#endif /* CFS_H */
// end file include/c_fs_utils.h