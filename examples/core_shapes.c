#include <raygpu.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

Texture tex = {0};


// This function only runs once
void setup(){
    tex = LoadTextureFromImage(GenImageChecker(WHITE, BLACK, 100, 100, 10));
}

// This function gets called repeatedly
void render(){
    BeginDrawing();
    ClearBackground((Color) {20,50,50,255});
        
    DrawRectangle(100,100,100,100,WHITE);
    DrawTexturePro(tex, CLITERAL(Rectangle) {0,0,100,100}, CLITERAL(Rectangle){200,100,100,100}, (Vector2){0,0}, 0.0f, WHITE);
    DrawCircle(GetMouseX(), GetMouseY(), 40, WHITE);
    DrawCircleV(GetMousePosition(), 20, CLITERAL(Color){255,0,0,255});
    DrawCircle(600, 300, 200, CLITERAL(Color){255,0,0,100});
    
    DrawFPS(5, 5);
    EndDrawing();
}

int main(void){
    ProgramInfo program = {
        .windowTitle = "Shapes Example",
        .windowWidth = 800,
        .windowHeight = 600,
        .setupFunction = setup,
        .renderFunction = render
    };
    InitProgram(program);
    //InitWindowEx(ctx);
}
