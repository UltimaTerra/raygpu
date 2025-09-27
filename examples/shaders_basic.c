#include <raygpu.h>
const char fragSourceGLSL[] = 
"#version 450\n"
"#extension GL_ARB_separate_shader_objects : enable  // Enable separate sampler objects if needed\n"
"// Inputs from vertex shader.\n"
"layout(location = 0) in vec2 frag_uv;\n"
"layout(location = 1) in vec4 frag_color;\n"
"// Output fragment color.\n"
"layout(location = 0) out vec4 outColor;\n"
"// Texture and sampler, bound separately.\n"
"layout(binding = 1) uniform texture2D texture0;  // Texture (binding = 1)\n"
"layout(binding = 2) uniform sampler texSampler;    // Sampler (binding = 2)\n"
"void main() {\n"
"    // Sample the texture using the combined sampler.\n"
"    vec4 texColor = texture(sampler2D(texture0, texSampler), frag_uv);\n"
"    outColor = (texColor * frag_color).yzxw + vec4(0.4f,0,0,0.4f);\n"
"}\n";

int main(){
    InitWindow(800, 800, "Shaders example");
    Shader colorInverter = LoadShaderFromMemory(NULL, fragSourceGLSL);
    Matrix matrix = ScreenMatrix(GetScreenWidth(), GetScreenHeight());
    Matrix identity = MatrixIdentity();
    DescribedBuffer* matrixbuffer = GenUniformBuffer(&matrix, sizeof(Matrix));
    DescribedBuffer* matrixbuffers = GenStorageBuffer(&identity, sizeof(Matrix));
    
    Texture tex = LoadTexture(TextFormat("%s/tileset.png", FindDirectory("resources", 3)));
    DescribedSampler sampler = LoadSampler(TEXTURE_WRAP_REPEAT, TEXTURE_FILTER_BILINEAR);
    
    while(!WindowShouldClose()){
        BeginDrawing();
        ClearBackground(BLACK);
        BeginShaderMode(colorInverter);
        SetShaderUniformBuffer(colorInverter, 0, matrixbuffer);
        SetShaderTexture(colorInverter, 1, tex);
        SetShaderSampler(colorInverter, 2, sampler);
        SetShaderStorageBuffer(colorInverter, 3, matrixbuffers);
        
        DrawTexturePro(
            tex,
            (Rectangle){0, 0, (float)tex.width, (float)tex.height}, 
            (Rectangle){0, 0, (float)tex.width, (float)tex.height}, 
            (Vector2)  {0, 0},
            0.0f,
            WHITE
        );
        
        EndShaderMode();
        DrawFPS(5, 5);
        EndDrawing();
    }
}