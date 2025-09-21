#include <algorithm>
#include <raygpu.h>
#include <vector>
#include <random>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#if SUPPORT_GLSL_PARSER == 0
const char wgsl[] = R"(
struct VertexInput {
    @location(0) position: vec2f,
    @location(1) offset: vec2f
};
struct VertexOutput {
    @builtin(position) position: vec4f
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = vec4f(in.position.xy + in.offset.xy, 0.0f, 1.0f);
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return vec4f(in.position.xy / 400.0f,1,1);
}
)";


const char computeSource[] = R"(
@group(0) @binding(0) var<storage,read_write> posBuffer: array<vec2<f32>>;
@group(0) @binding(1) var<storage,read> velBuffer: array<vec2<f32>>;

@compute
@workgroup_size(64, 1, 1)
fn compute_main(@builtin(global_invocation_id) id: vec3<u32>) {
    posBuffer[id.x * 64 + id.y] = posBuffer[id.x * 64 + id.y] + velBuffer[id.x * 64 + id.y];
})";
#elif SUPPORT_GLSL_PARSER == 1
const char vertexSource[] = R"(#version 450 core

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 offset;

layout(location = 0) out vec4 coler;

void main() {
    float dist = length(offset);
    //coler = vec4(1.0f / dist, 1.0f / (dist * 3.0f) * (dist * 3.0f), 1.0f / (dist * 3.0f) * (dist * 3.0f), 1.0f);
    coler = vec4(0.1f / dist, 0.0f, 0.0f, 1.0f);
    gl_Position = vec4(position.xy + offset.xy, 0.0f, 1.0f);
}
)";
const char fragmentSource[] = R"(#version 450 core

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec4 coler;
void main() {
    outColor = coler;
    //outColor = vec4(gl_FragCoord.xy / 400.0f, 1.0f, 1.0f);
}

)";

const char computeSource[] = R"(#version 450 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) buffer posBuffer {
    vec2 posBufferdata[];
};

layout(set = 0, binding = 1, std430) buffer velBuffer {
    vec2 velBufferdata[];
};

void main() {
    // Get the global invocation ID (matches WGSL's @builtin(global_invocation_id))
    uvec3 id = gl_GlobalInvocationID;
    
    uint index = id.x;// * 64u + id.y;
    vec2 pos = posBufferdata[index];
    vec2 accel = -pos / pow(dot(pos, pos), 1);
    velBufferdata[index] += accel * 0.000001f;
    posBufferdata[index] = posBufferdata[index] + velBufferdata[index];
}

)";


#endif
Shader rpl;
DescribedComputePipeline* firstPassPipeline;
VertexArray* vao;
DescribedBuffer* quad;
DescribedBuffer* positions;
DescribedBuffer* velocities;
DescribedBuffer* positionsnew;

constexpr bool headless = false;

constexpr size_t parts = (1 << 14);
void mainloop(void){
    BeginDrawing();
    BeginComputepass();
    BindComputePipeline(firstPassPipeline);
    DispatchCompute(parts / 64, 64, 1);
    EndComputepass();
    ClearBackground(BLACK);

    BeginShaderMode(rpl);
    BindShaderVertexArray(rpl, vao);
    DrawArraysInstanced(RL_TRIANGLE_STRIP, 4, parts);
    EndShaderMode();

    DrawFPS(10, 10);
    EndDrawing();
    if(false && headless){
        char b[64] = {0};

        snprintf(b, 64, "frame%04d.bmp", (int)GetFrameCount());
        TRACELOG(LOG_INFO, "Saving screenshot %s", b);
        TakeScreenshot(b);
    }
}
int main(){
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Compute Shader");
    SetTargetFPS(60);
    
    
    std::mt19937_64 gen(53);
    std::uniform_real_distribution<float> dis(-1,1);
    std::normal_distribution<float> ndis(0.6,0.2);
    std::normal_distribution<float> ndis2(1,0.3);
    std::vector<Vector2> pos(parts), vel(parts);
    
    for(size_t i = 0;i < parts;i++){
        float arg = M_PI * (dis(gen) + 1);
        float mag = std::sqrt(dis(gen)) * 0.2f;
        float radius = ndis(gen) + 0.2f;
        pos[i] = Vector2{std::cos(arg) * radius, std::sin(arg) * radius};
        vel[i] = Vector2{pos[i].y, -pos[i].x} / Vector2Norm(pos[i]) / 700.0f * ndis2(gen);
    }
    //Vertex Coordinates for a square
    float quadpos[8] = {
        0,0,
        1,0,
        0,1,
        1,1
    };
    for(int i = 0;i < 8;i++){
        quadpos[i] *= 0.004f;
    }
    quad = GenBufferEx(quadpos, sizeof(quadpos), WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);

    // GenBufferEx is required for Vertex and Storage Buffer Usage
    positions = GenBufferEx(pos.data(), pos.size() * sizeof(Vector2),  WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    velocities = GenBufferEx(vel.data(), vel.size() * sizeof(Vector2), WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

    firstPassPipeline = LoadComputePipeline(computeSource);
    SetBindgroupStorageBuffer(&firstPassPipeline->bindGroup, GetComputeShaderLocation(firstPassPipeline, "posBuffer"), positions);
    SetBindgroupStorageBuffer(&firstPassPipeline->bindGroup, GetComputeShaderLocation(firstPassPipeline, "velBuffer"), velocities);
    
    //SetBindgroupStorageBuffer(&cpl->bindGroup, GetUniformLocationCompute(cpl, "posBuffer"), positions);
    //SetBindgroupStorageBuffer(&cpl->bindGroup, GetUniformLocationCompute(cpl, "velBuffer"), velocities);

    vao = LoadVertexArray();
    VertexAttribPointer(vao, quad, 0, WGPUVertexFormat_Float32x2, 0, WGPUVertexStepMode_Vertex);
    VertexAttribPointer(vao, positions, 1, WGPUVertexFormat_Float32x2, 0, WGPUVertexStepMode_Instance);
    #if SUPPORT_GLSL_PARSER == 0
    rpl = LoadShaderSingleSource(wgsl);
    #elif SUPPORT_GLSL_PARSER == 1
    rpl = LoadShaderFromMemory(vertexSource, fragmentSource);
    #endif
    #ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(mainloop, 0, 0);
    #else
    while(!WindowShouldClose()){
        mainloop();
    }
    #endif
    return 0;
}
