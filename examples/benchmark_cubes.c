#include <raygpu.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
const char cubeShaderSource[] = 
"struct VertexInput {\n"
"    @location(0) position: vec3<f32>,\n"
"    @location(1) uv: vec2f,\n"
"    @location(2) normal: vec3f,\n"
"    @location(3) color: vec4f\n"
"};\n"
"struct VertexOutput {\n"
"    @builtin(position) position: vec4f,\n"
"    @location(0) color: vec4f,\n"
"    @location(1) normal: vec3f\n"
"};\n"
"@group(0) @binding(0) var <uniform> Perspective_View: mat4x4<f32>;\n"
"@group(0) @binding(1) var texture0: texture_2d<f32>;\n"
"@group(0) @binding(2) var texSampler: sampler;\n"
"@group(0) @binding(3) var<storage> modelMatrix: array<vec4f>;\n"
"@vertex fn vs_main(@builtin(instance_index) instanceIdx: u32, in: VertexInput) -> VertexOutput {\n"
"    var out: VertexOutput;\n"
"    out.position = Perspective_View * vec4f(modelMatrix[instanceIdx].xyz + in.position, 1.0f);\n"
"    out.normal = in.normal;\n"
"    out.color = in.color;\n"
"    return out;\n"
"}\n"
"@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {\n"
"    return vec4f((in.color * dot(in.normal, vec3f(-0.3,0.8,-0.55))).xyz, 1.0f);\n"
"    \n"
"}\n";

Shader shader;
Mesh cubeMesh;
Camera3D cam;
size_t instanceCount;
Vector4* instancetransforms;
void mainloop(){
    BeginDrawing();
    ClearBackground(BLANK);

    BeginShaderMode(shader);
    UseNoTexture();
    BeginMode3D(cam);
    BindShaderVertexArray(shader, cubeMesh.vao);
    DrawArraysIndexedInstanced(RL_TRIANGLES, *cubeMesh.ibo, 36, instanceCount);
    //DrawMeshInstanced(cubeMesh, Material{}, instancetransforms.data(), instancetransforms.size());
    EndMode3D();
    EndShaderMode();
    DrawFPS(0, 10);
    if(IsKeyPressed(KEY_P)){
        TakeScreenshot("screenshot.png");
    }
    if(IsKeyDown(KEY_W)){
        cam.position.y += GetFrameTime() * 0.1f;
    }
    if(IsKeyDown(KEY_S)){
        cam.position.y -= GetFrameTime() * 0.1f;
    }
    EndDrawing();
}

int main(){
    //SetConfigFlags(FLAG_MSAA_4X_HINT);
    //SetConfigFlags(FLAG_FULLSCREEN_MODE);
    InitWindow(2560, 1440, "Cube Benchmark");
    SetTargetFPS(0);
    float scale = 1.0f;
    cubeMesh = GenMeshCube(scale, scale, scale);
    cam = CLITERAL(Camera3D){
        .position = {0,70,0},
        .target = {100,0,100},
        .up = {0,1,0},
        .fovy = 60.0f
    };
    shader = LoadShaderSingleSource(cubeShaderSource);
    DescribedSampler smp = LoadSampler(TEXTURE_WRAP_REPEAT, TEXTURE_FILTER_BILINEAR);
    SetShaderSampler(shader, 2, smp);
    instanceCount = 2000 * 2000;
    instancetransforms = RL_CALLOC(instanceCount, sizeof(Vector4));
    size_t insertIndex = 0;
    for(int i = 0;i < 2000;i++){
        for(int j = 0;j < 2000;j++){
            instancetransforms[insertIndex++] = (Vector4){
                (float)i,
                (float)(sin(2 * M_PI * i / 10.0) + 15.0 * -cos(2.0 * M_PI * j / 90.0)),
                (float)j,
                (float)0
            };
        }
    }
    DescribedBuffer* persistent = GenStorageBuffer(instancetransforms, instanceCount * sizeof(Vector4));
    SetShaderStorageBuffer(shader, 3, persistent);
    //TRACELOG(LOG_WARNING, "OOO: %llu", (unsigned long long)persistent.buffer);
    #ifndef __EMSCRIPTEN__
    while(!WindowShouldClose()){
        mainloop();
    }
    #else
    emscripten_set_main_loop(mainloop, 0, 0);
    #endif
}
