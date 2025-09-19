// begin file include/renderstate.hpp
#include <array>
#include <deque>
#include <map>
#include <mutex>
#include <raygpu.h>
#include <set>
#include <vector>
#if SUPPORT_WGPU_BACKEND == 1
#include <webgpu/webgpu.h>
#else
#include <wgvk.h>
#endif

typedef struct {
    float axes[16];
    Vector2 position;
} PenInputState;

#define KEYS_MAX 512
#define MOUSEBTN_MAX 16
#define TOUCH_MAX 32
#define CHARQ_MAX 256
#define PEN_MAX 16

typedef struct {
    int64_t id;
    Vector2 pos;
} TouchPoint;

typedef struct {
    Rectangle windowPosition;
    uint8_t keydownPrevious[KEYS_MAX];
    uint8_t keydown[KEYS_MAX];
    Vector2 scrollThisFrame, scrollPreviousFrame;
    float gestureZoomThisFrame;
    float gestureAngleThisFrame;
    Vector2 mousePosPrevious;
    Vector2 mousePos;
    int cursorInWindow;
    uint8_t mouseButtonDownPrevious[MOUSEBTN_MAX];
    uint8_t mouseButtonDown[MOUSEBTN_MAX];
    TouchPoint touchPoints[TOUCH_MAX];
    size_t touchPointsCount;

    int charQueue[CHARQ_MAX];
    size_t charQueueHead, charQueueTail, charQueueCount;
    
    struct {
        unsigned int key;
        PenInputState value;
        int used;
    } penStates[PEN_MAX];
    size_t penStatesCount;
} window_input_state;

#define DEFINE_ARRAY_STACK(T, LINKAGE, N)                                       \
typedef struct {                                                                \
    T data[N];                                                                  \
    uint32_t current_pos;                                                       \
} T##_stack;                                                                    \
LINKAGE void T##_stack_init(T##_stack *s) { s->current_pos = 0; }               \
LINKAGE void T##_stack_push(T##_stack *s, T v) {                                \
    rassert(s->current_pos < (N), "Out of bounds access");                      \
    s->data[s->current_pos++] = v;                                              \
}                                                                               \
LINKAGE T T##_stack_pop(T##_stack *s) {                                         \
    rassert(s->current_pos > 0, "Out of bounds access");                        \
    return s->data[--s->current_pos];                                           \
}                                                                               \
LINKAGE T *T##_stack_peek(T##_stack *s) {                                       \
    rassert(s->current_pos > 0, "Out of bounds access");                        \
    return &s->data[s->current_pos - 1];                                        \
}                                                                               \
LINKAGE const T *T##_stack_cpeek(const T##_stack *s) {                          \
    rassert(s->current_pos > 0, "Out of bounds access");                        \
    return &s->data[s->current_pos - 1];                                        \
}                                                                               \
LINKAGE size_t T##_stack_size(const T##_stack *s) { return s->current_pos; }    \
LINKAGE int T##_stack_empty(const T##_stack *s) { return s->current_pos == 0; }
typedef struct MatrixBufferPair{
    Matrix matrix;
    WGPUBuffer buffer;
}MatrixBufferPair;

DEFINE_ARRAY_STACK(MatrixBufferPair, static inline, 8);
DEFINE_ARRAY_STACK(RenderTexture, static inline, 8);
#ifndef PHM_INLINE_CAPACITY
#define PHM_INLINE_CAPACITY 3
#endif

#ifndef PHM_INITIAL_HEAP_CAPACITY
#define PHM_INITIAL_HEAP_CAPACITY 8
#endif

#ifndef PHM_LOAD_FACTOR_NUM
#define PHM_LOAD_FACTOR_NUM 3
#endif

#ifndef PHM_LOAD_FACTOR_DEN
#define PHM_LOAD_FACTOR_DEN 4
#endif

#ifndef PHM_HASH_MULTIPLIER
#define PHM_HASH_MULTIPLIER 0x9E3779B97F4A7C15ULL
#endif
#ifndef PHM_EMPTY_SLOT_KEY
#define PHM_EMPTY_SLOT_KEY NULL
#endif
#ifndef PHM_DELETED_SLOT_KEY
#define PHM_DELETED_SLOT_KEY ((void*)0xFFFFFFFFFFFFFFFF)
#endif
#define DEFINE_PTR_HASH_MAP_R(SCOPE, Name, ValueType)                                                                              \
                                                                                                                                 \
    typedef struct Name##_kv_pair {                                                                                              \
        void *key;                                                                                                               \
        ValueType value;                                                                                                         \
    } Name##_kv_pair;                                                                                                            \
                                                                                                                                 \
    typedef struct Name {                                                                                                        \
        uint64_t current_size;     /* Number of non-NULL keys */                                                                 \
        uint64_t current_capacity; /* Capacity of the heap-allocated table */                                                    \
        bool has_null_key;                                                                                                       \
        ValueType null_value;                                                                                                    \
        Name##_kv_pair* table; /* Pointer to the hash table data (heap-allocated) */                                             \
    } Name;                                                                                                                      \
                                                                                                                                 \
    static inline uint64_t Name##_hash_key(void *key) {                                                                          \
        assert(key != NULL);                                                                                                     \
        return ((uintptr_t)key) * PHM_HASH_MULTIPLIER;                                                                           \
    }                                                                                                                            \
                                                                                                                                 \
    /* Helper to round up to the next power of 2. Result can be 0 if v is 0 or on overflow from UINT64_MAX. */                   \
    static inline uint64_t Name##_round_up_to_power_of_2(uint64_t v) {                                                           \
        if (v == 0)                                                                                                              \
            return 0;                                                                                                            \
        v--;                                                                                                                     \
        v |= v >> 1;                                                                                                             \
        v |= v >> 2;                                                                                                             \
        v |= v >> 4;                                                                                                             \
        v |= v >> 8;                                                                                                             \
        v |= v >> 16;                                                                                                            \
        v |= v >> 32;                                                                                                            \
        v++;                                                                                                                     \
        return v;                                                                                                                \
    }                                                                                                                            \
                                                                                                                                 \
    static void Name##_insert_entry(Name##_kv_pair *table, uint64_t capacity, void *key, ValueType value) {                      \
        assert(key != NULL && key != PHM_EMPTY_SLOT_KEY && capacity > 0 && (capacity & (capacity - 1)) == 0);                    \
        uint64_t cap_mask = capacity - 1;                                                                                        \
        uint64_t index = Name##_hash_key(key) & cap_mask;                                                                        \
        while (table[index].key != PHM_EMPTY_SLOT_KEY) {                                                                         \
            index = (index + 1) & cap_mask;                                                                                      \
        }                                                                                                                        \
        table[index].key = key;                                                                                                  \
        table[index].value = value;                                                                                              \
    }                                                                                                                            \
                                                                                                                                 \
    static Name##_kv_pair *Name##_find_slot(Name *map, void *key) {                                                              \
        assert(key != NULL && key != PHM_EMPTY_SLOT_KEY && map->table != NULL && map->current_capacity > 0);                     \
        uint64_t cap_mask = map->current_capacity - 1;                                                                           \
        uint64_t index = Name##_hash_key(key) & cap_mask;                                                                        \
        while (map->table[index].key != PHM_EMPTY_SLOT_KEY && map->table[index].key != key) {                                    \
            index = (index + 1) & cap_mask;                                                                                      \
        }                                                                                                                        \
        return &map->table[index];                                                                                               \
    }                                                                                                                            \
                                                                                                                                 \
    static void Name##_grow(Name *map); /* Forward declaration */                                                                \
                                                                                                                                 \
    SCOPE void Name##_init(Name *map) {                                                                                          \
        map->current_size = 0;                                                                                                   \
        map->current_capacity = 0;                                                                                               \
        map->has_null_key = false;                                                                                               \
        /* map->null_value is uninitialized, which is fine */                                                                    \
        map->table = NULL;                                                                                                       \
    }                                                                                                                            \
                                                                                                                                 \
    static void Name##_grow(Name *map) {                                                                                         \
        uint64_t old_capacity = map->current_capacity;                                                                           \
        Name##_kv_pair *old_table = map->table;                                                                                  \
        uint64_t new_capacity;                                                                                                   \
                                                                                                                                 \
        if (old_capacity == 0) {                                                                                                 \
            new_capacity = (PHM_INITIAL_HEAP_CAPACITY > 0) ? PHM_INITIAL_HEAP_CAPACITY : 8; /* Default 8 if initial is 0 */      \
        } else {                                                                                                                 \
            if (old_capacity >= (UINT64_MAX / 2))                                                                                \
                new_capacity = UINT64_MAX; /* Avoid overflow */                                                                  \
            else                                                                                                                 \
                new_capacity = old_capacity * 2;                                                                                 \
        }                                                                                                                        \
                                                                                                                                 \
        new_capacity = Name##_round_up_to_power_of_2(new_capacity);                                                              \
        if (new_capacity == 0 && old_capacity == 0 && ((PHM_INITIAL_HEAP_CAPACITY > 0) ? PHM_INITIAL_HEAP_CAPACITY : 8) > 0) {   \
            /* This case means round_up_to_power_of_2 resulted in 0 from a non-zero initial desired capacity (e.g. UINT64_MAX)   \
             */                                                                                                                  \
            /* If PHM_INITIAL_HEAP_CAPACITY was huge and overflowed round_up. Use max power of 2. */                             \
            new_capacity = (UINT64_C(1) << 63);                                                                                  \
        }                                                                                                                        \
                                                                                                                                 \
        if (new_capacity == 0 || (new_capacity <= old_capacity && old_capacity > 0)) {                                           \
            return; /* Cannot grow or no actual increase in capacity */                                                          \
        }                                                                                                                        \
                                                                                                                                 \
        Name##_kv_pair *new_table = (Name##_kv_pair *)calloc(new_capacity, sizeof(Name##_kv_pair));                              \
        if (!new_table)                                                                                                          \
            return; /* Allocation failure */                                                                                     \
                                                                                                                                 \
        if (old_table && map->current_size > 0) {                                                                                \
            uint64_t rehashed_count = 0;                                                                                         \
            for (uint64_t i = 0; i < old_capacity; ++i) {                                                                        \
                if (old_table[i].key != PHM_EMPTY_SLOT_KEY) {                                                                    \
                    Name##_insert_entry(new_table, new_capacity, old_table[i].key, old_table[i].value);                          \
                    rehashed_count++;                                                                                            \
                    if (rehashed_count == map->current_size)                                                                     \
                        break;                                                                                                   \
                }                                                                                                                \
            }                                                                                                                    \
        }                                                                                                                        \
        if (old_table)                                                                                                           \
            free(old_table);                                                                                                     \
        map->table = new_table;                                                                                                  \
        map->current_capacity = new_capacity;                                                                                    \
    }                                                                                                                            \
                                                                                                                                 \
    SCOPE int Name##_put(Name *map, void *key, ValueType value) {                                                                \
        if (key == NULL) {                                                                                                       \
            map->null_value = value;                                                                                             \
            if (!map->has_null_key) {                                                                                            \
                map->has_null_key = true;                                                                                        \
                return 1; /* New NULL key */                                                                                     \
            }                                                                                                                    \
            return 0; /* Updated NULL key */                                                                                     \
        }                                                                                                                        \
        assert(key != PHM_EMPTY_SLOT_KEY);                                                                                       \
                                                                                                                                 \
        if (map->current_capacity == 0 ||                                                                                        \
            (map->current_size + 1) * PHM_LOAD_FACTOR_DEN >= map->current_capacity * PHM_LOAD_FACTOR_NUM) {                      \
            uint64_t old_cap = map->current_capacity;                                                                            \
            Name##_grow(map);                                                                                                    \
            if (map->current_capacity == old_cap && old_cap > 0) { /* Grow failed or no increase */                              \
                /* Re-check if still insufficient */                                                                             \
                if ((map->current_size + 1) * PHM_LOAD_FACTOR_DEN >= map->current_capacity * PHM_LOAD_FACTOR_NUM)                \
                    return 0;                                                                                                    \
            } else if (map->current_capacity == 0)                                                                               \
                return 0; /* Grow failed to allocate any capacity */                                                             \
            else if ((map->current_size + 1) * PHM_LOAD_FACTOR_DEN >= map->current_capacity * PHM_LOAD_FACTOR_NUM)               \
                return 0;                                                                                                        \
        }                                                                                                                        \
        assert(map->current_capacity > 0 && map->table != NULL); /* Must have capacity after grow check */                       \
                                                                                                                                 \
        Name##_kv_pair *slot = Name##_find_slot(map, key);                                                                       \
        if (slot->key == PHM_EMPTY_SLOT_KEY) {                                                                                   \
            slot->key = key;                                                                                                     \
            slot->value = value;                                                                                                 \
            map->current_size++;                                                                                                 \
            return 1; /* New key */                                                                                              \
        } else {                                                                                                                 \
            assert(slot->key == key);                                                                                            \
            slot->value = value;                                                                                                 \
            return 0; /* Updated existing key */                                                                                 \
        }                                                                                                                        \
    }                                                                                                                            \
                                                                                                                                 \
    SCOPE ValueType *Name##_get(Name *map, void *key) {                                                                          \
        if (key == NULL)                                                                                                         \
            return map->has_null_key ? &map->null_value : NULL;                                                                  \
        assert(key != PHM_EMPTY_SLOT_KEY);                                                                                       \
        if (map->current_capacity == 0 || map->table == NULL)                                                                    \
            return NULL;                                                                                                         \
        Name##_kv_pair *slot = Name##_find_slot(map, key);                                                                       \
        return (slot->key == key) ? &slot->value : NULL;                                                                         \
    }                                                                                                                            \
                                                                                                                                 \
    SCOPE void Name##_for_each(Name *map, void (*callback)(void *key, ValueType *value, void *user_data), void *user_data) {     \
        if (map->has_null_key)                                                                                                   \
            callback(NULL, &map->null_value, user_data);                                                                         \
        if (map->current_capacity > 0 && map->table != NULL && map->current_size > 0) {                                          \
            uint64_t count = 0;                                                                                                  \
            for (uint64_t i = 0; i < map->current_capacity; ++i) {                                                               \
                if (map->table[i].key != PHM_EMPTY_SLOT_KEY) {                                                                   \
                    callback(map->table[i].key, &map->table[i].value, user_data);                                                \
                    if (++count == map->current_size)                                                                            \
                        break;                                                                                                   \
                }                                                                                                                \
            }                                                                                                                    \
        }                                                                                                                        \
    }                                                                                                                            \
                                                                                                                                 \
    SCOPE void Name##_free(Name *map) {                                                                                          \
        if (map->table != NULL)                                                                                                  \
            free(map->table);                                                                                                    \
        Name##_init(map); /* Reset to initial empty state */                                                                     \
    }                                                                                                                            \
                                                                                                                                 \
    SCOPE void Name##_move(Name *dest, Name *source) {                                                                           \
        if (dest == source)                                                                                                      \
            return;                                                                                                              \
        if (dest->table != NULL)                                                                                                 \
            free(dest->table); /* Free existing dest resources */                                                                \
        *dest = *source;       /* Copy all members, dest now owns source's table */                                              \
        Name##_init(source);   /* Reset source to prevent double free */                                                         \
    }                                                                                                                            \
                                                                                                                                 \
    SCOPE void Name##_clear(Name *map) {                                                                                         \
        map->current_size = 0;                                                                                                   \
        map->has_null_key = false;                                                                                               \
        if (map->table != NULL && map->current_capacity > 0) {                                                                   \
            /* calloc already zeroed memory if PHM_EMPTY_SLOT_KEY is 0. */                                                       \
            /* If PHM_EMPTY_SLOT_KEY is not 0, or for robustness: */                                                             \
            for (uint64_t i = 0; i < map->current_capacity; ++i) {                                                               \
                map->table[i].key = PHM_EMPTY_SLOT_KEY;                                                                          \
                /* map->table[i].value = (ValueType){0}; // Optional: if values need resetting */                                \
            }                                                                                                                    \
        }                                                                                                                        \
    }                                                                                                                            \
                                                                                                                                 \
    SCOPE void Name##_copy(Name *dest, const Name *source) {                                                                     \
        if (dest == source)                                                                                                      \
            return;                                                                                                              \
        if (dest->table != NULL)                                                                                                 \
            free(dest->table);                                                                                                   \
        Name##_init(dest); /* Initialize dest to a clean empty state */                                                          \
                                                                                                                                 \
        dest->has_null_key = source->has_null_key;                                                                               \
        if (source->has_null_key)                                                                                                \
            dest->null_value = source->null_value;                                                                               \
        dest->current_size = source->current_size;                                                                               \
                                                                                                                                 \
        if (source->table != NULL && source->current_capacity > 0) {                                                             \
            dest->table = (Name##_kv_pair *)calloc(source->current_capacity, sizeof(Name##_kv_pair));                            \
            if (!dest->table) {                                                                                                  \
                Name##_init(dest);                                                                                               \
                return;                                                                                                          \
            } /* Alloc fail, reset dest to safe empty */                                                                         \
            memcpy(dest->table, source->table, source->current_capacity * sizeof(Name##_kv_pair));                               \
            dest->current_capacity = source->current_capacity;                                                                   \
        }                                                                                                                        \
        /* If source had no table, dest remains in its _init state (table=NULL, capacity=0) */                                   \
    }



DEFINE_PTR_HASH_MAP_R(static, CreatedWindowMap, window_input_state)



struct renderstate {
    WGPUPresentMode unthrottled_PresentMode;
    WGPUPresentMode throttled_PresentMode;

    GLFWwindow *window;
    uint32_t width, height;

    PixelFormat frameBufferFormat;

    Shader defaultShader;
    RenderSettings currentSettings;
    Shader activeShader;

    DescribedRenderpass clearPass;
    DescribedRenderpass renderpass;
    DescribedComputepass computepass;
    DescribedRenderpass *activeRenderpass;
    DescribedComputepass *activeComputepass;

    uint32_t renderExtentX; // Dimensions of the current viewport
    uint32_t renderExtentY; // Required for camera function

    std::vector<DescribedBuffer *> smallBufferPool;
    std::vector<DescribedBuffer *> smallBufferRecyclingBin;

    // std::unordered_map<uint64_t, WGPUBindGroup> bindGroupPool;
    // std::unordered_map<uint64_t, WGPUBindGroup> bindGroupRecyclingBin;

    DescribedBuffer *identityMatrix;
    DescribedSampler defaultSampler;

    DescribedBuffer *quadindicesCache;

    Texture whitePixel;

    MatrixBufferPair_stack matrixStack;
    RenderTexture_stack renderTargetStack;

    bool wantsToggleFullscreen;
    bool minimized;

    RenderTexture mainWindowRenderTarget;
    // RenderTexture currentDefaultRenderTarget;

    CreatedWindowMap input_map;

    int windowFlags = 0;
    // Frame timing / FPS
    int targetFPS;
    uint64_t total_frames = 0;
    uint64_t init_timestamp;

    int64_t last_timestamps[64] = {0};

    std::mutex drawmutex;
    GIFRecordState *grst;

    SubWindow mainWindow{};
    std::map<void *, SubWindow> createdSubwindows;
    SubWindow activeSubWindow{};

    bool closeFlag = false;
};
// end file include/renderstate.hpp