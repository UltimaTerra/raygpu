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

    DescribedBuffer *quadindicesCache{};

    Texture whitePixel;

    MatrixBufferPair_stack matrixStack;
    RenderTexture_stack renderTargetStack;

    bool wantsToggleFullscreen;
    bool minimized;

    RenderTexture mainWindowRenderTarget;
    // RenderTexture currentDefaultRenderTarget;

    std::unordered_map<void *, window_input_state> input_map;

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