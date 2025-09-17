/*
 * MIT License
 * 
 * Copyright (c) 2025 @manuel5975p
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"
#include <macros_and_constants.h>
#include <c_fs_utils.h>
#include <cstddef>
#include <raygpu.h>
#include <cassert>
#include <vector>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cstdarg>
#include <thread>
#include <unordered_map>
#include <deque>
#include <map>

#include <external/stb_image_write.h>
#include <external/stb_image.h>
#include <external/sinfl.h>
#include <external/sdefl.h>
#include <internals.hpp>
#include <external/msf_gif.h>
extern "C" void ToggleFullscreenImpl(cwoid);
#ifdef __EMSCRIPTEN__
#endif  // __EMSCRIPTEN__
#include <renderstate.hpp>

//#include <enum_translation.h>

renderstate g_renderstate{};

#define swap_uint32(val) (((((((uint32_t)(val)) << 8) & 0xFF00FF00 ) | ((((uint32_t)(val)) >> 8) & 0xFF00FF)) << 16) | ((((((uint32_t)(val)) << 8) & 0xFF00FF00 ) | ((((uint32_t)(val)) >> 8) & 0xFF00FF)) >> 16))

ShaderSourceType detectShaderLanguage(const void* data, size_t sizeInBytes){
    if(data == 0 || sizeInBytes == 0){
        return sourceTypeUnknown;
    }

    if(sizeInBytes >= 4){
        const uint32_t* u32ptr = reinterpret_cast<const uint32_t*>(data);
        if(*u32ptr == 0x07230203 || *u32ptr == swap_uint32(0x07230203)){
            return sourceTypeSPIRV;
        }
    }
    char* c_str = (char*)(data);
    if(strstr(c_str, "@location") || strstr(c_str, "@location")){
        return sourceTypeWGSL;
    }
    if(strstr(c_str, "@group(") || strstr(c_str, "@binding(")){
        return sourceTypeWGSL;
    }
    
    else if(strstr(c_str, "#version")){
        return sourceTypeGLSL;
    }

    else{
        return sourceTypeUnknown;
    }
}

void detectShaderLanguage(ShaderSources* sourcesPointer){
    for(uint32_t i = 0;i < sourcesPointer->sourceCount;i++){
        ShaderSourceType srctype = (sourcesPointer->sources[i].data == nullptr) ? sourceTypeUnknown : detectShaderLanguage(sourcesPointer->sources[i].data, sourcesPointer->sources[i].sizeInBytes);
        if(srctype != sourceTypeUnknown){
            sourcesPointer->language = srctype;
            return;
        }
    }
}

typedef struct GIFRecordState{
    uint64_t delayInCentiseconds;
    uint64_t lastFrameTimestamp;
    MsfGifState msf_state;
    uint64_t numberOfFrames;
    bool recording;
}GIFRecordState;

void startRecording(GIFRecordState* grst, uint64_t delayInCentiseconds){
    if(grst->recording){
        TRACELOG(LOG_WARNING, "Already recording");
        return;
    }
    else{
        grst->numberOfFrames = 0;
    }
    msf_gif_bgra_flag = true;
    grst->msf_state = CLITERAL(MsfGifState){0};
    grst->delayInCentiseconds = delayInCentiseconds;
    msf_gif_begin(&grst->msf_state, GetScreenWidth(), GetScreenHeight());
    grst->recording = true;
}

void addScreenshot(GIFRecordState* grst, WGPUTexture tex){
    //#ifdef __EMSCRIPTEN__
    //if(grst->numberOfFrames > 0)
    //    msf_gif_frame(&grst->msf_state, (uint8_t*)fbLoad.data, grst->delayInCentiseconds, 8, fbLoad.rowStrideInBytes);
    //#endif
    grst->lastFrameTimestamp = NanoTime();
    Image fb = LoadImageFromTextureEx(tex, 0);
    ImageFormat(&fb, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    //#ifndef __EMSCRIPTEN__
    msf_gif_frame(&grst->msf_state, (uint8_t*)fb.data, grst->delayInCentiseconds, 8, fb.rowStrideInBytes);
    //#endif
    UnloadImage(fb);
    ++grst->numberOfFrames;
}

void endRecording(GIFRecordState* grst, const char* filename){
    MsfGifResult result = msf_gif_end(&grst->msf_state);
    
    if (result.data) {
    #ifdef __EMSCRIPTEN__
        // Use EM_ASM to execute JavaScript for downloading the GIF
        // Allocate a buffer in the Emscripten heap and copy the GIF data
        // Then create a Blob and trigger a download in the browser

        // Ensure that the data is null-terminated if necessary
        // You might need to allocate memory in the Emscripten heap
        // and copy the data there, but for simplicity, we'll pass the pointer and size
        EM_ASM({
            var fname   = UTF8ToString($0);
            var dataPtr = $1 >>> 0;
            var dataLen = $2 >>> 0;
            // Make a copy so it’s safe after C frees the buffer
            var bytes = HEAPU8.slice(dataPtr, dataPtr + dataLen);
            var blob = new Blob([bytes], { type: 'image/gif' });
            var link = document.createElement('a');
            link.href = URL.createObjectURL(blob);
            link.download = fname;
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
            URL.revokeObjectURL(link.href);
        }, filename, (uintptr_t)result.data, (int)result.dataSize, filename, (uintptr_t)result.data, (int)result.dataSize);
    #else
        // Native file system approach
        FILE * fp = fopen(filename, "wb");
        if (fp) {
            fwrite(result.data, 1, result.dataSize, fp);
            fclose(fp);
        } else {
            // Handle file open error if necessary
            fprintf(stderr, "Failed to open file: %s\n", filename);
        }
    #endif
    }

    //Free the GIF result resources
    msf_gif_free(result);

    // Update the recording state
    grst->recording = false;

    // Optionally clear the GIFRecordState structure
    memset(grst, 0, sizeof(GIFRecordState));
}
Vector2 nextuv;
Vector3 nextnormal;
Vector4 nextcol;
vertex* vboptr = 0;
vertex* vboptr_base = 0;
#if SUPPORT_VULKAN_BACKEND == 1
WGPUBuffer vbo_buf = 0;
#endif
VertexArray* renderBatchVAO;
DescribedBuffer* renderBatchVBO;
PrimitiveType current_drawmode;
//DescribedBuffer vbomap;

#if RAYGPU_NO_INLINE_FUNCTIONS == 1
void rlColor4f(float r, float g, float b, float alpha){
    nextcol.x = r;
    nextcol.y = g;
    nextcol.z = b;
    nextcol.w = alpha;
}
void rlColor4ub(uint8_t r, uint8_t g, uint8_t b, uint8_t a){
    nextcol.x = ((float)((int)r)) / 255.0f;
    nextcol.y = ((float)((int)g)) / 255.0f;
    nextcol.z = ((float)((int)b)) / 255.0f;
    nextcol.w = ((float)((int)a)) / 255.0f;
}
void rlColor3f(float r, float g, float b){
    rlColor4f(r, g, b, 1.0f);
}

void rlTexCoord2f(float u, float v){
    nextuv.x = u;
    nextuv.y = v;
}

void rlVertex2f(float x, float y){
    *(vboptr++) = CLITERAL(vertex){{x, y, 0}, nextuv, nextnormal, nextcol};
    if(UNLIKELY(vboptr - vboptr_base >= (ptrdiff_t)RENDERBATCH_SIZE)){
        drawCurrentBatch();
    }
}
void rlNormal3f(float x, float y, float z){
    nextnormal.x = x;
    nextnormal.y = y;
    nextnormal.z = z;
}
void rlVertex3f(float x, float y, float z){
    *(vboptr++) = CLITERAL(vertex){{x, y, z}, nextuv, nextnormal, nextcol};
    if(UNLIKELY(vboptr - vboptr_base >= (ptrdiff_t)RENDERBATCH_SIZE)){
        drawCurrentBatch();
    }
}
#endif
RGAPI VertexArray* LoadVertexArray(){
    VertexArray* ret = callocnew(VertexArray);
    new (ret) VertexArray;
    return ret;
}
RGAPI void VertexAttribPointer(VertexArray* array, DescribedBuffer* buffer, uint32_t attribLocation, WGPUVertexFormat format, uint32_t offset, WGPUVertexStepMode stepmode){
    VertexArray_add(array, buffer, attribLocation, format, offset, stepmode);
}
RGAPI void BindVertexArray(VertexArray* va){
    BindShaderVertexArray(GetActiveShader(), va);
}

RGAPI void BindShaderVertexArray(Shader shader, VertexArray* va){
    GetShaderImpl(shader)->state.vertexAttributes = va->attributes;
    GetShaderImpl(shader)->state.vertexAttributeCount = va->attributes_count;

    for(unsigned i = 0; i < va->buffers_count; i++){
        bool shouldBind = false;

        // Check if any enabled attribute uses this buffer
        for(size_t j = 0; j < va->attributes_count;j++){
            const AttributeAndResidence* attr = va->attributes + j; 
            if(attr->bufferSlot == i){
                if(attr->enabled){
                    shouldBind = true;
                    break;
                }
                else{
                }
            }
        }

        if(shouldBind){
            const BufferEntry* bufferPair = va->buffers + i;
            RenderPassSetVertexBuffer(GetActiveRenderPass(), i, bufferPair->buffer, 0);
        } else {
            TRACELOG(LOG_DEBUG, "Buffer slot %u not bound (no enabled attributes use it).", i);
        }
    }
    
}
RGAPI void EnableVertexAttribArray(VertexArray* array, uint32_t attribLocation){
    VertexArray_enableAttribute(array, attribLocation);
}
RGAPI void DisableVertexAttribArray(VertexArray* array, uint32_t attribLocation){
    VertexArray_disableAttribute(array, attribLocation);
}
RGAPI void DrawArrays(PrimitiveType drawMode, uint32_t vertexCount){
    Shader activeShader = GetActiveShader();
    ShaderImpl* activeShaderImpl = GetShaderImpl(activeShader);
    
    BindShader(activeShader, drawMode);

    if(activeShaderImpl->bindGroup.needsUpdate){
        RenderPassSetBindGroup(GetActiveRenderPass(), 0, &activeShaderImpl->bindGroup);
    }
    RenderPassDraw(GetActiveRenderPass(), vertexCount, 1, 0, 0);
}
RGAPI void DrawArraysIndexed(PrimitiveType drawMode, DescribedBuffer indexBuffer, uint32_t vertexCount){
    Shader activeShader = GetActiveShader();
    ShaderImpl* activeShaderImpl = GetShaderImpl(activeShader);
    BindShader(activeShader, drawMode);

    if(activeShaderImpl->bindGroup.needsUpdate){
        RenderPassSetBindGroup(GetActiveRenderPass(), 0, &activeShaderImpl->bindGroup);
    }
    RenderPassSetIndexBuffer(GetActiveRenderPass(), &indexBuffer, WGPUIndexFormat_Uint32, 0);
    RenderPassDrawIndexed(GetActiveRenderPass(), vertexCount, 1, 0, 0, 0);
}
RGAPI void DrawArraysIndexedInstanced(PrimitiveType drawMode, DescribedBuffer indexBuffer, uint32_t vertexCount, uint32_t instanceCount){
    Shader activeShader = GetActiveShader();
    ShaderImpl* activeShaderImpl = GetShaderImpl(activeShader);
    BindShader(activeShader, drawMode);
    
    RenderPassSetBindGroup(GetActiveRenderPass(), 0, &activeShaderImpl->bindGroup);
    RenderPassSetIndexBuffer(GetActiveRenderPass(), &indexBuffer, WGPUIndexFormat_Uint32, 0);
    RenderPassDrawIndexed(GetActiveRenderPass(), vertexCount, instanceCount, 0, 0, 0);
}

RGAPI void DrawArraysInstanced(PrimitiveType drawMode, uint32_t vertexCount, uint32_t instanceCount){
    Shader activeShader = GetActiveShader();
    ShaderImpl* activeShaderImpl = GetShaderImpl(activeShader);
    BindShader(activeShader, drawMode);
    
    RenderPassDraw(GetActiveRenderPass(), vertexCount, instanceCount, 0, 0);
}

RGAPI Texture GetDepthTexture(){
    return g_renderstate.renderTargetStack.peek().depth;
}
RGAPI Texture GetMultisampleColorTarget(){
    return g_renderstate.renderTargetStack.peek().colorMultisample;
}


RGAPI void drawCurrentBatch(){
    size_t vertexCount = vboptr - vboptr_base;
    //std::cout << "vcoun = " << vertexCount << "\n";
    if(vertexCount == 0)return;
    #if SUPPORT_VULKAN_BACKEND == 8
    DescribedBuffer* vbo = UpdateVulkanRenderbatch();
    constexpr bool allocated_via_pool = false;
    #else
    DescribedBuffer* vbo = nullptr;
    bool allocated_via_pool = false;
    if(vertexCount < VERTEX_BUFFER_CACHE_SIZE && !g_renderstate.smallBufferPool.empty()){
        allocated_via_pool = true;
        vbo = g_renderstate.smallBufferPool.back();
        g_renderstate.smallBufferPool.pop_back();
        wgpuQueueWriteBuffer(GetQueue(), (WGPUBuffer)vbo->buffer, 0, vboptr_base, vertexCount * sizeof(vertex));
    }
    else{
        vbo = GenVertexBuffer(vboptr_base, vertexCount * sizeof(vertex));
    }
    #endif

    renderBatchVAO->buffers[0].buffer = vbo;
    SetStorageBuffer(3, g_renderstate.identityMatrix);
    Shader activeShader = GetActiveShader();
    ShaderImpl* activeShaderImpl = GetShaderImpl(activeShader);
    UpdateBindGroup(&activeShaderImpl->bindGroup);
    switch(current_drawmode){
        case RL_LINES:{

            //TODO: Line texturing is currently disable in all DrawLine... functions
            SetTexture(1, g_renderstate.whitePixel);
            BindShaderVertexArray(activeShader, renderBatchVAO);
            BindShader(activeShader, RL_LINES);
            DrawArrays(RL_LINES, vertexCount);
            
            activeShaderImpl->bindGroup.needsUpdate = true;
        }break;
        case RL_TRIANGLE_STRIP:{
            BindShader(activeShader, RL_TRIANGLE_STRIP);
            BindShaderVertexArray(activeShader, renderBatchVAO);
            DrawArrays(RL_TRIANGLE_STRIP, vertexCount);
            break;
        }
        
        case RL_TRIANGLES:{
            BindShaderVertexArray(GetActiveShader(), renderBatchVAO);
            BindShader(GetActiveShader(), RL_TRIANGLES);
            DrawArrays(RL_TRIANGLES, vertexCount);
        } break;
        case RL_QUADS:{
            const size_t quadCount = vertexCount / 4;
            if(g_renderstate.quadindicesCache->size < 6 * quadCount * sizeof(uint32_t)){
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
            }
            const DescribedBuffer* ibuf = g_renderstate.quadindicesCache;
            BindShaderVertexArray(GetActiveShader(), renderBatchVAO);
            DrawArraysIndexed(RL_TRIANGLES, *ibuf, quadCount * 6);

            
        } break;
        default:break;
    }
    if(!allocated_via_pool){
        #if SUPPORT_VULKAN_BACKEND == 8
        PushUsedBuffer(vbo->buffer);
        #endif
        UnloadBuffer(vbo);
    }
    else{
        g_renderstate.smallBufferRecyclingBin.push_back(vbo);
    }
    vboptr = vboptr_base;
}

void LoadIdentity(cwoid){
    g_renderstate.matrixStack.peek().first = MatrixIdentity();
}
void PushMatrix(){
    g_renderstate.matrixStack.push(std::pair<Matrix, WGPUBuffer>{});
}
void PopMatrix(){
    g_renderstate.matrixStack.pop();
}
Matrix GetMatrix(){
    return g_renderstate.matrixStack.peek().first;
}
Matrix* GetMatrixPtr(){
    return &g_renderstate.matrixStack.peek().first;
}
void SetMatrix(Matrix m){
    g_renderstate.matrixStack.peek().first = m;
}

void adaptRenderPass(DescribedRenderpass* drp, const ModifiablePipelineState& settings){
    drp->settings = settings.settings;
}

void FillReflectionInfo(DescribedShaderModule* module){

}

DescribedShaderModule LoadShaderModule(ShaderSources sources){
    
    DescribedShaderModule ret zeroinit;
    
    
    if(sources.language == ShaderSourceType::sourceTypeUnknown){
        detectShaderLanguage(&sources);
        rassert(sources.language != ShaderSourceType::sourceTypeUnknown, "Shader source must be detectable: GLSL requires #version, wgsl @binding or @location token");
    }
    
    switch (sources.language){
        case sourceTypeGLSL:
        #if SUPPORT_GLSL_PARSER == 1
        return LoadShaderModuleGLSL(sources);
        #else
        TRACELOG(LOG_FATAL, "Library was built without GLSL support, recompile with SUPPORT_GLSL_PARSER=1");
        #endif
        return ret;
        case sourceTypeWGSL:
        #if SUPPORT_WGSL_PARSER == 1
        return LoadShaderModuleWGSL(sources);
        #else
        TRACELOG(LOG_FATAL, "Library was built without WGSL support, recompile with SUPPORT_WGSL_PARSER=1");
        #endif
        return ret;
        case sourceTypeSPIRV:
        return LoadShaderModuleSPIRV(sources);
        default: rg_unreachable();
    }

    return ret;
}

/**
 * @brief This function determines compatibility between RenderSettings
 * @details 
 * The purpose of this function is to determine whether the attachment states of a renderpass and a pipeline is compatible.
 * For this, the multisample state and depth state need to match (and also stencil but not implemented right now) 
 * 
 * @param settings1 
 * @param settings2 
 * @return true 
 * @return false 
 */
static inline bool RenderSettingsCompatible(const ModifiablePipelineState& state, RenderSettings settings2){
    return state.settings.sampleCount == settings2.sampleCount &&
           state.settings.depthTest == settings2.depthTest;
}

RGAPI void BeginShaderMode(Shader shader){
    drawCurrentBatch();
    ShaderImpl* impl = GetShaderImpl(shader);
    if(!RenderSettingsCompatible(impl->state, g_renderstate.renderpass.settings)){
        EndRenderpass();
        adaptRenderPass(&g_renderstate.renderpass, impl->state);
        BeginRenderpass();
    }
    g_renderstate.activeShader = shader;
    uint32_t location = GetUniformLocation(shader, RL_DEFAULT_SHADER_UNIFORM_NAME_PROJECTION_VIEW);
    if(location != LOCATION_NOT_FOUND){
        SetUniformBufferData(location, &g_renderstate.matrixStack.peek().first, sizeof(Matrix));
    }
    //BindPipeline(pipeline, drawMode);
}
RGAPI void EndShaderMode(){
    drawCurrentBatch();
    ShaderImpl* defaultShaderImpl = GetShaderImpl(g_renderstate.defaultShader);
    if(!RenderSettingsCompatible(defaultShaderImpl->state, g_renderstate.renderpass.settings)){
        EndRenderpass();
        adaptRenderPass(&g_renderstate.renderpass, defaultShaderImpl->state);
        BeginRenderpass();
    }
    g_renderstate.activeShader = g_renderstate.defaultShader;
    //BindPipeline(g_renderstate.activePipeline, g_renderstate.activePipeline->lastUsedAs);
}

RGAPI void DisableDepthTest(cwoid){
    drawCurrentBatch();
    g_renderstate.currentSettings.depthTest = 0;
}
RGAPI void BeginBlendMode(rlBlendMode blendMode) {
    // Get a reference to the blend state part of the current settings
    auto& blendState = g_renderstate.currentSettings.blendState;
    
    // Default common operation
    blendState.color.operation = (WGPUBlendOperation_Add);
    blendState.alpha.operation = (WGPUBlendOperation_Add);

    switch (blendMode) {
        case BLEND_ALPHA:
            // Alpha blend: SrcColor * SrcAlpha + DstColor * (1 - SrcAlpha)
            // Alpha blend: SrcAlpha * 1 + DstAlpha * (1 - SrcAlpha)
            blendState.color.srcFactor = (WGPUBlendFactor_SrcAlpha);
            blendState.color.dstFactor = (WGPUBlendFactor_OneMinusSrcAlpha);
            blendState.alpha.srcFactor = (WGPUBlendFactor_One); // Often One or SrcAlpha
            blendState.alpha.dstFactor = (WGPUBlendFactor_OneMinusSrcAlpha);
            // Operation is already BlendOperation_Add
            break;

        case BLEND_ADDITIVE:
            // Additive blend: SrcColor * SrcAlpha + DstColor * 1
            // Alpha blend: SrcAlpha * 1 + DstAlpha * 1 (or often just passes Dest alpha)
            // This matches glBlendFunc(GL_SRC_ALPHA, GL_ONE) and glBlendEquation(GL_FUNC_ADD)
            // Often, additive alpha is just (One, One) or preserves dest alpha (Zero, One)
            // Let's assume (SrcAlpha, One) for color and (One, One) for alpha for brightness.
            blendState.color.srcFactor = (WGPUBlendFactor_SrcAlpha);
            blendState.color.dstFactor = (WGPUBlendFactor_One);
            blendState.alpha.srcFactor = (WGPUBlendFactor_One); // Could be SrcAlpha or Zero depending on desired alpha result
            blendState.alpha.dstFactor = (WGPUBlendFactor_One); // Could be One or Zero
            // Operation is already BlendOperation_Add
            break;

        case BLEND_MULTIPLIED:
            // Multiplied blend: SrcColor * DstColor + DstColor * (1 - SrcAlpha)
            // Alpha blend: SrcAlpha * 0 + DstAlpha * 1 (keeps destination alpha)
            // Matches glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE) commonly used for multiply
            // The original code used glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA) which would affect alpha too.
            // Let's implement the common separate logic for better results.
            blendState.color.srcFactor = (WGPUBlendFactor_Dst);
            blendState.color.dstFactor = (WGPUBlendFactor_OneMinusSrcAlpha);
            blendState.alpha.srcFactor = (WGPUBlendFactor_Zero); // Keeps destination alpha
            blendState.alpha.dstFactor = (WGPUBlendFactor_One);
            // Operation is already BlendOperation_Add
            break;

        case BLEND_ADD_COLORS:
            // Add colors blend: SrcColor * 1 + DstColor * 1
            // Alpha blend: SrcAlpha * 1 + DstAlpha * 1
            // Matches glBlendFunc(GL_ONE, GL_ONE) and glBlendEquation(GL_FUNC_ADD)
            blendState.color.srcFactor = (WGPUBlendFactor_One);
            blendState.color.dstFactor = (WGPUBlendFactor_One);
            blendState.alpha.srcFactor = (WGPUBlendFactor_One);
            blendState.alpha.dstFactor = (WGPUBlendFactor_One);
            // Operation is already BlendOperation_Add
            break;

        case BLEND_SUBTRACT_COLORS:
            // Subtract colors blend: DstColor * 1 - SrcColor * 1 (Note: Usually Reverse Subtract: Src * 1 - Dst * 1 is less common)
            // Let's assume Dst - Src based on common SUBTRACT usage, but GL_FUNC_SUBTRACT is Src - Dst.
            // GL_FUNC_SUBTRACT: result = src * srcFactor - dst * dstFactor
            // Alpha blend: DstAlpha * 1 - SrcAlpha * 1 (or Add alpha?)
            // Matches glBlendFunc(GL_ONE, GL_ONE) and glBlendEquation(GL_FUNC_SUBTRACT)
            // Applying SUBTRACT operation to both color and alpha based on glBlendEquation.
            blendState.color.srcFactor = (WGPUBlendFactor_One);
            blendState.color.dstFactor = (WGPUBlendFactor_One);
            blendState.color.operation = (WGPUBlendOperation_Subtract); // Or ReverseSubtract depending on desired outcome
            blendState.alpha.srcFactor = (WGPUBlendFactor_One);
            blendState.alpha.dstFactor = (WGPUBlendFactor_One);
            blendState.alpha.operation = (WGPUBlendOperation_Subtract); // Apply to alpha too, mimicking glBlendEquation
            break;

        case BLEND_ALPHA_PREMULTIPLY:
            // Premultiplied alpha blend: SrcColor * 1 + DstColor * (1 - SrcAlpha)
            // Alpha blend: SrcAlpha * 1 + DstAlpha * (1 - SrcAlpha)
            // Matches glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA) and glBlendEquation(GL_FUNC_ADD)
            blendState.color.srcFactor = (WGPUBlendFactor_One);
            blendState.color.dstFactor = (WGPUBlendFactor_OneMinusSrcAlpha);
            blendState.alpha.srcFactor = (WGPUBlendFactor_One);
            blendState.alpha.dstFactor = (WGPUBlendFactor_OneMinusSrcAlpha);
            // Operation is already BlendOperation_Add
            break;

        case BLEND_CUSTOM:
            // Custom blend mode is not set here, it should be configured separately before drawing.
            // Fall through to default or handle appropriately if this function should reset to a default custom state.
            // For now, assuming it shouldn't be called with BLEND_CUSTOM, matching original rg_unreachable().
            [[fallthrough]]; // Explicit fallthrough if C++17 or later, otherwise just let it fall.

        default:
            // If an unknown or BLEND_CUSTOM mode is passed, trigger unreachable.
            // This indicates a logic error or that BLEND_CUSTOM should be handled elsewhere.
            rg_unreachable();
            break;
    }
}
RGAPI void EndBlendMode(void){
    g_renderstate.currentSettings.blendState = GetDefaultSettings().blendState;
}
RGAPI void BeginMode2D(Camera2D camera){
    drawCurrentBatch();
    Matrix mat = GetCameraMatrix2D(camera);
    mat = MatrixMultiply(ScreenMatrix(g_renderstate.renderExtentX, g_renderstate.renderExtentY), mat);
    PushMatrix();
    SetMatrix(mat);
    uint32_t uniformLoc = GetUniformLocation(GetActiveShader(), RL_DEFAULT_SHADER_UNIFORM_NAME_PROJECTION_VIEW);
    SetUniformBufferData(uniformLoc, &mat, sizeof(Matrix));
}
RGAPI void EndMode2D(){
    drawCurrentBatch();
    PopMatrix();
    //g_renderstate.activeScreenMatrix = ScreenMatrix(g_renderstate.renderExtentX, g_renderstate.renderExtentY);
    SetUniformBufferData(GetUniformLocation(GetActiveShader(), RL_DEFAULT_SHADER_UNIFORM_NAME_PROJECTION_VIEW), GetMatrixPtr(), sizeof(Matrix));
}
RGAPI void BeginMode3D(Camera3D camera){
    drawCurrentBatch();
    Matrix mat = GetCameraMatrix3D(camera, float(g_renderstate.renderExtentX) / g_renderstate.renderExtentY);
    //g_renderstate.activeScreenMatrix = mat;
    PushMatrix();
    SetMatrix(mat);
    SetUniformBufferData(0, &mat, sizeof(Matrix));
}
RGAPI void EndMode3D(){
    drawCurrentBatch();
    
    //g_renderstate.activeScreenMatrix = ScreenMatrix(g_renderstate.renderExtentX, g_renderstate.renderExtentY);
    PopMatrix();
    SetUniformBufferData(0, GetMatrixPtr(), sizeof(Matrix));
}

RGAPI void rlSetLineWidth(float lineWidth){
    g_renderstate.currentSettings.lineWidth = (uint32_t)(lineWidth <= 0.0f ? 0.0f : lineWidth);
}

RGAPI int GetScreenWidth (cwoid){
    return g_renderstate.width;
}
RGAPI int GetScreenHeight(cwoid){
    return g_renderstate.height;
}


RGAPI void BeginRenderpass(cwoid){
    BeginRenderpassEx(&g_renderstate.renderpass);
}
RGAPI void EndRenderpass(cwoid){
    if(g_renderstate.activeRenderpass){
        EndRenderpassEx(g_renderstate.activeRenderpass);
    }
    else{
        rg_trap();
    }
    g_renderstate.activeRenderpass = nullptr;
}
RGAPI void ClearBackground(Color clearColor){
    bool rpActive = GetActiveRenderPass() != nullptr;
    DescribedRenderpass* backup = GetActiveRenderPass();
    if(rpActive){
        EndRenderpassEx(g_renderstate.activeRenderpass);
    }
    
    g_renderstate.clearPass.colorClear = WGPUColor{
        clearColor.r / 255.0,
        clearColor.g / 255.0,
        clearColor.b / 255.0,
        clearColor.a / 255.0
    };
    BeginRenderpassEx(&g_renderstate.clearPass);
    EndRenderpassEx(&g_renderstate.clearPass);
    if(rpActive){
        BeginRenderpassEx(backup);
    }

}
RGAPI void BeginComputepass(){
    BeginComputepassEx(&g_renderstate.computepass);
}

RGAPI void EndComputepass(){
    EndComputepassEx(&g_renderstate.computepass);
}

#ifdef __EMSCRIPTEN__
typedef void (*FrameCallback)(void);
typedef void (*FrameCallbackArg)(void*);
// Workaround for JSPI not working in emscripten_set_main_loop. Loosely based on this code:
// https://github.com/emscripten-core/emscripten/issues/22493#issuecomment-2330275282
// This code only works with JSPI is enabled.
// I believe -sEXPORTED_RUNTIME_METHODS=getWasmTableEntry is technically necessary to link this.
EM_JS(void, requestAnimationFrameLoopWithJSPI_impl, (FrameCallback callback), {
    var wrappedCallback = WebAssembly.promising(getWasmTableEntry(callback));
    async function tick() {
        // Start the frame callback. 'await' means we won't call
        // requestAnimationFrame again until it completes.
        //var keepLooping = await wrappedCallback();
        //if (keepLooping) requestAnimationFrame(tick);
        await wrappedCallback();
        requestAnimationFrame(tick);
    }
    requestAnimationFrame(tick);
});
EM_JS(void, requestAnimationFrameLoopWithJSPIArg_impl, (FrameCallbackArg callback, void* userData), {
    var wrappedCallback = WebAssembly.promising(getWasmTableEntry(callback));
    async function tick() {
        // Start the frame callback. 'await' means we won't call
        // requestAnimationFrame again until it completes.
        //var keepLooping = await wrappedCallback();
        //if (keepLooping) requestAnimationFrame(tick);
        await wrappedCallback(userData);
        requestAnimationFrame(tick);
    }
    requestAnimationFrame(tick);
});
//#define emscripten_set_main_loop requestAnimationFrameLoopWithJSPI
#endif
RGAPI void requestAnimationFrameLoopWithJSPIArg(void (*callback)(void*), void* userData, int, int){
    #ifdef __EMSCRIPTEN__
    requestAnimationFrameLoopWithJSPIArg_impl(callback, userData);
    #else
    TRACELOG(LOG_WARNING, "requestAnimationFrame not supported outside of emscripten");
    #endif
}
RGAPI void requestAnimationFrameLoopWithJSPI(void (*callback)(void), int, int){
    #ifdef __EMSCRIPTEN__
    requestAnimationFrameLoopWithJSPI_impl(callback);
    #else
    TRACELOG(LOG_WARNING, "requestAnimationFrame not supported outside of emscripten");
    #endif
}
RenderTexture headless_rtex;
RGAPI void BeginDrawing(){

    while (g_renderstate.minimized){
        PollEvents();
        #ifdef __EMSCRIPTEN__
        emscripten_sleep(100);
        #else
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        #endif
    }
    
    {
        
        //if(g_renderstate.renderTargetStack[g_renderstate.renderTargetStackPosition].texture.id)
        //    UnloadTexture(g_renderstate.renderTargetStack[g_renderstate.renderTargetStackPosition].texture);
        #if SUPPORT_WGPU_BACKEND == 1
        //if(g_renderstate.createdSubwindows[g_renderstate.window].surface.renderTarget.texture.id)
        //    UnloadTexture(g_renderstate.createdSubwindows[g_renderstate.window].surface.renderTarget.texture);
        #endif

        //g_renderstate.drawmutex.lock();
        g_renderstate.renderExtentX = g_renderstate.createdSubwindows[g_renderstate.window]->surface.renderTarget.texture.width;
        g_renderstate.width = g_renderstate.createdSubwindows[g_renderstate.window]->surface.renderTarget.texture.width;
        g_renderstate.renderExtentY = g_renderstate.createdSubwindows[g_renderstate.window]->surface.renderTarget.texture.height;
        g_renderstate.height = g_renderstate.createdSubwindows[g_renderstate.window]->surface.renderTarget.texture.height;
        //std::cout << g_renderstate.createdSubwindows[g_renderstate.window].surface.frameBuffer.depth.width << std::endl;
        GetNewTexture(&g_renderstate.createdSubwindows[g_renderstate.window]->surface);
        g_renderstate.renderTargetStack.push(g_renderstate.createdSubwindows[g_renderstate.window]->surface.renderTarget);
        g_renderstate.mainWindowRenderTarget = g_renderstate.createdSubwindows[g_renderstate.window]->surface.renderTarget;
        //g_renderstate.renderTargetStack[g_renderstate.renderTargetStackPosition] = g_renderstate.mainWindowRenderTarget;
        //__builtin_dump_struct(&(g_renderstate.mainWindowRenderTarget.depth), printf);
    }
    BeginRenderpassEx(&g_renderstate.renderpass);
    //SetUniformBuffer(0, g_renderstate.defaultScreenMatrix);
    SetMatrix(ScreenMatrix(g_renderstate.renderExtentX, g_renderstate.renderExtentY));
    SetUniformBufferData(0, GetMatrixPtr(), sizeof(Matrix));
    
    if(IsKeyPressed(KEY_F2) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || true)){
        if(g_renderstate.grst->recording){
            EndGIFRecording();
        }
        else{
            StartGIFRecording();
        }
    }
}
RGAPI int GetRenderWidth  (cwoid){
    return g_renderstate.renderExtentX;
}
RGAPI int GetRenderHeight (cwoid){
    return g_renderstate.renderExtentY;
}
    
RGAPI void EndDrawing(){
    if(g_renderstate.activeRenderpass){    
        EndRenderpassEx(g_renderstate.activeRenderpass);
    }
    if(g_renderstate.windowFlags & FLAG_STDOUT_TO_FFMPEG){
        Image img = LoadImageFromTextureEx((WGPUTexture)GetActiveColorTarget(), 0);
        if (img.format != PIXELFORMAT_UNCOMPRESSED_B8G8R8A8 && img.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
            // Handle unsupported formats or convert as necessary
            fprintf(stderr, "Unsupported pixel format for FFmpeg export.\n");
            // You might want to convert the image to a supported format here
            // For simplicity, we'll skip exporting in this case
            return;
        }
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

        // Calculate the total size of the image data to write
        size_t totalSize = img.rowStrideInBytes * img.height;

        // Write the image data to stdout (FFmpeg should be reading from stdin)
        size_t fmtsize = GetPixelSizeInBytes(img.format);
        char offset[1];
        //fwrite(offset, 1, 1, stdout);
        for(size_t i = 0;i < img.height;i++){
            unsigned char* dptr = static_cast<unsigned char*>(img.data) + i * img.rowStrideInBytes;
            //for(uint32_t r = 0;r < img.width;r++){
            //    RGBA8Color c = reinterpret_cast<RGBA8Color*>(dptr)[r];
            //    std::cerr << (int)c.a << "\n";
            //    reinterpret_cast<RGBA8Color*>(dptr)[r] = RGBA8Color{c.a, c.b, c.g, c.r};
            //}
            size_t bytesWritten = fwrite(dptr, 1, img.width * fmtsize, stdout);
        }

        // Flush stdout to ensure all data is sent to FFmpeg promptly
        fflush(stdout);
        UnloadImage(img);
    }
    if(g_renderstate.grst->recording){
        uint64_t stmp = NanoTime();
        if(stmp - g_renderstate.grst->lastFrameTimestamp > g_renderstate.grst->delayInCentiseconds * 10000000ull){
            g_renderstate.renderTargetStack.peek().texture.format = g_renderstate.frameBufferFormat;
            Texture fbCopy = LoadTextureEx(g_renderstate.renderTargetStack.peek().texture.width, g_renderstate.renderTargetStack.peek().texture.height, g_renderstate.frameBufferFormat, false);
            BeginComputepass();
            ComputepassEndOnlyComputing();
            CopyTextureToTexture(g_renderstate.renderTargetStack.peek().texture, fbCopy);
            EndComputepass();
            BeginRenderpass();
            int recordingTextX = GetScreenWidth() - MeasureText("Recording", 30);
            DrawText("Recording", recordingTextX, 5, 30, Color{255,40,40,255});
            EndRenderpass();
            addScreenshot(g_renderstate.grst, (WGPUTexture)fbCopy.id);
            UnloadTexture(fbCopy);
            g_renderstate.grst->lastFrameTimestamp = stmp;
        }
        else{
            BeginRenderpass();
            int recordingTextX = GetScreenWidth() - MeasureText("Recording", 30);
            DrawText("Recording", recordingTextX, 5, 30, Color{255,40,40,255});
            EndRenderpass();
        }
        
        
    }
    
    //WGPUSurfaceTexture surfaceTexture;
    //wgpuSurfaceGetCurrentTexture(g_renderstate.surface, &surfaceTexture);
    
    
    if(!(g_renderstate.windowFlags & FLAG_HEADLESS)){
        #ifndef __EMSCRIPTEN__
        PresentSurface(&g_renderstate.mainWindow->surface);
        #endif
    }
    else{
        DummySubmitOnQueue();
    }
    
    std::copy(g_renderstate.smallBufferRecyclingBin.begin(), g_renderstate.smallBufferRecyclingBin.end(), std::back_inserter(g_renderstate.smallBufferPool));
    g_renderstate.smallBufferRecyclingBin.clear();
    auto& ipstate = g_renderstate.input_map[g_renderstate.window];
    std::copy(ipstate.keydown.begin(), ipstate.keydown.end(), ipstate.keydownPrevious.begin());
    ipstate.mousePosPrevious = ipstate.mousePos;
    ipstate.scrollPreviousFrame = ipstate.scrollThisFrame;
    ipstate.scrollThisFrame = Vector2{0, 0};
    std::copy(ipstate.mouseButtonDown.begin(), ipstate.mouseButtonDown.end(), ipstate.mouseButtonDownPrevious.begin());
    for(auto& [_, ipstate] : g_renderstate.input_map){
        ipstate.charQueue.clear();
        ipstate.gestureAngleThisFrame = 0;
        ipstate.gestureZoomThisFrame = 1;
    }
    PollEvents();
    if(g_renderstate.wantsToggleFullscreen){
        g_renderstate.wantsToggleFullscreen = false;
        ToggleFullscreenImpl();
    }
    //if(!(g_renderstate.windowFlags & FLAG_HEADLESS))
    //    g_renderstate.drawmutex.unlock();
    uint64_t nanosecondsPerFrame = GetTargetFPS() > 0 ? std::floor(1e9 / GetTargetFPS()) : 0;
    uint64_t beginframe_stmp = g_renderstate.last_timestamps[(g_renderstate.total_frames - 1) % 64];
    ++g_renderstate.total_frames;
    g_renderstate.last_timestamps[g_renderstate.total_frames % 64] = NanoTime();
    uint64_t elapsed = NanoTime() - beginframe_stmp;
    if(elapsed & (1ull << 63))return;
    //std::cout << elapsed << "\n";
    std::this_thread::sleep_for(std::chrono::nanoseconds(nanosecondsPerFrame - elapsed));
    //if(!(g_renderstate.windowFlags & FLAG_VSYNC_HINT) && nanosecondsPerFrame > elapsed && GetTargetFPS() > 0)
    //    NanoWait(nanosecondsPerFrame - elapsed);
    
    g_renderstate.renderTargetStack.pop();
    //std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}
void StartGIFRecording(){
    startRecording(g_renderstate.grst, 4);
}
void EndGIFRecording(){
    #ifndef __EMSCRIPTEN__
    if(!g_renderstate.grst->recording)return;
    char buf[32] = {0};
    for(int i = 1;i < 1000;i++){
        snprintf(buf, sizeof(buf), "recording%03d.gif", i);
        cfs_path path;
        cfs_path_init(&path);
        cfs_path_set(&path, buf);
        if(!cfs_path_exists(&path)){
            break;
        }
    }
    #else
    constexpr char buf[] = "gifexport.gif";
    #endif
    endRecording(g_renderstate.grst, buf);
}
void rlBegin(PrimitiveType mode){
    
    if(current_drawmode != mode){
        drawCurrentBatch();
        //assert(g_renderstate.activeRenderPass == &g_renderstate.renderpass);
        //EndRenderpassEx(&g_renderstate.renderpass);
        //BeginRenderpassEx(&g_renderstate.renderpass);
    }
    if(mode == RL_LINES){ //TODO: Fix this, why is this required? Check core_msaa and comment this out to trigger a bug
        SetTexture(1, g_renderstate.whitePixel);
    }
    current_drawmode = mode;
}
RGAPI void rlEnd(){
    
}
RGAPI uint64_t RoundUpToNextMultipleOf256(uint64_t x) {
    return (x + 255) & ~0xFF;
}
RGAPI uint64_t RoundUpToNextMultipleOf16(uint64_t x) {
    return (x + 15) & ~0xF;
}
#ifdef __EMSCRIPTEN__

#endif 


// --- Channel conversion helpers --------------------------------------------

template <typename T>
constexpr T clamp01(T v) {
    if constexpr (std::is_floating_point_v<T>) {
        return v < T(0) ? T(0) : (v > T(1) ? T(1) : v);
    } else {
        return v; // only used for float inputs
    }
}

template <typename FromC, typename ToC>
static inline ToC convert_channel(FromC x) {
    if constexpr (std::is_floating_point_v<ToC>) {
        // -> float
        if constexpr (std::is_floating_point_v<FromC>) {
            return static_cast<ToC>(x);
        } else {
            using UFrom = std::make_unsigned_t<FromC>;
            constexpr double maxFrom = static_cast<double>(std::numeric_limits<UFrom>::max());
            return static_cast<ToC>(static_cast<double>(static_cast<UFrom>(x)) / maxFrom);
        }
    } else {
        // -> integer
        using UTo = std::make_unsigned_t<ToC>;
        constexpr double maxTo = static_cast<double>(std::numeric_limits<UTo>::max());

        if constexpr (std::is_floating_point_v<FromC>) {
            double y = std::round(clamp01(static_cast<double>(x)) * maxTo);
            if (y < 0.0) y = 0.0;
            if (y > maxTo) y = maxTo;
            return static_cast<ToC>(static_cast<UTo>(y));
        } else {
            using UFrom = std::make_unsigned_t<FromC>;
            constexpr double maxFrom = static_cast<double>(std::numeric_limits<UFrom>::max());
            double y = std::round((static_cast<double>(static_cast<UFrom>(x)) / maxFrom) * maxTo);
            if (y < 0.0) y = 0.0;
            if (y > maxTo) y = maxTo;
            return static_cast<ToC>(static_cast<UTo>(y));
        }
    }
}


static inline uint16_t float32_to_float16(float f) {
    union { uint32_t u; float f; } v { .f = f };
    uint32_t x = v.u;

    uint32_t sign = (x >> 16) & 0x8000u;               // sign in half position
    uint32_t mant = x & 0x007FFFFFu;
    int32_t  exp  = int32_t((x >> 23) & 0xFF) - 127;   // unbiased

    if (((x >> 23) & 0xFFu) == 0xFFu) {
        // Inf/NaN
        if (mant == 0) return uint16_t(sign | 0x7C00u);     // Inf
        // Quiet NaN: set MSB of mantissa; keep some payload
        return uint16_t(sign | 0x7C00u | (mant >> 13) | 0x200u);
    }

    if (exp > 15) {
        // Overflow -> Inf
        return uint16_t(sign | 0x7C00u);
    }

    if (exp <= -15) {
        // Might be subnormal or underflow to zero
        if (exp < -24) {
            // Too small -> signed zero
            return uint16_t(sign);
        }
        // Subnormal half: implicit leading 1 for float32 mantissa
        mant |= 0x00800000u;
        // shift right with rounding to nearest even
        int shift = (-exp) - 14; // how much to shift to put into 10 bits
        uint32_t rnd = (mant >> (shift - 1)) & 1u;
        uint32_t sticky = ((mant & ((1u << (shift - 1)) - 1u)) != 0u);
        uint32_t halfMant = mant >> shift;
        // round to nearest even
        halfMant += (rnd & (sticky | (halfMant & 1u)));
        return uint16_t(sign | halfMant);
    }

    // Normal case
    uint32_t halfExp  = uint32_t(exp + 15);
    // Round to nearest even when dropping 13 bits
    uint32_t halfMant = mant + 0x00001000u; // add round bit (1<<12)
    if (halfMant & 0x00800000u) { // mantissa overflow from rounding
        halfMant = 0;
        ++halfExp;
        if (halfExp >= 31) { // overflow to Inf
            return uint16_t(sign | 0x7C00u);
        }
    }
    return uint16_t(sign | (halfExp << 10) | (halfMant >> 13));
}

static inline float float16_to_float32(uint16_t h) {
    uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp  = (h >> 10) & 0x1Fu;
    uint32_t mant =  h & 0x03FFu;

    uint32_t out;
    if (exp == 0) {
        if (mant == 0) {
            // zero
            out = sign;
        } else {
            // subnormal -> normalize
            int e = -1;
            uint32_t m = mant;
            while ((m & 0x0400u) == 0) { m <<= 1; --e; }
            m &= 0x03FFu; // drop leading 1
            uint32_t exp32  = uint32_t(127 - 15 + 1 + e);
            uint32_t mant32 = m << 13;
            out = sign | (exp32 << 23) | mant32;
        }
    } else if (exp == 31) {
        // Inf/NaN
        uint32_t mant32 = mant ? (mant << 13) | 0x400000u : 0; // make it quiet NaN
        out = sign | 0x7F800000u | mant32;
    } else {
        // normal
        uint32_t exp32  = exp + (127 - 15);
        uint32_t mant32 = mant << 13;
        out = sign | (exp32 << 23) | mant32;
    }
    union { uint32_t u; float f; } v { .u = out };
    return v.f;
}


// --- 4-channel pixel converter ---------------------------------------------

template<typename From, typename To>
inline To convert4(const From& fr) {
    To ret{};
    ret.r = convert_channel<decltype(fr.r), decltype(ret.r)>(fr.r);
    ret.g = convert_channel<decltype(fr.g), decltype(ret.g)>(fr.g);
    ret.b = convert_channel<decltype(fr.b), decltype(ret.b)>(fr.b);
    ret.a = convert_channel<decltype(fr.a), decltype(ret.a)>(fr.a);
    return ret;
}

// ---- Specializations for RGBA16FColor --------------------------------------
// float32 <-> half16 (no clamping)
template<>
inline RGBA16FColor convert4<RGBA32FColor, RGBA16FColor>(const RGBA32FColor& fr) {
    RGBA16FColor ret{};
    ret.r = float32_to_float16(fr.r);
    ret.g = float32_to_float16(fr.g);
    ret.b = float32_to_float16(fr.b);
    ret.a = float32_to_float16(fr.a);
    return ret;
}
template<>
inline RGBA32FColor convert4<RGBA16FColor, RGBA32FColor>(const RGBA16FColor& fr) {
    RGBA32FColor ret{};
    ret.r = float16_to_float32(fr.r);
    ret.g = float16_to_float32(fr.g);
    ret.b = float16_to_float32(fr.b);
    ret.a = float16_to_float32(fr.a);
    return ret;
}

// 8-bit UNORM <-> half16  (map int<->[0,1] then pack/unpack)
template<>
inline RGBA16FColor convert4<RGBA8Color, RGBA16FColor>(const RGBA8Color& fr) {
    RGBA16FColor ret{};
    ret.r = float32_to_float16(static_cast<float>(fr.r) / 255.0f);
    ret.g = float32_to_float16(static_cast<float>(fr.g) / 255.0f);
    ret.b = float32_to_float16(static_cast<float>(fr.b) / 255.0f);
    ret.a = float32_to_float16(static_cast<float>(fr.a) / 255.0f);
    return ret;
}
template<>
inline RGBA16FColor convert4<BGRA8Color, RGBA16FColor>(const BGRA8Color& fr) {
    RGBA16FColor ret{};
    // NOTE: field names in BGRA8Color already expose r,g,b,a logically.
    ret.r = float32_to_float16(static_cast<float>(fr.r) / 255.0f);
    ret.g = float32_to_float16(static_cast<float>(fr.g) / 255.0f);
    ret.b = float32_to_float16(static_cast<float>(fr.b) / 255.0f);
    ret.a = float32_to_float16(static_cast<float>(fr.a) / 255.0f);
    return ret;
}
template<>
inline RGBA8Color convert4<RGBA16FColor, RGBA8Color>(const RGBA16FColor& fr) {
    RGBA8Color ret{};
    auto q = [](uint16_t h)->uint8_t {
        float f = float16_to_float32(h);
        float c = std::round(std::fmax(0.0f, std::fmin(1.0f, f)) * 255.0f);
        return static_cast<uint8_t>(c);
    };
    ret.r = q(fr.r); ret.g = q(fr.g); ret.b = q(fr.b); ret.a = q(fr.a);
    return ret;
}
template<>
inline BGRA8Color convert4<RGBA16FColor, BGRA8Color>(const RGBA16FColor& fr) {
    BGRA8Color ret{};
    auto q = [](uint16_t h)->uint8_t {
        float f = float16_to_float32(h);
        float c = std::round(std::fmax(0.0f, std::fmin(1.0f, f)) * 255.0f);
        return static_cast<uint8_t>(c);
    };
    ret.r = q(fr.r); ret.g = q(fr.g); ret.b = q(fr.b); ret.a = q(fr.a);
    return ret;
}

// --- Range & image converters -----------------------------------------------

template<typename From, typename To>
void FormatRange(const From* source, To* dest, size_t count){
    const From* fr = reinterpret_cast<const From*>(source);
    To* top = reinterpret_cast<To*>(dest);

    for(size_t i = 0; i < count; ++i){
        top[i] = convert4<From, To>(fr[i]);
    }
}

template<typename From, typename To>
void FormatImage_Impl(const Image& source, Image& dest){
    for(uint32_t i = 0; i < source.height; ++i){
        const From* dataptr = reinterpret_cast<const From*>(
            static_cast<const uint8_t*>(source.data) + source.rowStrideInBytes * i
        );
        To* destptr = reinterpret_cast<To*>(
            static_cast<uint8_t*>(dest.data) + dest.rowStrideInBytes * i
        );
        FormatRange<From, To>(dataptr, destptr, source.width);
    }
}

static inline void CopyImageRows(const Image& src, Image& dst) {
    const uint8_t* s = static_cast<const uint8_t*>(src.data);
    uint8_t* d = static_cast<uint8_t*>(dst.data);
    const uint64_t rowBytes = std::min<uint64_t>(src.rowStrideInBytes, dst.rowStrideInBytes);
    for (uint32_t i = 0; i < src.height; ++i) {
        std::memcpy(d + dst.rowStrideInBytes * i, s + src.rowStrideInBytes * i, rowBytes);
    }
}


// --- Format matrix -----------------------------------------------------------
// Assumes the following pixel structs exist (field names matter, layout maps
// memory order):
//   struct RGBA8Color   { uint8_t  r,g,b,a; };
//   struct BGRA8Color   { uint8_t  b,g,r,a; };
//   struct RGBA32FColor { float    r,g,b,a; };

RGAPI void ImageFormat(Image* img, PixelFormat newFormat){
    if (!img) return;
    if (img->format == newFormat) return;

    const uint32_t psize = GetPixelSizeInBytes(newFormat);

    Image newimg{};
    newimg.format = newFormat;
    newimg.width = img->width;
    newimg.height = img->height;
    newimg.mipmaps = img->mipmaps;
    newimg.rowStrideInBytes = static_cast<uint64_t>(newimg.width) * psize;
    newimg.data = RL_CALLOC(static_cast<uint64_t>(img->width) * img->height, psize);
    if (!newimg.data) return;

    auto do_convert = [&](auto fromTag, auto toTag) {
        using FromT = decltype(fromTag);
        using ToT   = decltype(toTag);
        FormatImage_Impl<FromT, ToT>(*img, newimg);
    };

    bool converted = true;
    
    switch (img->format) {
        // ----------------- RGBA8 -> *
        case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8: {
            switch (newFormat) {
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
                    CopyImageRows(*img, newimg); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_B8G8R8A8:
                    do_convert(RGBA8Color{},  BGRA8Color{}); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
                    do_convert(RGBA8Color{},  RGBA32FColor{}); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
                    do_convert(RGBA8Color{},  RGBA16FColor{}); break;
                default: converted = false; break;
            }
        } break;

        // ----------------- BGRA8 -> *
        case PixelFormat::PIXELFORMAT_UNCOMPRESSED_B8G8R8A8: {
            switch (newFormat) {
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
                    do_convert(BGRA8Color{},  RGBA8Color{}); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_B8G8R8A8:
                    CopyImageRows(*img, newimg); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
                    do_convert(BGRA8Color{},  RGBA32FColor{}); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
                    do_convert(BGRA8Color{},  RGBA16FColor{}); break;
                default: converted = false; break;
            }
        } break;

        // ----------------- RGBA32F -> *
        case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R32G32B32A32: {
            switch (newFormat) {
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
                    do_convert(RGBA32FColor{}, RGBA8Color{}); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_B8G8R8A8:
                    do_convert(RGBA32FColor{}, BGRA8Color{}); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
                    CopyImageRows(*img, newimg); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
                    do_convert(RGBA32FColor{}, RGBA16FColor{}); break;
                default: converted = false; break;
            }
        } break;

        // ----------------- RGBA16 -> *
        case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R16G16B16A16: {
            switch (newFormat) {
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
                    do_convert(RGBA16FColor{}, RGBA8Color{}); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_B8G8R8A8:
                    do_convert(RGBA16FColor{}, BGRA8Color{}); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
                    do_convert(RGBA16FColor{}, RGBA32FColor{}); break;
                case PixelFormat::PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
                    CopyImageRows(*img, newimg); break;
                default: converted = false; break;
            }
        } break;

        default:
            converted = false; break;
    }

    if (!converted) {
        RL_FREE(newimg.data);
        return;
    }

    RL_FREE(img->data);
    img->data = newimg.data;
    img->rowStrideInBytes = newimg.rowStrideInBytes;
    img->format = newFormat;
}



RGAPI Color* LoadImageColors(Image img){
    Image copy = ImageFromImage(img, Rectangle{0,0,(float)img.width, (float)img.height});
    ImageFormat(&copy, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    return (RGBA8Color*)copy.data;
}
RGAPI void UnloadImageColors(Color* cols){
    free(cols);
}

RGAPI Image LoadImageFromTexture(Texture tex){
    //#ifndef __EMSCRIPTEN__
    //auto& device = g_renderstate.device;
    return LoadImageFromTextureEx((WGPUTexture)tex.id, 0);
    //#else
    //std::cerr << "LoadImageFromTexture not supported on web\n";
    //return Image{};
    //#endif
}
RGAPI void TakeScreenshot(const char* filename){
    Image img = LoadImageFromTextureEx(g_renderstate.mainWindowRenderTarget.texture.id, 0);
    SaveImage(img, filename);
    UnloadImage(img);
}


RGAPI bool IsKeyDown(int key){
    void* ah = GetActiveWindowHandle();
    return g_renderstate.input_map[GetActiveWindowHandle()].keydown[key];
}
RGAPI bool IsKeyPressed(int key){
    return g_renderstate.input_map[GetActiveWindowHandle()].keydown[key] && !g_renderstate.input_map[GetActiveWindowHandle()].keydownPrevious[key];
}
RGAPI int GetCharPressed(){
    int fc = 0;
    if(!g_renderstate.input_map     [GetActiveWindowHandle()].charQueue.empty()){
        fc = g_renderstate.input_map[GetActiveWindowHandle()].charQueue.front();
        g_renderstate.input_map     [GetActiveWindowHandle()].charQueue.pop_front();
    }
    return fc;
}
RGAPI int GetMouseX(cwoid){
    return (int)GetMousePosition().x;
}
RGAPI int GetMouseY(cwoid){
    return (int)GetMousePosition().y;
}

float GetGesturePinchZoom(cwoid){
    return g_renderstate.input_map[g_renderstate.window].gestureZoomThisFrame;
}
float GetGesturePinchAngle(cwoid){
     
    return g_renderstate.input_map[g_renderstate.window].gestureAngleThisFrame;
}
Vector2 GetMousePosition(cwoid){
    return g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].mousePos;
}
Vector2 GetMouseDelta(cwoid){
    return Vector2{g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].mousePos.x - g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].mousePos.x,
                   g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].mousePos.y - g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].mousePos.y};
}
float GetMouseWheelMove(void){
    return g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].scrollPreviousFrame.y;
}
Vector2 GetMouseWheelMoveV(void){
    return g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].scrollPreviousFrame;
    //return Vector2{
    //    (float)(g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].globalScrollX - g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].globalScrollYPrevious),
    //    (float)(g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].globalScrollY - g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].globalScrollYPrevious)};
}
bool IsMouseButtonPressed(int button){
    return g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].mouseButtonDown[button] && !g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].mouseButtonDownPrevious[button];
}
bool IsMouseButtonDown(int button){
    return g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].mouseButtonDown[button];
}
bool IsMouseButtonReleased(int button){
    return !g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].mouseButtonDown[button] && g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].mouseButtonDownPrevious[button];
}
bool IsCursorOnScreen(cwoid){
    return g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()].cursorInWindow;
}

uint64_t NanoTime(cwoid){
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}
double GetTime(cwoid){
    uint64_t nano_diff = NanoTime() - g_renderstate.init_timestamp;

    return double(nano_diff) * 1e-9;
}
uint32_t GetFPS(cwoid){
    auto firstzero = std::find(std::begin(g_renderstate.last_timestamps), std::end(g_renderstate.last_timestamps), int64_t(0));
    auto [minit, maxit] = std::minmax_element(std::begin(g_renderstate.last_timestamps), firstzero);
    //TRACELOG(LOG_INFO, "%d : %d", int(minit - std::begin(g_renderstate.last_timestamps)), int(maxit - std::begin(g_renderstate.last_timestamps)));
    double dblv = (firstzero - std::begin(g_renderstate.last_timestamps) - 1.0) * 1.0e9 / (*maxit - *minit);
    if(std::isnan(dblv) || std::isinf(dblv) || std::abs(dblv) > 1e9){
        return 0;
    }
    return uint32_t(std::round(dblv));
}
void DrawFPS(int posX, int posY){
    char fpstext[128] = {0};
    std::snprintf(fpstext, 128, "%d FPS", GetFPS());
    double ratio = double(GetFPS()) / GetTargetFPS();
    ratio = std::max(0.0, std::min(1.0, ratio));
    uint8_t v8 = ratio * 200;
    DrawText(fpstext, posX, posY, 40, Color{uint8_t(255 - uint8_t(ratio * ratio * 255)), v8, 20, 255});
}


Shader LoadShader(const char *vsFileName, const char *fsFileName){
    Shader shader = { 0 };

    char *vShaderStr = NULL;
    char *fShaderStr = NULL;

    if (vsFileName != NULL) vShaderStr = LoadFileText(vsFileName);
    if (fsFileName != NULL) fShaderStr = LoadFileText(fsFileName);

    if ((vShaderStr == NULL) && (fShaderStr == NULL)) TraceLog(LOG_WARNING, "SHADER: Shader files provided are not valid, using default shader");

    shader = LoadShaderFromMemory(vShaderStr, fShaderStr);

    UnloadFileText(vShaderStr);
    UnloadFileText(fShaderStr);

    return shader;
}

Shader LoadShaderSingleSource(const char* shaderSource){
    ShaderSources sources zeroinit;
    #if defined(SUPPORT_WGSL_PARSER) && SUPPORT_WGSL_PARSER == 1 
    sources.language = sourceTypeWGSL;
    auto& src = sources.sources[sources.sourceCount++];
    src.data = shaderSource;
    src.sizeInBytes = std::strlen(shaderSource);
    StringToUniformMap* bindings = getBindings(sources);
    InOutAttributeInfo attribs = getAttributes(sources);
    
    AttributeAndResidence allAttribsInOneBuffer[MAX_VERTEX_ATTRIBUTES];
    const uint32_t attributeCount = attribs.vertexAttributeCount;
    uint32_t offset = 0;
    for(uint32_t attribIndex = 0;attribIndex < attribs.vertexAttributeCount;attribIndex++){
        const WGPUVertexFormat format = attribs.vertexAttributes[attribIndex].format;
        const uint32_t location = attribs.vertexAttributes[attribIndex].location;
        allAttribsInOneBuffer[attribIndex] = CLITERAL(AttributeAndResidence){
            .attr = WGPUVertexAttribute{
                .nextInChain = nullptr,
                .format = format,
                .offset = offset,
                .shaderLocation = location
            },
            .bufferSlot = 0,
            .stepMode = WGPUVertexStepMode_Vertex,
            .enabled = true
        };
        offset += attributeSize(format);
    }
    ResourceTypeDescriptor* values = (ResourceTypeDescriptor*)RL_CALLOC(bindings->current_size, sizeof(ResourceTypeDescriptor));
    uint32_t insertIndex = 0;
    for(uint32_t i = 0;i < bindings->current_capacity;i++){
        if(bindings->table[i].key.length != 0){
            values[insertIndex++] = bindings->table[i].value;
        }
    }
    
    std::sort(values, values + bindings->current_size, [](const ResourceTypeDescriptor& x, const ResourceTypeDescriptor& y){
        return x.location < y.location;
    });

    Shader ret = LoadPipelineEx(shaderSource, allAttribsInOneBuffer, attributeCount, values, insertIndex, GetDefaultSettings());
    RL_FREE(values);
    StringToUniformMap_free(bindings);
    RL_FREE(bindings);
    return ret;
    #else
    return CLITERAL(Shader){0};
    #endif
}

extern "C" FullSurface CreateHeadlessSurface(int width, int height, PixelFormat format){
    FullSurface ret zeroinit;
    ret.headless = 1;
    ret.surfaceConfig.device = GetDevice();
    ret.surfaceConfig.width = width;
    ret.surfaceConfig.width = height;
    #if SUPPORT_VULKAN_BACKEND == 1
    ret.surfaceConfig.format = toWGPUPixelFormat(format);
    #else
    ret.surfaceConfig.format = toWGPUPixelFormat(format);
    #endif
    ret.renderTarget = LoadRenderTextureEx(width, height, format, 1, 1);
    return ret;
}

RGAPI Shader rlLoadShaderCode(const char* vertexCode, const char* fragmentCode);

#if SUPPORT_GLSL_PARSER == 0 || !defined(SUPPORT_GLSL_PARSER)
Shader rlLoadShaderCode(const char* vertexCode, const char* fragmentCode){
    return Shader{};
}
#endif

uint32_t rlGetShaderIdDefault(){
    return DefaultShader().id;
}
RGAPI uint32_t GetUniformLocation(Shader shader, const char* uniformName){
    ShaderImpl* impl = GetShaderImpl(shader);
    BindingIdentifier identifier = {
        .length = (uint32_t)strlen(uniformName)
    };
    rassert(identifier.length <= MAX_BINDING_NAME_LENGTH, "Identifier too short");
    memcpy(identifier.name, uniformName, identifier.length < MAX_BINDING_NAME_LENGTH ? identifier.length : MAX_BINDING_NAME_LENGTH);
    const ResourceTypeDescriptor* desc = StringToUniformMap_get(impl->shaderModule.reflectionInfo.uniforms, identifier);
    return desc ? desc->location : LOCATION_NOT_FOUND;
}
RGAPI uint32_t GetAttributeLocation(Shader shader, const char* attributeName){
    //Returns LOCATION_NOT_FOUND if not found
    return getReflectionAttributeLocation(&GetShaderImpl(shader)->shaderModule.reflectionInfo.attributes, attributeName);
}
//RGAPI uint32_t GetUniformLocationCompute(const DescribedComputePipeline* pl, const char* uniformName){
//    //Returns LOCATION_NOT_FOUND if not found
//    return pl->shaderModule.reflectionInfo.uniforms->GetLocation(uniformName);
//}
extern "C" uint32_t rlGetLocationUniform(const uint32_t shaderID, const char* uniformName){
    ShaderImpl* impl = GetShaderImplByID(shaderID);
    BindingIdentifier identifier = {
        .length = (uint32_t)strlen(uniformName)
    };
    rassert(identifier.length <= MAX_BINDING_NAME_LENGTH, "Identifier too short");
    memcpy(identifier.name, uniformName, identifier.length < MAX_BINDING_NAME_LENGTH ? identifier.length : MAX_BINDING_NAME_LENGTH);
    const ResourceTypeDescriptor* desc = StringToUniformMap_get(impl->shaderModule.reflectionInfo.uniforms, identifier);
    return desc ? desc->location : LOCATION_NOT_FOUND;
}
extern "C" uint32_t rlGetLocationAttrib(const uint32_t shaderID, const char* attributeName){
    ShaderImpl* impl = GetShaderImplByID(shaderID);
    return getReflectionAttributeLocation(&impl->shaderModule.reflectionInfo.attributes, attributeName);
    //return GetAttributeLocation(reinterpret_cast<const DescribedPipeline*>(renderorcomputepipeline), attributeName);
}
// Load shader from code strings and bind default locations
Shader LoadShaderFromMemory(const char *vsCode, const char *fsCode){
    Shader shader zeroinit;
    #if SUPPORT_GLSL_PARSER == 1
    //shader.id = LoadPipelineGLSL(vsCode, fsCode);
    
    shader = rlLoadShaderCode(vsCode, fsCode);

    //if (shader.id == rlGetShaderIdDefault()) shader.locs = rlGetShaderLocsDefault();
    
    if (shader.id != 0){
        // After custom shader loading, we TRY to set default location names
        // Default shader attribute locations have been binded before linking:
        //          vertex position location    = 0
        //          vertex texcoord location    = 1
        //          vertex normal location      = 2
        //          vertex color location       = 3
        //          vertex tangent location     = 4
        //          vertex texcoord2 location   = 5
        //          vertex boneIds location     = 6
        //          vertex boneWeights location = 7

        // NOTE: If any location is not found, loc point becomes -1

        shader.locs = (int *)RL_CALLOC(RL_MAX_SHADER_LOCATIONS, sizeof(int));

        // All locations reset to -1 (no location)
        for (int i = 0; i < RL_MAX_SHADER_LOCATIONS; i++) shader.locs[i] = -1;

        // Get handles to GLSL input attribute locations
        shader.locs[SHADER_LOC_VERTEX_POSITION]   = rlGetLocationAttrib(shader.id, RL_DEFAULT_SHADER_ATTRIB_NAME_POSITION);
        shader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = rlGetLocationAttrib(shader.id, RL_DEFAULT_SHADER_ATTRIB_NAME_TEXCOORD);
        shader.locs[SHADER_LOC_VERTEX_TEXCOORD02] = rlGetLocationAttrib(shader.id, RL_DEFAULT_SHADER_ATTRIB_NAME_TEXCOORD2);
        shader.locs[SHADER_LOC_VERTEX_NORMAL] = rlGetLocationAttrib(shader.id, RL_DEFAULT_SHADER_ATTRIB_NAME_NORMAL);
        shader.locs[SHADER_LOC_VERTEX_TANGENT] = rlGetLocationAttrib(shader.id, RL_DEFAULT_SHADER_ATTRIB_NAME_TANGENT);
        shader.locs[SHADER_LOC_VERTEX_COLOR] = rlGetLocationAttrib(shader.id, RL_DEFAULT_SHADER_ATTRIB_NAME_COLOR);
        shader.locs[SHADER_LOC_VERTEX_BONEIDS] = rlGetLocationAttrib(shader.id, RL_DEFAULT_SHADER_ATTRIB_NAME_BONEIDS);
        shader.locs[SHADER_LOC_VERTEX_BONEWEIGHTS] = rlGetLocationAttrib(shader.id, RL_DEFAULT_SHADER_ATTRIB_NAME_BONEWEIGHTS);
        //shader.locs[SHADER_LOC_VERTEX_INSTANCE_TX] = rlGetLocationAttrib(shader.id, RL_DEFAULT_SHADER_ATTRIB_NAME_INSTANCE_TX);

        // Get handles to GLSL uniform locations (vertex shader)
        shader.locs[SHADER_LOC_MATRIX_MVP]        = static_cast<int>(rlGetLocationUniform(shader.id, RL_DEFAULT_SHADER_UNIFORM_NAME_MVP));
        shader.locs[SHADER_LOC_MATRIX_VIEW]       = static_cast<int>(rlGetLocationUniform(shader.id, RL_DEFAULT_SHADER_UNIFORM_NAME_VIEW));
        shader.locs[SHADER_LOC_MATRIX_PROJECTION] = static_cast<int>(rlGetLocationUniform(shader.id, RL_DEFAULT_SHADER_UNIFORM_NAME_PROJECTION));
        shader.locs[SHADER_LOC_MATRIX_MODEL]      = static_cast<int>(rlGetLocationUniform(shader.id, RL_DEFAULT_SHADER_UNIFORM_NAME_MODEL));
        shader.locs[SHADER_LOC_MATRIX_NORMAL]     = static_cast<int>(rlGetLocationUniform(shader.id, RL_DEFAULT_SHADER_UNIFORM_NAME_NORMAL));
        shader.locs[SHADER_LOC_BONE_MATRICES]     = static_cast<int>(rlGetLocationUniform(shader.id, RL_DEFAULT_SHADER_UNIFORM_NAME_BONE_MATRICES));

        // Get handles to GLSL uniform locations (fragment shader)
        shader.locs[SHADER_LOC_COLOR_DIFFUSE] = rlGetLocationUniform(shader.id, RL_DEFAULT_SHADER_UNIFORM_NAME_COLOR);
        shader.locs[SHADER_LOC_MAP_DIFFUSE] = rlGetLocationUniform(shader.id, RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE0);  // SHADER_LOC_MAP_ALBEDO
        shader.locs[SHADER_LOC_MAP_SPECULAR] = rlGetLocationUniform(shader.id, RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE1); // SHADER_LOC_MAP_METALNESS
        shader.locs[SHADER_LOC_MAP_NORMAL] = rlGetLocationUniform(shader.id, RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE2);
    }
    #else // If glsl isn't supported, 
    TRACELOG(LOG_ERROR, "GLSL parsing not supported");
    #endif
    
    return shader;
}
extern "C" Texture3D LoadTexture3DEx(uint32_t width, uint32_t height, uint32_t depth, PixelFormat format){
    return LoadTexture3DPro(width, height, depth, format, WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding, 1);
}


WGPUBindGroup UpdateAndGetNativeBindGroup(DescribedBindGroup* bg){
    if(bg->needsUpdate){
        UpdateBindGroup(bg);
        //bg->bindGroup = wgpuDeviceCreateBindGroup((WGPUDevice)GetDevice(), &(bg->desc));
        bg->needsUpdate = false;
    }
    return bg->bindGroup;
}

extern "C" const char* copyString(const char* str){
    size_t len = std::strlen(str) + 1;
    char* ret = (char*)std::calloc(len, 1);
    std::memcpy(ret, str, len);
    return ret;
}

extern "C" void PrepareShader(Shader shader, VertexArray* va){
    ShaderImpl* impl = GetShaderImpl(shader);
    impl->state.vertexAttributes = va->attributes;
    impl->state.vertexAttributeCount = va->attributes_count;
    //pipeline->activePipeline = PipelineHashMap_getOrCreate(&pipeline->pipelineCache, pipeline->state, pipeline->shaderModule, pipeline->bglayout, pipeline->layout);
}

constexpr char mipmapComputerSource[] = R"(
@group(0) @binding(0) var previousMipLevel: texture_2d<f32>;
@group(0) @binding(1) var nextMipLevel: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(8, 8)
fn compute_main(@builtin(global_invocation_id) id: vec3<u32>) {
    let offset = vec2<u32>(0, 1);
    
    let color = (
        textureLoad(previousMipLevel, 2 * id.xy + offset.xx, 0) +
        textureLoad(previousMipLevel, 2 * id.xy + offset.xy, 0) +
        textureLoad(previousMipLevel, 2 * id.xy + offset.yx, 0) +
        textureLoad(previousMipLevel, 2 * id.xy + offset.yy, 0)
    ) * 0.25;
    textureStore(nextMipLevel, id.xy, color);
}
)";

DescribedBindGroupLayout LoadBindGroupLayoutMod(const DescribedShaderModule* shaderModule){
    ResourceTypeDescriptor* flat = (ResourceTypeDescriptor*)RL_CALLOC(shaderModule->reflectionInfo.uniforms->current_size, sizeof(ResourceTypeDescriptor));
    
    uint32_t insertIndex = 0;
    for(size_t i = 0;i <  shaderModule->reflectionInfo.uniforms->current_capacity;i++){
        if(shaderModule->reflectionInfo.uniforms->table[i].key.length > 0){
            flat[insertIndex++] = shaderModule->reflectionInfo.uniforms->table[i].value;
        }
    }

    std::sort(flat, flat + shaderModule->reflectionInfo.uniforms->current_size, [](const ResourceTypeDescriptor& a, const ResourceTypeDescriptor& b){
        return a.location < b.location;
    });
    
    DescribedBindGroupLayout ret =  LoadBindGroupLayout(flat, shaderModule->reflectionInfo.uniforms->current_size, false);

    RL_FREE(flat);
    return ret;
}

extern "C" Texture LoadTextureEx(uint32_t width, uint32_t height, PixelFormat format, bool to_be_used_as_rendertarget){
    return LoadTexturePro(width, height, format, (WGPUTextureUsage_RenderAttachment * to_be_used_as_rendertarget) | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc, 1, 1);
}

Texture LoadTexture(const char* filename){
    Image img = LoadImage(filename);
    Texture tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}
Texture LoadBlankTexture(uint32_t width, uint32_t height){
    return LoadTextureEx(width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true);
}

Texture LoadDepthTexture(uint32_t width, uint32_t height){
    return LoadTextureEx(width, height, PIXELFORMAT_DEPTH_32_FLOAT, true);
}
RenderTexture LoadRenderTextureEx(uint32_t width, uint32_t height, PixelFormat colorFormat, uint32_t sampleCount, uint32_t attachmentCount){
    RenderTexture ret{
        .texture = LoadTextureEx(width, height, colorFormat, true),
        .colorMultisample = Texture{}, 
        .depth = LoadTexturePro(width, height, PIXELFORMAT_DEPTH_32_FLOAT, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc, sampleCount, 1)
    };
    if(sampleCount > 1){
        ret.colorMultisample = LoadTexturePro(width, height, colorFormat, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc, sampleCount, 1);
    }
    if(attachmentCount > 1){
        rassert(sampleCount == 1, "Multisampled and multi-Attachment tendertextures not supported yet");
        for(uint32_t i = 0;i < attachmentCount - 1;i++){
            ret.moreColorAttachments[i] = LoadTexturePro(width, height, colorFormat, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc, sampleCount, 1);
        }
    }
    ret.colorAttachmentCount = attachmentCount;
    return ret;
}



DescribedRenderpass* GetActiveRenderPass(){
    return g_renderstate.activeRenderpass;
}
Shader GetActiveShader(){
    return g_renderstate.activeShader;
}

void SetTexture                   (uint32_t index, Texture tex){
    SetShaderTexture(GetActiveShader(), index, tex);
}
void SetSampler                   (uint32_t index, DescribedSampler sampler){
    SetShaderSampler (GetActiveShader(), index, sampler);
}
void SetUniformBuffer             (uint32_t index, DescribedBuffer* buffer){
    SetShaderUniformBuffer (GetActiveShader(), index, buffer);
}
void SetStorageBuffer             (uint32_t index, DescribedBuffer* buffer){
    SetShaderStorageBuffer(GetActiveShader(), index, buffer);
}
void SetUniformBufferData         (uint32_t index, const void* data, size_t size){
    SetShaderUniformBufferData(GetActiveShader(), index, data, size);
}
void SetStorageBufferData         (uint32_t index, const void* data, size_t size){
    SetShaderStorageBufferData(GetActiveShader(), index, data, size);
}

void SetBindgroupUniformBuffer (DescribedBindGroup* bg, uint32_t index, DescribedBuffer* buffer){
    WGPUBindGroupEntry entry{};
    entry.binding = index;
    entry.buffer = buffer->buffer;
    entry.size = buffer->size;
    UpdateBindGroupEntry(bg, index, entry);
}

void SetBindgroupStorageBuffer (DescribedBindGroup* bg, uint32_t index, DescribedBuffer* buffer){
    WGPUBindGroupEntry entry{};
    entry.binding = index;
    entry.buffer = buffer->buffer;
    entry.size = buffer->size;
    UpdateBindGroupEntry(bg, index, entry);
}

extern "C" void SetBindgroupTexture3D(DescribedBindGroup* bg, uint32_t index, Texture3D tex){
    WGPUBindGroupEntry entry{};
    entry.binding = index;
    entry.textureView = tex.view;
    
    UpdateBindGroupEntry(bg, index, entry);
}
extern "C" void SetBindgroupTextureView(DescribedBindGroup* bg, uint32_t index, WGPUTextureView texView){
    WGPUBindGroupEntry entry{};
    entry.binding = index;
    entry.textureView = texView;
    
    UpdateBindGroupEntry(bg, index, entry);
}
extern "C" void SetBindgroupTexture(DescribedBindGroup* bg, uint32_t index, Texture tex){
    WGPUBindGroupEntry entry{};
    entry.binding = index;
    entry.textureView = tex.view;
    
    UpdateBindGroupEntry(bg, index, entry);
}

extern "C" void SetBindgroupSampler(DescribedBindGroup* bg, uint32_t index, DescribedSampler sampler){
    WGPUBindGroupEntry entry{};
    entry.binding = index;
    entry.sampler = sampler.sampler;
    UpdateBindGroupEntry(bg, index, entry);
}



void SetShaderTexture             (Shader shader, uint32_t index, Texture tex){
    ShaderImpl* sh = allocatedShaderIDs_shc + shader.id;
    SetBindgroupTexture(&sh->bindGroup, index, tex);
}
void SetShaderSampler             (Shader shader, uint32_t index, DescribedSampler sampler){
    ShaderImpl* sh = allocatedShaderIDs_shc + shader.id;
    SetBindgroupSampler(&sh->bindGroup, index, sampler);
}
void SetShaderUniformBuffer       (Shader shader, uint32_t index, DescribedBuffer* buffer){
    ShaderImpl* sh = allocatedShaderIDs_shc + shader.id;
    SetBindgroupUniformBuffer(&sh->bindGroup, index, buffer);
}
void SetShaderStorageBuffer       (Shader shader, uint32_t index, DescribedBuffer* buffer){
    ShaderImpl* sh = allocatedShaderIDs_shc + shader.id;
    SetBindgroupStorageBuffer(&sh->bindGroup, index, buffer);
}
void SetShaderUniformBufferData   (Shader shader, uint32_t index, const void* data, size_t size){
    ShaderImpl* sh = allocatedShaderIDs_shc + shader.id;
    SetBindgroupUniformBufferData(&sh->bindGroup, index, data, size);
}
void SetShaderStorageBufferData   (Shader shader, uint32_t index, const void* data, size_t size){
    ShaderImpl* sh = allocatedShaderIDs_shc + shader.id;
    SetBindgroupStorageBufferData(&sh->bindGroup, index, data, size);
}

inline uint64_t bgEntryHash(const ResourceDescriptor& bge){
    const uint32_t rotation = (bge.binding * 7) & 63;
    uint64_t value = ROT_BYTES((uint64_t)bge.buffer, rotation);
    value ^= ROT_BYTES((uint64_t)bge.textureView, rotation);
    value ^= ROT_BYTES((uint64_t)bge.sampler, rotation);
    value ^= ROT_BYTES((uint64_t)bge.offset, rotation);
    value ^= ROT_BYTES((uint64_t)bge.size, rotation);
    return value;
}

inline uint64_t bgEntryHash(const WGPUBindGroupEntry& bge){
    const uint32_t rotation = (bge.binding * 7) & 63;
    uint64_t value = ROT_BYTES((uint64_t)bge.buffer, rotation);
    value ^= ROT_BYTES((uint64_t)bge.textureView, rotation);
    value ^= ROT_BYTES((uint64_t)bge.sampler, rotation);
    value ^= ROT_BYTES((uint64_t)bge.offset, rotation);
    value ^= ROT_BYTES((uint64_t)bge.size, rotation);
    return value;
}

extern "C" DescribedBindGroup LoadBindGroup(const DescribedBindGroupLayout* bglayout, const WGPUBindGroupEntry* entries, size_t entryCount){
    DescribedBindGroup ret zeroinit;
    if(entryCount > 0){

        ret.entries = (WGPUBindGroupEntry*)RL_CALLOC(entryCount, sizeof(ResourceDescriptor));
        std::memcpy(ret.entries, entries, entryCount * sizeof(ResourceDescriptor));
    }
    ret.entryCount = entryCount;
    ret.layout = bglayout;

    ret.needsUpdate = true;
    ret.descriptorHash = 0;


    for(uint32_t i = 0;i < ret.entryCount;i++){
        ret.descriptorHash ^= bgEntryHash(ret.entries[i]);
    }
    //ret.bindGroup = wgpuDeviceCreateBindGroup((WGPUDevice)GetDevice(), &ret.desc);
    return ret;
}

inline bool operator==(const RenderSettings& a, const RenderSettings& b){
    return a.depthTest == b.depthTest &&
           a.faceCull == b.faceCull &&
           a.depthCompare == b.depthCompare &&
           a.frontFace == b.frontFace;
}

void UnloadRenderpass(DescribedRenderpass rp){
    //if(rp.rca)free(rp.rca); //Should not happen
    //rp.rca = nullptr;

    //if(rp.dsa)free(rp.dsa);
    //rp.dsa = nullptr;
}


DescribedSampler LoadSampler(TextureWrap amode, TextureFilter fmode){
    return LoadSamplerEx(amode, fmode, fmode, 1.0f);
}


WGPUTexture GetActiveColorTarget(){
    return g_renderstate.renderTargetStack.peek().texture.id;
}

extern "C" char* LoadFileText(const char *fileName) {
    std::ifstream file(fileName, std::ios::ate);
    if (!file.is_open()) {
        return nullptr;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    char* buffer = (char*)RL_MALLOC(size + 1);
    if (!buffer) {
        return nullptr;
    }

    if (!file.read(static_cast<char*>(buffer), size)) {
        RL_FREE(buffer);
        return nullptr;
    }
    buffer[size] = '\0';
    return buffer;
}
extern "C" void UnloadFileText(char* content){
    RL_FREE((void*)content);
}
extern "C" void UnloadFileData(void* content){
    RL_FREE((void*)content);
}
extern "C" void* LoadFileData(const char *fileName, size_t *dataSize) {
    std::ifstream file(fileName, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        *dataSize = 0;
        TRACELOG(LOG_ERROR, "Failed to open file %s", fileName);
        return nullptr;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    void* buffer = RL_MALLOC(size);
    if (!buffer) {
        *dataSize = 0;
        TRACELOG(LOG_ERROR, "Failed to load file %s", fileName);
        return nullptr;
    }

    if (!file.read(static_cast<char*>(buffer), size)) {
        RL_FREE(buffer);
        *dataSize = 0;
        TRACELOG(LOG_ERROR, "Failed to load file %s", fileName);
        return nullptr;
    }

    *dataSize = static_cast<size_t>(size);
    return buffer;
}
extern "C" Image LoadImage(const char* filename){
    size_t size;
    std::string fn_cpp(filename);
    const char* extbegin = fn_cpp.data() + fn_cpp.rfind('.'); 
    void* data = LoadFileData(filename, &size);

    if(size != 0){
        Image ld =  LoadImageFromMemory(extbegin, data, size);
        RL_FREE(data);
        return ld;
    }
    else{
        
    }
    return Image{};
}

void UnloadImage(Image img){
    //TRACELOG(LOG_INFO, "UnloadImage called");
    RL_FREE(img.data);
    img.data = nullptr;
}
extern "C" Image LoadImageFromMemory(const char* extension, const void* data, size_t dataSize){
    Image image zeroinit;
    image.mipmaps = 1;
    uint32_t comp;
    image.data = stbi_load_from_memory((stbi_uc*)data, static_cast<int>(dataSize), (int*)&image.width, (int*)&image.height, (int*)&comp, 0);
    image.rowStrideInBytes = comp * image.width;
    if(comp == 4){
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }else if(comp == 3){
        image.format = RGB8;
    }
    return image;
}
extern "C" Image GenImageColor(Color a, uint32_t width, uint32_t height){
    Image ret{
        .data = RL_CALLOC(width * height, sizeof(Color)),
        .width = width, 
        .height = height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 
        .rowStrideInBytes = width * 4,
    };
    for(uint32_t i = 0;i < height;i++){
        for(uint32_t j = 0;j < width;j++){
            const size_t index = size_t(i) * width + j;
            static_cast<Color*>(ret.data)[index] = a;
        }
    }
    return ret;
}
extern "C" Image GenImageChecker(Color a, Color b, uint32_t width, uint32_t height, uint32_t checkerCount){
    Image ret{
        .data = RL_CALLOC(width * height, sizeof(Color)),
        .width = width, 
        .height = height, 
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 
        .rowStrideInBytes = width * 4, 
    };
    for(uint32_t i = 0;i < height;i++){
        for(uint32_t j = 0;j < width;j++){
            const size_t index = size_t(i) * width + j;
            const size_t ic = i * checkerCount / height;
            const size_t jc = j * checkerCount / width;
            static_cast<Color*>(ret.data)[index] = ((ic ^ jc) & 1) ? a : b;
        }
    }
    return ret;
}
void SaveImage(Image _img, const char* filepath){
    std::string_view fp(filepath, filepath + std::strlen(filepath));
    //std::cout << img.format << "\n";
    if(_img.format == GRAYSCALE){
        if(fp.ends_with(".png")){
            stbi_write_png(filepath, _img.width, _img.height, 2, _img.data, _img.width * sizeof(uint16_t));
            return;
        }
        else{
            TRACELOG(LOG_WARNING, "Grayscale can only export to png");
        }
    }
    Image img = ImageFromImage(_img, CLITERAL(Rectangle){0,0,(float)_img.width, (float)_img.height});
    ImageFormat(&img, PixelFormat::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    //size_t stride = std::ceil(img.rowStrideInBytes);
    //if(stride == 0){
    //    stride = img.width * sizeof(Color);
    //}
    if(fp.ends_with(".png")){
        stbi_write_png(filepath, (int)img.width, (int)img.height, 4, img.data, (int)img.rowStrideInBytes);
    }
    else if(fp.ends_with(".jpg")){
        stbi_write_jpg(filepath, (int)img.width, (int)img.height, 4, img.data, 100);
    }
    else if(fp.ends_with(".bmp")){
        //if(row)
        //std::cerr << "Careful with bmp!" << filepath << "\n";
        stbi_write_bmp(filepath, (int)img.width, (int)img.height, 4, img.data);
    }
    else{
        TRACELOG(LOG_ERROR, "Unrecognized image format in filename %s", filepath);
    }
    UnloadImage(img);
}
void UseTexture(Texture tex){
    Shader activeShader = GetActiveShader();
    ShaderImpl* activeShaderImpl = GetShaderImpl(activeShader);
    uint32_t texture0loc = GetUniformLocation(activeShader, RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE0);
    if(texture0loc == LOCATION_NOT_FOUND){
        texture0loc = 1;
        //return;
    }
    if(activeShaderImpl->bindGroup.entries[texture0loc].textureView == tex.view)return;
    drawCurrentBatch();
    SetTexture(texture0loc, tex);
    
}

void UseNoTexture(){
    UseTexture(g_renderstate.whitePixel);
}

UniformAccessor DescribedComputePipeline::operator[](const char* uniformName){
    
    const ResourceTypeDescriptor* it = StringToUniformMap_get(shaderModule.reflectionInfo.uniforms, BIfromCString(uniformName));
    if(it == NULL){
        TRACELOG(LOG_ERROR, "Accessing nonexistent uniform %s", uniformName);
        return UniformAccessor{
            .index = LOCATION_NOT_FOUND,
            .bindgroup = nullptr
        };
    }
    uint32_t location = it->location;
    return UniformAccessor{
        .index = location, 
        .bindgroup = &this->bindGroup
    };
}

void UniformAccessor::operator=(DescribedBuffer* buf){
    SetBindgroupStorageBuffer(bindgroup, index, buf);
}

//DescribedPipeline* Relayout(DescribedPipeline* pl, VertexArray* vao){
//    pl->state.vertexAttributes = vao->attributes;
//    pl->activePipeline = PipelineHashMap_getOrCreate(&pl->pipelineCache, pl->state, pl->shaderModule, pl->bglayout, pl->layout);
//    return pl;
//}


// TODO 
// BeginTextureAndPipelineMode and BeginTextureMode are very similar, maybe abstract away their common parts
void BeginTextureAndPipelineMode(RenderTexture rtex, Shader pl){
    if(g_renderstate.activeRenderpass){
        EndRenderpass();
    }
    g_renderstate.renderTargetStack.push(rtex);
    g_renderstate.renderExtentX = rtex.texture.width;
    g_renderstate.renderExtentY = rtex.texture.height;
    PushMatrix();
    Matrix mat = ScreenMatrix(g_renderstate.renderExtentX, g_renderstate.renderExtentY);
    SetMatrix(mat);

    g_renderstate.activeShader = pl;
    uint32_t location = GetUniformLocation(g_renderstate.activeShader, RL_DEFAULT_SHADER_UNIFORM_NAME_PROJECTION_VIEW);
    if(location != LOCATION_NOT_FOUND){
        SetUniformBufferData(location, &g_renderstate.matrixStack.peek().first, sizeof(Matrix));
    }
    BeginRenderpass();
}

void BeginTextureMode(RenderTexture rtex){
    if(g_renderstate.activeRenderpass){
        EndRenderpass();
    }
    g_renderstate.renderTargetStack.push(rtex);
    g_renderstate.renderExtentX = rtex.texture.width;
    g_renderstate.renderExtentY = rtex.texture.height;

    PushMatrix();
    Matrix mat = ScreenMatrix(g_renderstate.renderExtentX, g_renderstate.renderExtentY);
    SetMatrix(mat);
    SetUniformBufferData(0, &mat, sizeof(Matrix));
    BeginRenderpass();
}

void EndTextureAndPipelineMode(){
    drawCurrentBatch();

    EndRenderpassPro(GetActiveRenderPass(), true);

    g_renderstate.renderTargetStack.pop();
    g_renderstate.activeShader = g_renderstate.defaultShader;
    PopMatrix();
    if(!g_renderstate.renderTargetStack.empty()){
        g_renderstate.renderExtentX = g_renderstate.renderTargetStack.peek().texture.width;
        g_renderstate.renderExtentY = g_renderstate.renderTargetStack.peek().texture.height;
        Matrix mat = ScreenMatrix((int)g_renderstate.renderExtentX, (int)g_renderstate.renderExtentY);
        //SetUniformBuffer(0, g_renderstate.defaultScreenMatrix);
        SetUniformBufferData(0, GetMatrixPtr(), sizeof(Matrix));
        BeginRenderpass();
    }
}

void EndTextureMode(){
    drawCurrentBatch();

    EndRenderpassPro(GetActiveRenderPass(), true);

    g_renderstate.renderTargetStack.pop();
    PopMatrix();
    if(!g_renderstate.renderTargetStack.empty()){
        g_renderstate.renderExtentX = g_renderstate.renderTargetStack.peek().texture.width;
        g_renderstate.renderExtentY = g_renderstate.renderTargetStack.peek().texture.height;
        Matrix mat = ScreenMatrix((int)g_renderstate.renderExtentX, (int)g_renderstate.renderExtentY);
        //SetUniformBuffer(0, g_renderstate.defaultScreenMatrix);
        SetUniformBufferData(0, GetMatrixPtr(), sizeof(Matrix));
        BeginRenderpass();
    }
}
RGAPI void BeginWindowMode(SubWindow sw){
    SubWindow swref = g_renderstate.createdSubwindows.at(sw->handle);
    g_renderstate.activeSubWindow = sw;
    GetNewTexture(&swref->surface);
    BeginTextureMode(swref->surface.renderTarget);
}


RGAPI void EndWindowMode(){
    EndTextureMode();
    if(!g_renderstate.renderTargetStack.empty()){
        g_renderstate.renderExtentX = g_renderstate.renderTargetStack.peek().texture.width;
        g_renderstate.renderExtentY = g_renderstate.renderTargetStack.peek().texture.height;
        Matrix mat = ScreenMatrix((int)g_renderstate.renderExtentX, (int)g_renderstate.renderExtentY);
        //SetUniformBuffer(0, g_renderstate.defaultScreenMatrix);
        PopMatrix();
        SetUniformBufferData(0, GetMatrixPtr(), sizeof(Matrix));
        BeginRenderpass();
    }
    PresentSurface(&g_renderstate.activeSubWindow->surface);
    auto& ipstate = g_renderstate.input_map[(GLFWwindow*)GetActiveWindowHandle()];
    std::copy(ipstate.keydown.begin(), ipstate.keydown.end(), ipstate.keydownPrevious.begin());
    ipstate.mousePosPrevious = ipstate.mousePos;
    ipstate.scrollPreviousFrame = ipstate.scrollThisFrame;
    ipstate.scrollThisFrame = Vector2{0, 0};
    std::copy(ipstate.mouseButtonDown.begin(), ipstate.mouseButtonDown.end(), ipstate.mouseButtonDownPrevious.begin());
    g_renderstate.activeSubWindow = NULL;
    return;
}


int GetRandomValue(int min, int max){
    int w = (max - min);
    int v = rand() & w;
    return v + min;
}

extern "C" DescribedBuffer* GenVertexBuffer(const void* data, size_t size){
    return GenBufferEx(data, size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex);
}
DescribedBuffer* GenIndexBuffer(const void* data, size_t size){
    return GenBufferEx(data, size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index);
}
DescribedBuffer* GenUniformBuffer(const void* data, size_t size){
    return GenBufferEx(data, size, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage);
}
DescribedBuffer* GenStorageBuffer(const void* data, size_t size){
    return GenBufferEx(data, size, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform);
}



extern "C" void SetTargetFPS(int fps){
    g_renderstate.targetFPS = fps;
}
extern "C" int GetTargetFPS(){
    return g_renderstate.targetFPS;
}
extern "C" uint64_t GetFrameCount(){
    return g_renderstate.total_frames;
}
//TODO: this is bad
extern "C" float GetFrameTime(){
    if(g_renderstate.total_frames <= 1){
        return 0.0f;
    }
    if(g_renderstate.last_timestamps[g_renderstate.total_frames % 64] - g_renderstate.last_timestamps[(g_renderstate.total_frames - 1) % 64] < 0){
        return 1.0e-9f * (g_renderstate.last_timestamps[(g_renderstate.total_frames - 1) % 64] - g_renderstate.last_timestamps[(g_renderstate.total_frames - 2) % 64]);
    } 
    return 1.0e-9f * (g_renderstate.last_timestamps[g_renderstate.total_frames % 64] - g_renderstate.last_timestamps[(g_renderstate.total_frames - 1) % 64]);
}
extern "C" void SetConfigFlags(int /* enum WindowFlag */ flag){
    g_renderstate.windowFlags |= flag;
}
void NanoWaitImpl(uint64_t stmp){
    for(;;){
        uint64_t now = NanoTime();
        if(now >= stmp)break;
        if(stmp - now > 2500000){
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        else if(stmp - now > 150000){
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }else{
            for(;NanoTime() < stmp;){}
            break;
        }
    }
}

extern "C" void NanoWait(uint64_t time){
    NanoWaitImpl(NanoTime() + time);
    return;
}
RenderSettings GetDefaultSettings(){
    RenderSettings ret zeroinit;
    ret.lineWidth = 1;
    ret.faceCull = 1;
    ret.frontFace = WGPUFrontFace_CCW;
    ret.depthTest = 1;
    ret.depthCompare = WGPUCompareFunction_LessEqual;
    ret.sampleCount = (g_renderstate.windowFlags & FLAG_MSAA_4X_HINT) ? 4 : 1;

    ret.blendState.alpha.srcFactor = (WGPUBlendFactor_One);
    ret.blendState.alpha.dstFactor = (WGPUBlendFactor_OneMinusSrcAlpha);
    ret.blendState.alpha.operation = (WGPUBlendOperation_Add);
    ret.blendState.color.srcFactor = (WGPUBlendFactor_SrcAlpha);
    ret.blendState.color.dstFactor = (WGPUBlendFactor_OneMinusSrcAlpha);
    ret.blendState.color.operation = (WGPUBlendOperation_Add);
    
    return ret;
}
FILE* tracelogFile = stdout;
int tracelogLevel = LOG_INFO;
void SetTraceLogFile(FILE* file){
    tracelogFile = file;
}
void SetTraceLogLevel(int logLevel){
    tracelogLevel = logLevel;
}

Texture GetDefaultTexture(cwoid){
    return g_renderstate.whitePixel;
}

Shader DefaultShader(){
    return g_renderstate.defaultShader;
}
void TraceLog(int logType, const char *text, ...){
    // Message has level below current threshold, don't emit
    if(logType < tracelogLevel)return;

    va_list args;
    va_start(args, text);

    //if (traceLog){
    //    traceLog(logType, text, args);
    //    va_end(args);
    //    return;
    //}
    constexpr size_t MAX_TRACELOG_MSG_LENGTH = 16384;
    char buffer[MAX_TRACELOG_MSG_LENGTH] = {0};
    int needs_reset = 0;
    switch (logType)
    {
        case LOG_TRACE: strcpy(buffer, "TRACE: "); break;
        case LOG_DEBUG: strcpy(buffer, "DEBUG: "); break;
        case LOG_INFO: strcpy(buffer, TERMCTL_GREEN "INFO: "); needs_reset = 1;break;
        case LOG_WARNING: strcpy(buffer, TERMCTL_YELLOW "WARNING: ");needs_reset = 1; break;
        case LOG_ERROR: strcpy(buffer, TERMCTL_RED "ERROR: ");needs_reset = 1; break;
        case LOG_FATAL: strcpy(buffer, TERMCTL_RED "FATAL: "); break;
        default: break;
    }
    size_t offset_now = strlen(buffer);
    
    unsigned int textSize = (unsigned int)strlen(text);
    memcpy(buffer + strlen(buffer), text, (textSize < (MAX_TRACELOG_MSG_LENGTH - 12))? textSize : (MAX_TRACELOG_MSG_LENGTH - 12));
    if(needs_reset){
        strcat(buffer, TERMCTL_RESET "\n");
    }
    else{
        strcat(buffer, "\n");
    }
    vfprintf(tracelogFile, buffer, args);
    fflush  (tracelogFile);

    va_end(args);
    // If fatal logging, exit program
    if (logType == LOG_FATAL){
        fputs(TERMCTL_RED "Exiting due to fatal error!\n" TERMCTL_RESET, tracelogFile);
        rg_trap();
        exit(EXIT_FAILURE); 
    }

}

unsigned char *DecompressData(const unsigned char *compData, int compDataSize, int *dataSize)
{
    unsigned char *data = NULL;

    // Decompress data from a valid DEFLATE stream
    data = (unsigned char *)calloc((20)*1024*1024, 1);
    int length = sinflate(data, 20*1024*1024, compData, compDataSize);
    // WARNING: RL_REALLOC can make (and leave) data copies in memory, be careful with sensitive compressed data!
    // TODO: Use a different approach, create another buffer, copy data manually to it and wipe original buffer memory

    unsigned char *temp = (unsigned char *)realloc(data, length);

    if (temp != NULL) data = temp;
    else TRACELOG(LOG_WARNING, "SYSTEM: Failed to re-allocate required decompression memory");

    *dataSize = length;

    TRACELOG(LOG_INFO, "SYSTEM: Decompress data: Comp. size: %i -> Original size: %i", compDataSize, *dataSize);

    return data;
}

unsigned char *CompressData(const unsigned char *data, int dataSize, int *compDataSize)
{
    #define COMPRESSION_QUALITY_DEFLATE  8

    unsigned char *compData = NULL;

    // Compress data and generate a valid DEFLATE stream
    struct sdefl *sdefl = (struct sdefl *)calloc(1, sizeof(struct sdefl));   // WARNING: Possible stack overflow, struct sdefl is almost 1MB
    int bounds = sdefl_bound(dataSize);
    compData = (unsigned char *)calloc(bounds, 1);

    *compDataSize = sdeflate(sdefl, compData, data, dataSize, COMPRESSION_QUALITY_DEFLATE);   // Compression level 8, same as stbiw
    RL_FREE(sdefl);

    TRACELOG(LOG_INFO, "SYSTEM: Compress data: Original size: %i -> Comp. size: %i", dataSize, *compDataSize);

    return compData;
}

// String pointer reverse break: returns right-most occurrence of charset in s
static const char *strprbrk(const char *s, const char *charset)
{
    const char *latestMatch = NULL;

    for (; s = strpbrk(s, charset), s != NULL; latestMatch = s++) { }

    return latestMatch;
}
typedef bool (*cfs_search_predicate)(const cfs_path* path, void* user_data);

// Internal struct for the BFS queue, pairing a path with its search depth.
typedef struct cfs_search_item_t {
    cfs_path path;
    int depth;
} cfs_search_item;

// A simple queue for the search items, required for BFS.
typedef struct cfs_search_queue_t {
    cfs_search_item* items;
    int capacity;
    int count;
    int head;
} cfs_search_queue;


/**
 * @brief Initializes a search queue. Returns false on memory allocation failure.
 */
static inline bool cfs_int_queue_init(cfs_search_queue* q) {
    q->capacity = 16; // Start with a slightly larger capacity
    q->count = 0;
    q->head = 0;
    q->items = (cfs_search_item*)malloc(q->capacity * sizeof(cfs_search_item));
    return q->items != NULL;
}

/**
 * @brief Frees all memory used by the queue, including the paths within it.
 */
static inline void cfs_int_queue_destroy(cfs_search_queue* q) {
    if (!q || !q->items) return;
    // Free any remaining paths in the queue
    for (int i = 0; i < q->count; ++i) {
        int index = (q->head + i) % q->capacity;
        cfs_path_free_storage(&q->items[index].path);
    }
    free(q->items);
    q->items = NULL;
    q->capacity = 0;
    q->count = 0;
    q->head = 0;
}

/**
 * @brief Adds an item to the back of the queue, resizing if necessary.
 * @note This function takes ownership of the path data in 'item'.
 */
static inline bool cfs_int_queue_push(cfs_search_queue* q, cfs_search_item item) {
    if (q->count == q->capacity) {
        int new_capacity = q->capacity * 2;
        // The queue needs to be re-ordered to be contiguous before realloc.
        cfs_search_item* new_items = (cfs_search_item*)malloc(new_capacity * sizeof(cfs_search_item));
        if (!new_items) return false;

        for (int i = 0; i < q->count; ++i) {
            new_items[i] = q->items[(q->head + i) % q->capacity];
        }
        
        free(q->items);
        q->items = new_items;
        q->capacity = new_capacity;
        q->head = 0;
    }

    // Add the new item to the logical end of the queue.
    int tail_index = (q->head + q->count) % q->capacity;
    q->items[tail_index] = item;
    q->count++;
    return true;
}

/**
 * @brief Removes and returns the item from the front of the queue.
 */
static inline cfs_search_item cfs_int_queue_pop(cfs_search_queue* q) {
    cfs_search_item item = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return item;
}

/**
 * @brief Gets the filename component of a path.
 * @example cfs_path_get_filename("C:/folder/file.txt") -> "file.txt"
 * @return A pointer into the original string, not a new allocation.
 */
static inline const char* cfs_path_get_filename(const cfs_path* p) {
    if (!p || p->len == 0) return "";
    
    const char* c_str = cfs_path_c_str(p);
    // Find the last path separator
    for (size_t i = p->len - 1; i > 0; --i) {
        if (c_str[i] == CFS_PATH_SEPARATOR) {
            return &c_str[i + 1];
        }
    }
    // If no separator is found, the whole string is the filename
    return c_str;
}


// -----------------------------------------------------------------------------
// ---[ Core Search Function Implementations ]----------------------------------
// -----------------------------------------------------------------------------

/**
 * @brief C99 implementation of the breadth-first search logic.
 * @param result_path Output parameter; if a path is found, it's copied here. Must be initialized.
 * @param start_path The directory where the search begins.
 * @param max_depth How many levels of subdirectories to search. 0=only check items in start_path.
 * @param predicate The function to call for each file/directory found.
 * @param user_data Context data to be passed to the predicate function.
 * @return Returns true if the predicate found a match, false otherwise.
 */
static inline bool breadthFirstSearch_c(
    cfs_path* result_path,
    const cfs_path* start_path,
    int max_depth,
    cfs_search_predicate predicate,
    void* user_data)
{
    cfs_search_queue queue;
    if (!cfs_int_queue_init(&queue)) {
        return false;
    }

    // Push the starting item onto the queue
    cfs_search_item start_item;
    start_item.depth = 0;
    cfs_path_init(&start_item.path);
    if (cfs_path_set(&start_item.path, cfs_path_c_str(start_path))) {
        cfs_int_queue_push(&queue, start_item);
    } else {
        cfs_path_free_storage(&start_item.path);
        cfs_int_queue_destroy(&queue);
        return false; // Could not set initial path
    }

    bool found = false;
    while (queue.count > 0) {
        cfs_search_item current = cfs_int_queue_pop(&queue);

        // 1. Test the current path against the predicate
        if (predicate(&current.path, user_data)) {
            cfs_path_set(result_path, cfs_path_c_str(&current.path));
            found = true;
            cfs_path_free_storage(&current.path); // Free the popped item
            goto cleanup; // Exit search, we found it
        }

        // 2. If it's a directory and we can go deeper, add its children
        if (current.depth < max_depth && cfs_is_directory(cfs_path_c_str(&current.path))) {
            cfs_path_list children;
            if (cfs_list_directory(cfs_path_c_str(&current.path), &children)) {
                for (uint32_t i = 0; i < children.pathCount; ++i) {
                    cfs_search_item child_item;
                    child_item.depth = current.depth + 1;
                    cfs_path_init(&child_item.path);
                    // Move path from list to the search item
                    if (cfs_path_set(&child_item.path, cfs_path_c_str(&children.paths[i]))) {
                       if (!cfs_int_queue_push(&queue, child_item)) {
                           // Push failed, free the path we just created
                           cfs_path_free_storage(&child_item.path);
                       }
                    }
                }
                cfs_free_path_list(&children);
            }
        }
        
        // 3. Free the path from the item we popped and processed
        cfs_path_free_storage(&current.path);
    }

cleanup:
    cfs_int_queue_destroy(&queue); // This frees any paths remaining in the queue
    return found;
}

/**
 * @brief Predicate function used by FindDirectory to match a directory name.
 */
static inline bool find_directory_predicate(const cfs_path* path, void* user_data) {
    const char* directory_name_to_find = (const char*)user_data;
    if (cfs_is_directory(cfs_path_c_str(path))) {
        const char* filename = cfs_path_get_filename(path);
        if (strcmp(filename, directory_name_to_find) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Searches for a directory by name, starting from the current directory
 *        and searching outwards into parent directories. This is a direct C99
 *        port of the provided C++ function.
 *
 * @param directoryName The name of the directory to find (e.g., "data").
 * @param maxOutwardSearch The number of parent directories to search upwards. (0 = current dir only).
 * @return A const char* to the full path if found, otherwise NULL.
 * @warning The returned pointer is to a static internal buffer and is NOT
 *          thread-safe. Its content becomes invalid on the next call to this function.
 */
const char* FindDirectory(const char* directoryName, int maxOutwardSearch) {
    static char dirPaff[2048] = {0};

    cfs_path search_dir;
    cfs_path_init(&search_dir);
    if (!cfs_get_working_directory(&search_dir)) {
        return NULL; // Cannot even start if we don't know where we are.
    }
    
    cfs_path found_path;
    cfs_path_init(&found_path);

    bool is_found = false;
    for (int i = 0; i <= maxOutwardSearch; ++i) {
        // Use BFS with a fixed depth of 2, as in the original C++ code.
        if (breadthFirstSearch_c(&found_path, &search_dir, 2, find_directory_predicate, (void*)directoryName)) {
            is_found = true;
            break;
        }

        // If not found, prepare to search in the parent directory for the next loop iteration.
        // We do this by appending "/.." and letting cfs_get_absolute_path resolve it.
        char parent_path_str[CFS_MAX_PATH];
        snprintf(parent_path_str, CFS_MAX_PATH, "%s%c..", cfs_path_c_str(&search_dir), CFS_PATH_SEPARATOR);
        
        cfs_path absolute_parent_path;
        cfs_path_init(&absolute_parent_path);
        if (cfs_get_absolute_path(&absolute_parent_path, parent_path_str)) {
            // Update search_dir for the next loop.
            cfs_path_set(&search_dir, cfs_path_c_str(&absolute_parent_path));
        }
        cfs_path_free_storage(&absolute_parent_path);
    }

    const char* return_val = NULL;
    if (is_found) {
        // Copy result into the static buffer. Use strncpy for safety.
        strncpy(dirPaff, cfs_path_c_str(&found_path), 2047);
        dirPaff[2047] = '\0'; // Ensure null termination
        return_val = dirPaff;
    }

    // Clean up all heap-allocated path objects.
    cfs_path_free_storage(&search_dir);
    cfs_path_free_storage(&found_path);

    return return_val;
}

RGAPI bool FileExists(const char* filename){
    return cfs_exists(filename);
}

const char *GetDirectoryPath(const char *filePath)
{
    /*
    // NOTE: Directory separator is different in Windows and other platforms,
    // fortunately, Windows also support the '/' separator, that's the one should be used
    #if defined(_WIN32)
        char separator = '\\';
    #else
        char separator = '/';
    #endif
    */
    const char *lastSlash = NULL;
    static char dirPath[2048] = { 0 };
    memset(dirPath, 0, 2048);

    // In case provided path does not contain a root drive letter (C:\, D:\) nor leading path separator (\, /),
    // we add the current directory path to dirPath
    if ((filePath[1] != ':') && (filePath[0] != '\\') && (filePath[0] != '/'))
    {
        // For security, we set starting path to current directory,
        // obtained path will be concatenated to this
        dirPath[0] = '.';
        dirPath[1] = '/';
    }

    lastSlash = strprbrk(filePath, "\\/");
    if (lastSlash)
    {
        if (lastSlash == filePath)
        {
            // The last and only slash is the leading one: path is in a root directory
            dirPath[0] = filePath[0];
            dirPath[1] = '\0';
        }
        else
        {
            // NOTE: Be careful, strncpy() is not safe, it does not care about '\0'
            char *dirPathPtr = dirPath;
            if ((filePath[1] != ':') && (filePath[0] != '\\') && (filePath[0] != '/')) dirPathPtr += 2;     // Skip drive letter, "C:"
            memcpy(dirPathPtr, filePath, strlen(filePath) - (strlen(lastSlash) - 1));
            dirPath[strlen(filePath) - strlen(lastSlash) + (((filePath[1] != ':') && (filePath[0] != '\\') && (filePath[0] != '/'))? 2 : 0)] = '\0';  // Add '\0' manually
        }
    }

    return dirPath;
}
char *EncodeDataBase64(const unsigned char *data, int dataSize, int *outputSize)
{
    static const char base64encodeTable[] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
        'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
        'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
    };

    static const int modTable[] = { 0, 2, 1 };

    *outputSize = 4*((dataSize + 2)/3);

    char *encodedData = (char *)malloc(*outputSize);

    if (encodedData == NULL) return NULL;   // Security check

    for (int i = 0, j = 0; i < dataSize;)
    {
        unsigned int octetA = (i < dataSize)? (unsigned char)data[i++] : 0;
        unsigned int octetB = (i < dataSize)? (unsigned char)data[i++] : 0;
        unsigned int octetC = (i < dataSize)? (unsigned char)data[i++] : 0;

        unsigned int triple = (octetA << 0x10) + (octetB << 0x08) + octetC;

        encodedData[j++] = base64encodeTable[(triple >> 3*6) & 0x3F];
        encodedData[j++] = base64encodeTable[(triple >> 2*6) & 0x3F];
        encodedData[j++] = base64encodeTable[(triple >> 1*6) & 0x3F];
        encodedData[j++] = base64encodeTable[(triple >> 0*6) & 0x3F];
    }

    for (int i = 0; i < modTable[dataSize%3]; i++) encodedData[*outputSize - 1 - i] = '=';  // Padding character

    return encodedData;
}

// Decode Base64 string data
unsigned char *DecodeDataBase64(const unsigned char *data, int *outputSize)
{
    static const unsigned char base64decodeTable[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 62, 0, 0, 0, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0, 0, 0, 0, 0, 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
        37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
    };

    // Get output size of Base64 input data
    int outSize = 0;
    for (int i = 0; data[4*(size_t)i] != 0; i++)
    {
        if (data[4*i + 3] == '=')
        {
            if (data[4*i + 2] == '=') outSize += 1;
            else outSize += 2;
        }
        else outSize += 3;
    }

    // Allocate memory to store decoded Base64 data
    unsigned char *decodedData = (unsigned char *)malloc(outSize);

    for (int i = 0; i < outSize/3; i++)
    {
        unsigned char a = base64decodeTable[(int)data[4*(size_t)i]];
        unsigned char b = base64decodeTable[(int)data[4*(size_t)i + 1]];
        unsigned char c = base64decodeTable[(int)data[4*(size_t)i + 2]];
        unsigned char d = base64decodeTable[(int)data[4*(size_t)i + 3]];

        decodedData[3*(size_t)i] = (a << 2) | (b >> 4);
        decodedData[3*(size_t)i + 1] = (b << 4) | (c >> 2);
        decodedData[3*(size_t)i + 2] = (c << 6) | d;
    }

    if (outSize%3 == 1)
    {
        int n = outSize/3;
        unsigned char a = base64decodeTable[(int)data[4*(size_t)n]];
        unsigned char b = base64decodeTable[(int)data[4*(size_t)n + 1]];
        decodedData[outSize - 1] = (a << 2) | (b >> 4);
    }
    else if (outSize%3 == 2)
    {
        int n = outSize/3;
        unsigned char a = base64decodeTable[(int)data[4*(size_t)n]];
        unsigned char b = base64decodeTable[(int)data[4*(size_t)n + 1]];
        unsigned char c = base64decodeTable[(int)data[4*(size_t)n + 2]];
        decodedData[outSize - 2] = (a << 2) | (b >> 4);
        decodedData[outSize - 1] = (b << 4) | (c >> 2);
    }

    *outputSize = outSize;
    return decodedData;
}
#include "telegrama_render_literal.inc"
size_t telegrama_render_size1 = sizeof(telegrama_render1);
size_t telegrama_render_size2 = sizeof(telegrama_render2);
size_t telegrama_render_size3 = sizeof(telegrama_render3);

ShaderImpl* allocatedShaderIDs_shc = NULL;
uint32_t nextShaderID_shc = 0;
uint32_t capacity_shc = 0;
RGAPI ShaderImpl* GetShaderImplByID(uint32_t id){
    return allocatedShaderIDs_shc + id;
}
RGAPI ShaderImpl* GetShaderImpl(Shader shader){
    return GetShaderImplByID(shader.id);
}
RGAPI uint32_t getNextShaderID_shc(){
    if(nextShaderID_shc >= capacity_shc){
        uint32_t newCapacity = capacity_shc * 2 + (capacity_shc == 0) * 8;
        ShaderImpl* newAllocatedShaderIDs_shc = (ShaderImpl*)RL_CALLOC(newCapacity, sizeof(ShaderImpl));
        if(capacity_shc){
            memcpy(newAllocatedShaderIDs_shc, allocatedShaderIDs_shc, capacity_shc * sizeof(ShaderImpl));
        }
        if(allocatedShaderIDs_shc){
            RL_FREE(allocatedShaderIDs_shc);
        }
        capacity_shc = newCapacity;
        allocatedShaderIDs_shc = newAllocatedShaderIDs_shc; 
    }
    return nextShaderID_shc++;
}
