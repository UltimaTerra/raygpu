#include <raygpu.h>

RenderTexture tex;

void setup(){
    tex = LoadRenderTexture(400, 400);
}
void draw(){
    BeginDrawing();

    BeginTextureMode(tex);
    ClearBackground(BLANK);
    DrawText("Inside the render texture", 5,5,12,BLACK);
    DrawRectangleLines(0,0,300,300, BLACK);
    DrawRectangle(50, 50, 20, 20, RED);
    DrawRectangle(80, 80, 50, 100, GREEN);
    EndTextureMode();

    ClearBackground(BLUE);
    DrawTexturePro(tex.texture, (Rectangle){0,0,300,300}, (Rectangle){100,100,400,400}, (Vector2){0,0}, 0.0f, WHITE);
    DrawFPS(5, 5);
    EndDrawing();
}
int main(void){
    ProgramInfo progInfo = {
        .windowWidth = 1100,
        .windowHeight = 750,
        .windowTitle = "Rendertexture Example",
        .setupFunction = setup,
        .renderFunction = draw
    };
    InitProgram(progInfo);
}
