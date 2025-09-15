#include <c_fs_utils.h>
#include <raygpu.h>
#include <internals.hpp>

#if SUPPORT_VULKAN_BACKEND == 1
#include "backend_vulkan/vulkan_internals.hpp"
#endif


const char shaderSource[] = R"(
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
    @location(2) normal: vec3f,
    @location(3) color: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};

struct LightBuffer {
    count: u32,
    positions: array<vec3f>
};

@group(0) @binding(0) var<uniform> Perspective_View: mat4x4f;
@group(0) @binding(1) var texture0: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;
@group(0) @binding(3) var<storage> modelMatrix: array<mat4x4f>;
@group(0) @binding(4) var<storage> lights: LightBuffer;
@group(0) @binding(5) var<storage> lights2: LightBuffer;

//Can be omitted
//@group(0) @binding(3) var<storage> storig: array<vec4f>;


@vertex
fn vs_main(@builtin(instance_index) instanceIdx : u32, in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = Perspective_View * 
                   modelMatrix[instanceIdx] *
    vec4f(in.position.xyz, 1.0f);
    out.color = in.color;
    out.uv = in.uv;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return textureSample(texture0, texSampler, in.uv).rgba * in.color;
}
)";

extern "C" const char vertexSourceGLSL[] = R"(#version 450

// Input attributes.
layout(location = 0) in vec3 in_position;  // position
layout(location = 1) in vec2 in_uv;        // texture coordinates
layout(location = 2) in vec3 in_normal;    // normal (unused)
layout(location = 3) in vec4 in_color;     // vertex color

// Outputs to fragment shader.
layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;
layout(location = 2) out vec3 out_normal;
// Uniform block for Perspective_View matrix (binding = 0).
layout(binding = 0) uniform Perspective_View {
    mat4 pvmatrix;
};

// Storage buffer for model matrices (binding = 3).
// Note: 'buffer' qualifier makes it a shader storage buffer.
layout(binding = 3) readonly buffer modelMatrix {
    mat4 modelMatrices[];  // Array of model matrices.
};

void main() {
    gl_PointSize = 1.0f;
    
    // Compute transformed position using instance-specific model matrix.
    gl_Position = pvmatrix * modelMatrices[gl_InstanceIndex] * vec4(in_position, 1.0);
    //gl_Position = vec4(0.001f * vec4(in_position, 1.0).xy - vec2(1, 1), 0, 1);
    //gl_Position = vec4(in_position, 1.0);
    frag_uv = in_uv;
    frag_color = in_color;
    out_normal = in_normal;
}
)";

extern "C" const char fragmentSourceGLSL[] = R"(#version 450
#extension GL_ARB_separate_shader_objects : enable  // Enable separate sampler objects if needed

// Inputs from vertex shader.
layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;

// Output fragment color.
layout(location = 0) out vec4 outColor;

// Texture and sampler, bound separately.
layout(binding = 1) uniform texture2D texture0;  // Texture (binding = 1)
layout(binding = 2) uniform sampler texSampler;    // Sampler (binding = 2)

void main() {
    // Sample the texture using the combined sampler.
    vec4 texColor = texture(sampler2D(texture0, texSampler), frag_uv);
    outColor = texColor * frag_color;
    //outColor = vec4(frag_color.xyz,1);
    //outColor = vec4(1,0,1,1);
}
)";



struct full_renderstate;

#include <renderstate.hpp>


void PollEvents(){
    #if SUPPORT_SDL2 != 0
    PollEvents_SDL2();
    #endif
    #if SUPPORT_SDL3 != 0
    PollEvents_SDL3();
    #endif
    #if SUPPORT_GLFW != 0
    PollEvents_GLFW();
    #endif
    #if SUPPORT_RGFW != 0
    PollEvents_RGFW();
    #endif
}
void* GetActiveWindowHandle(){
    if(g_renderstate.activeSubWindow.handle)return g_renderstate.activeSubWindow.handle;
    return g_renderstate.window;
}
bool WindowShouldClose(cwoid){
    if(g_renderstate.window == nullptr){ //headless
        return false;
    }
    #ifdef MAIN_WINDOW_SDL2
    return g_renderstate.closeFlag;
    #elif defined(MAIN_WINDOW_GLFW)
    return WindowShouldClose_GLFW(g_renderstate.window);
    #elif defined(MAIN_WINDOW_SDL3)
    return g_renderstate.closeFlag;
    #else
    return g_renderstate.closeFlag;
    #endif
}

























extern "C" Texture2D texShapes;

extern "C" DescribedPipeline* LoadPipelineForVAO_Vk(const char* vsSource, const char* fsSource, const VertexArray* vao, const ResourceTypeDescriptor* uniforms, uint32_t uniformCount, RenderSettings settings);

RGAPI void* InitWindow(uint32_t width, uint32_t height, const char* title){
    #if FORCE_HEADLESS == 1
    g_renderstate.windowFlags |= FLAG_HEADLESS;
    #endif
    //TODO: fix this, preferrably set correct format in InitWindow_SDL or GLFW
    g_renderstate.frameBufferFormat = PIXELFORMAT_UNCOMPRESSED_B8G8R8A8;
    if(g_renderstate.windowFlags & FLAG_STDOUT_TO_FFMPEG){
        if(IsATerminal(stdout)){
            TRACELOG(LOG_ERROR, "Refusing to pipe video output to terminal");
            TRACELOG(LOG_ERROR, "Try <program> | ffmpeg -y -f rawvideo -pix_fmt rgba -s %ux%u -r 60 -i - -vf format=yuv420p out.mp4", width, height);
            exit(1);
        }
        SetTraceLogFile(stderr);
    }
    TRACELOG(LOG_INFO, "Hello!");
    cfs_path workingDirectory = {0};
    cfs_get_working_directory(&workingDirectory);
    
    
    TRACELOG(LOG_INFO, "Working directory: %s", cfs_path_c_str(&workingDirectory));
    g_renderstate.last_timestamps[0] = (int64_t)NanoTime();
    
    InitBackend();
    g_renderstate.width = width;
    g_renderstate.height = height;
    
    g_renderstate.whitePixel = LoadTextureFromImage(GenImageChecker(Color{255,255,255,255}, Color{255,255,255,255}, 1, 1, 0));
    texShapes = g_renderstate.whitePixel;
    TraceLog(LOG_INFO, "Loaded whitepixel texture");

    //DescribedShaderModule tShader = LoadShaderModuleFromMemory(shaderSource);
    
    Matrix identity = MatrixIdentity();
    g_renderstate.identityMatrix = GenStorageBuffer(&identity, sizeof(Matrix));

    g_renderstate.grst = (GIFRecordState*)RL_CALLOC(1, 160);



    
    
    //void* window = nullptr;
    if(!(g_renderstate.windowFlags & FLAG_HEADLESS)){
        #if SUPPORT_GLFW == 1 || SUPPORT_SDL2 == 1 || SUPPORT_SDL3 == 1 || SUPPORT_RGFW == 1
        #ifdef MAIN_WINDOW_GLFW
        SubWindow createdWindow = InitWindow_GLFW((int)width, (int)height, title);
        #elif defined(MAIN_WINDOW_SDL3)
        // Initialize_SDL3();
        SubWindow createdWindow = InitWindow_SDL3(width, height, title);
        #elif defined(MAIN_WINDOW_RGFW)
        SubWindow createdWindow = InitWindow_RGFW(width, height, title);
        #else
        Initialize_SDL2();
        SubWindow createdWindow = InitWindow_SDL2(width, height, title);
        #endif
        
        WGPUSurface wSurface = (WGPUSurface)CreateSurfaceForWindow(createdWindow);
        createdWindow = g_renderstate.createdSubwindows.at(createdWindow.handle);
        createdWindow.surface = CompleteSurface(wSurface, (int)(width * createdWindow.scaleFactor), (int)(height * createdWindow.scaleFactor));
        
        g_renderstate.createdSubwindows[createdWindow.handle] = createdWindow;

        g_renderstate.window = (GLFWwindow*)createdWindow.handle;
        auto it = g_renderstate.createdSubwindows.find(g_renderstate.window);
        if(it == g_renderstate.createdSubwindows.end()){
            TRACELOG(LOG_FATAL, "Window creation error");
        }
        g_renderstate.mainWindow = &it->second;
        //g_wgpustate.surface = wgpu::Surface((WGPUSurface)g_wgpustate.createdSubwindows[glfwWin.handle].surface.surface);
        #endif
        

        
        
        //#ifndef __EMSCRIPTEN__
        
        //std::cout << "Supported Framebuffer Format: 0x" << std::hex << (WGPUTextureFormat)config.format << std::dec << "\n";        
    }else{
        g_renderstate.frameBufferFormat = PIXELFORMAT_UNCOMPRESSED_B8G8R8A8;
        g_renderstate.createdSubwindows[nullptr].surface = CreateHeadlessSurface(width, height, g_renderstate.frameBufferFormat);
    }
    


    ResourceTypeDescriptor uniforms[4] = {
        ResourceTypeDescriptor{uniform_buffer, 64, 0, readonly, format_or_sample_type::we_dont_know, WGPUShaderStage(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment)},
        ResourceTypeDescriptor{texture2d, 0, 1      , readonly, sample_f32,                          WGPUShaderStage(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment)},
        ResourceTypeDescriptor{texture_sampler, 0, 2, readonly, format_or_sample_type::we_dont_know, WGPUShaderStage(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment)},
        ResourceTypeDescriptor{storage_buffer, 64, 3, readonly, format_or_sample_type::we_dont_know, WGPUShaderStage(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment)}
    };

    AttributeAndResidence attrs[4] = {
        AttributeAndResidence{WGPUVertexAttribute{nullptr, WGPUVertexFormat_Float32x3, 0 * sizeof(float), 0}, 0, WGPUVertexStepMode_Vertex, true},
        AttributeAndResidence{WGPUVertexAttribute{nullptr, WGPUVertexFormat_Float32x2, 3 * sizeof(float), 1}, 0, WGPUVertexStepMode_Vertex, true},
        AttributeAndResidence{WGPUVertexAttribute{nullptr, WGPUVertexFormat_Float32x3, 5 * sizeof(float), 2}, 0, WGPUVertexStepMode_Vertex, true},
        AttributeAndResidence{WGPUVertexAttribute{nullptr, WGPUVertexFormat_Float32x4, 8 * sizeof(float), 3}, 0, WGPUVertexStepMode_Vertex, true},
    };
    
    //arraySetter(shaderInputs.per_vertex_sizes, {3,2,4});
    //arraySetter(shaderInputs.uniform_minsizes, {64, 0, 0, 0});
    //uarraySetter(shaderInputs.uniform_types, {uniform_buffer, texture2d, sampler, storage_buffer});
    auto colorTexture = LoadTextureEx(width, height, g_renderstate.frameBufferFormat, true);
    //g_wgpustate.mainWindowRenderTarget.texture = colorTexture;
    if(g_renderstate.windowFlags & FLAG_MSAA_4X_HINT)
        g_renderstate.mainWindowRenderTarget.colorMultisample = LoadTexturePro(width, height, g_renderstate.frameBufferFormat, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc, 4, 1);
    auto depthTexture = LoadTexturePro(width,
                                  height, 
                                  PIXELFORMAT_DEPTH_32_FLOAT, 
                                  WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc, 
                                  (g_renderstate.windowFlags & FLAG_MSAA_4X_HINT) ? 4 : 1,
                                  1
    );
    g_renderstate.mainWindowRenderTarget.depth = depthTexture;
    //init_full_renderstate(g_renderstate.rstate, shaderSource, attrs, 4, uniforms, sizeof(uniforms) / sizeof(ResourceTypeDescriptor), (WGPUTextureView)colorTexture.view, (WGPUTextureView)g_renderstate.mainWindowRenderTarget.depth.view);
    TRACELOG(LOG_INFO, "Renderstate inited");
    g_renderstate.renderExtentX = width;
    g_renderstate.renderExtentY = height;
    

    LoadFontDefault();
    for(size_t i = 0;i < 4;i++){
        g_renderstate.smallBufferPool.push_back(GenVertexBuffer(nullptr, sizeof(vertex) * VERTEX_BUFFER_CACHE_SIZE));
    }

    vboptr = (vertex*)std::calloc(10000, sizeof(vertex));
    vboptr_base = vboptr;
    renderBatchVBO = GenVertexBuffer(nullptr, size_t(RENDERBATCH_SIZE) * sizeof(vertex));
    
    renderBatchVAO = LoadVertexArray();
    VertexAttribPointer(renderBatchVAO, renderBatchVBO, 0, WGPUVertexFormat_Float32x3, 0 * sizeof(float), WGPUVertexStepMode_Vertex);
    VertexAttribPointer(renderBatchVAO, renderBatchVBO, 1, WGPUVertexFormat_Float32x2, 3 * sizeof(float), WGPUVertexStepMode_Vertex);
    VertexAttribPointer(renderBatchVAO, renderBatchVBO, 2, WGPUVertexFormat_Float32x3, 5 * sizeof(float), WGPUVertexStepMode_Vertex);
    VertexAttribPointer(renderBatchVAO, renderBatchVBO, 3, WGPUVertexFormat_Float32x4, 8 * sizeof(float), WGPUVertexStepMode_Vertex);

    constexpr WGPUColor opaqueBlack{
        .r = 0.0,
        .g = 0.0,
        .b = 0.0,
        .a = 1.0
    };
    g_renderstate.renderpass = LoadRenderpassEx(GetDefaultSettings(), false, opaqueBlack, false, 0.0f);

    g_renderstate.clearPass = LoadRenderpassEx(GetDefaultSettings(), true, opaqueBlack, true, 1.0f);
    //}
    //state->clearPass.rca->clearValue = WGPUColor{1.0, 0.4, 0.2, 1.0};
    //state->clearPass.rca->loadOp = WGPULoadOp_Clear;
    //state->clearPass.rca->storeOp = WGPUStoreOp_Store;
    //state->activeRenderpass = nullptr;
    #if SUPPORT_GLSL_PARSER == 1
    ShaderSources defaultGLSLSource zeroinit;
    defaultGLSLSource.sourceCount = 2;
    defaultGLSLSource.sources[0].data = vertexSourceGLSL;
    defaultGLSLSource.sources[0].sizeInBytes = std::strlen(vertexSourceGLSL);
    defaultGLSLSource.sources[0].stageMask = WGPUShaderStage_Vertex;
    defaultGLSLSource.sources[1].data = fragmentSourceGLSL;
    defaultGLSLSource.sources[1].sizeInBytes = std::strlen(fragmentSourceGLSL);
    defaultGLSLSource.sources[1].stageMask = WGPUShaderStage_Fragment;
    //g_renderstate.defaultShader = LoadPipelineForVAOEx(defaultGLSLSource, renderBatchVAO, uniforms, sizeof(uniforms) / sizeof(ResourceTypeDescriptor), GetDefaultSettings());
    g_renderstate.defaultShader = LoadShaderFromMemory(vertexSourceGLSL, fragmentSourceGLSL);
    #elif SUPPORT_WGSL_PARSER == 1
    ShaderSources defaultWGSLSource zeroinit;
    defaultWGSLSource.sourceCount = 1;
    defaultWGSLSource.sources[0].data = shaderSource;
    defaultWGSLSource.sources[0].sizeInBytes = std::strlen(shaderSource);
    defaultWGSLSource.sources[0].stageMask = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    //g_renderstate.defaultShader = LoadPipeline(shaderSource);
    g_renderstate.defaultShader = LoadPipelineEx(
        shaderSource,
        renderBatchVAO->attributes,
        renderBatchVAO->attributes_count,
        uniforms,
        sizeof(uniforms) / sizeof(ResourceTypeDescriptor),
        GetDefaultSettings()
    );

    #else
    #error "Must support either glsl or wgsl"
    #endif
    g_renderstate.activeShader = g_renderstate.defaultShader;
    size_t quadCount = 2000;
    g_renderstate.quadindicesCache = GenBufferEx(nullptr, quadCount * 6 * sizeof(uint32_t), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index);//allocnew(DescribedBuffer);    //WGPUBufferDescriptor vbmdesc{};
    std::vector<uint32_t> indices(6 * quadCount);
    for(size_t i = 0;i < quadCount;i++){
        indices[i * 6 + 0] = (i * 4 + 0);
        indices[i * 6 + 1] = (i * 4 + 1);
        indices[i * 6 + 2] = (i * 4 + 3);
        indices[i * 6 + 3] = (i * 4 + 1);
        indices[i * 6 + 4] = (i * 4 + 2);
        indices[i * 6 + 5] = (i * 4 + 3);
    }
    BufferData(g_renderstate.quadindicesCache, indices.data(), 6 * quadCount * sizeof(uint32_t));
    //g_renderstate.quadindicesCache->usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
    //WGPUCommandEncoderDescriptor cedesc{};
    //cedesc.label = STRVIEW("Global Command Encoder");
    //g_renderstate.renderpass.cmdEncoder = wgpuDeviceCreateCommandEncoder(g_wgpustate.device.Get(), &cedesc);
    Matrix m = ScreenMatrix(width, height);
    static_assert(sizeof(Matrix) == 64, "non 4 byte floats? or what");
    g_renderstate.matrixStack.push(std::pair<Matrix, WGPUBuffer>{});
    //g_wgpustate.defaultScreenMatrix = GenUniformBuffer(&m, sizeof(Matrix));
    //SetUniformBuffer(0, g_wgpustate.defaultScreenMatrix);
    SetTexture(1, g_renderstate.whitePixel);
    Matrix iden = MatrixIdentity();
    SetStorageBufferData(3, &iden, 64);
    
    DescribedSampler sampler = LoadSampler(TEXTURE_WRAP_REPEAT, TEXTURE_FILTER_BILINEAR);
    g_renderstate.defaultSampler = sampler;
    SetSampler(2, sampler);
    g_renderstate.init_timestamp = NanoTime();
    g_renderstate.currentSettings = GetDefaultSettings();
    #ifndef __EMSCRIPTEN__
    if((g_renderstate.windowFlags & FLAG_VSYNC_HINT))
        SetTargetFPS(60);
    //if(!(g_wgpustate.windowFlags & FLAG_HEADLESS) && (g_wgpustate.windowFlags & FLAG_VSYNC_HINT)){
    //    auto rate = glfwGetVideoMode(glfwGetPrimaryMonitor())->refreshRate;
    //    SetTargetFPS(rate);
    //}
    else
        SetTargetFPS(0);
    
    #endif
    #ifndef __EMSCRIPTEN__
    return nullptr;
    #else
    return nullptr;
    #endif
}
/**
  @return WGPUSurface or WGPUSurface (void*)

 */
extern "C" void* CreateSurfaceForWindow(SubWindow window){
    void* surfacePtr = nullptr;
    window.scaleFactor = 1; // default
    switch (window.type){
        case windowType_sdl3:
        #if SUPPORT_SDL3 == 1
        surfacePtr = CreateSurfaceForWindow_SDL3(window.handle);
        #else
        rg_trap();
        #endif
        break;
        case windowType_glfw:
        #if SUPPORT_GLFW == 1
        surfacePtr = CreateSurfaceForWindow_GLFW(window.handle);
        #else
        rg_trap();
        #endif
        break;
        case windowType_sdl2:
        #if SUPPORT_SDL2 == 1
        surfacePtr = CreateSurfaceForWindow_SDL2(window.handle);
        #else
        rg_trap();
        #endif
        case windowType_rgfw:
        #if SUPPORT_RGFW == 1
        surfacePtr = CreateSurfaceForWindow_RGFW(window.handle);
        #else
        rg_trap();
        #endif
        break;
    }
    TRACELOG(LOG_INFO, "Created surface: %p", surfacePtr);
    return surfacePtr;
}
extern "C" void CharCallback(void* window, unsigned int codePoint){
    g_renderstate.input_map[window].charQueue.push_back((int)codePoint);
}
extern "C" SubWindow OpenSubWindow(int width, int height, const char* title){
    SubWindow createdWindow zeroinit;
    #ifdef MAIN_WINDOW_GLFW
    createdWindow = OpenSubWindow_GLFW(width, height, title);
    #elif defined(MAIN_WINDOW_SDL2)
    createdWindow = OpenSubWindow_SDL2(width, height, title);
    #elif defined(MAIN_WINDOW_SDL3)
    createdWindow = OpenSubWindow_SDL3(width, height, title);
    rassert(createdWindow.handle != nullptr, "Returned window can't have null handle");
    #endif
    void* wgpu_or_wgpu_surface = CreateSurfaceForWindow(createdWindow);
    #if SUPPORT_WGPU_BACKEND == 1 || SUPPORT_VULKAN_BACKEND == 1
    WGPUSurface wSurface = (WGPUSurface)wgpu_or_wgpu_surface;
    g_renderstate.createdSubwindows[createdWindow.handle].surface = CompleteSurface(wSurface, width, height);
    #else
    WGPUSurface vSurface = (WGPUSurface)wgpu_or_wgpu_surface;
    WGPUSurfaceConfiguration config{};
    config.device = g_vulkanstate.device;
    config.width = width;
    config.height = height;
    config.format = WGPUTextureFormat_BGRA8Unorm;
    config.presentMode = WGPUPresentMode_Immediate;
    wgpuSurfaceConfigure(vSurface, &config);
    FullSurface fsurface zeroinit;
    fsurface.surfaceConfig = config;
    fsurface.surface = vSurface;
    if(g_renderstate.windowFlags & FLAG_MSAA_4X_HINT)
        fsurface.renderTarget.colorMultisample = LoadTexturePro(width, height, g_renderstate.frameBufferFormat, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc, 4, 1);
    fsurface.renderTarget.depth = LoadTexturePro(width,
                                  height, 
                                  PIXELFORMAT_DEPTH_32_FLOAT, 
                                  WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc, 
                                  (g_renderstate.windowFlags & FLAG_MSAA_4X_HINT) ? 4 : 1,
                                  1
    );
    fsurface.renderTarget.colorAttachmentCount = 1;
    //fsurface.renderTarget.depth = LoadDepthTexture(width, height);
    g_renderstate.createdSubwindows[createdWindow.handle].surface = fsurface;
    #endif
    
    return g_renderstate.createdSubwindows[createdWindow.handle];
}
extern "C" void ToggleFullscreenImpl(){
    #ifdef MAIN_WINDOW_GLFW
    ToggleFullscreen_GLFW();
    #elif defined(MAIN_WINDOW_SDL2)
    ToggleFullscreen_SDL2();
    #elif defined(MAIN_WINDOW_SDL3)
    ToggleFullscreen_SDL3();
    #endif
}
extern "C" void ToggleFullscreen(){
    g_renderstate.wantsToggleFullscreen = true;
}
Vector2 GetTouchPosition(int index){
    #ifdef MAIN_WINDOW_GLFW
    return Vector2{0,0};//GetTouchPointCount_GLFW(index);
    #elif defined(MAIN_WINDOW_SDL2)
    return GetTouchPosition_SDL2(index);
    #else
    return Vector2{0, 0};
    #endif
}
int GetTouchPointCount(cwoid){
    return static_cast<int>(g_renderstate.input_map[g_renderstate.activeSubWindow.handle].touchPoints.size());
}
int GetMonitorWidth(cwoid){
    #ifdef MAIN_WINDOW_GLFW
    return GetMonitorWidth_GLFW();
    #elif defined(MAIN_WINDOW_SDL2)
    return GetMonitorWidth_SDL2();
    #elif defined (MAIN_WINDOW_SDL3)
    return GetMonitorWidth_SDL3();
    #endif
    return 0;
}
int GetMonitorHeight(cwoid){
    #ifdef MAIN_WINDOW_GLFW
    return GetMonitorHeight_GLFW();
    #elif defined(MAIN_WINDOW_SDL2)
    return GetMonitorHeight_SDL2();
    #elif defined (MAIN_WINDOW_SDL3)
    return GetMonitorHeight_SDL3();
    #endif
    return 0;
}
void SetWindowShouldClose(){
    #ifdef MAIN_WINDOW_GLFW
    return SetWindowShouldClose_GLFW(g_renderstate.window);
    #elif defined(MAIN_WINDOW_SDL2)
    g_renderstate.closeFlag = true;
    #endif
}



extern "C" size_t GetPixelSizeInBytes(PixelFormat format) {
    switch(format){
        case PixelFormat::PIXELFORMAT_UNCOMPRESSED_B8G8R8A8:
        case PixelFormat::PIXELFORMAT_UNCOMPRESSED_B8G8R8A8_SRGB:
        case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
        case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8_SRGB:
        return 4;
        case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
        return 8;
        case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
        return 16;

        case PixelFormat::GRAYSCALE:
        return 2;
        case PixelFormat::RGB8:
        return 3;
        case PixelFormat::PIXELFORMAT_DEPTH_24_PLUS:
        case PixelFormat::PIXELFORMAT_DEPTH_32_FLOAT:
        return 4;
        case PixelFormat::PixelFormat_Force32:
        return 0;
    }
}


