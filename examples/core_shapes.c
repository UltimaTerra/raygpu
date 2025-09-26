#include <raygpu.h>
Texture tex = {0};
void mainloop(){
    BeginDrawing();
    ClearBackground((Color) {20,50,50,255});
        
    DrawRectangle(100,100,100,100,WHITE);
    DrawTexturePro(tex, CLITERAL(Rectangle) {0,0,100,100}, CLITERAL(Rectangle){200,100,100,100}, (Vector2){0,0}, 0.0f, WHITE);
    DrawCircle(GetMouseX(), GetMouseY(), 40, WHITE);
    DrawCircleV(GetMousePosition(), 20, CLITERAL(Color){255,0,0,255});
    DrawCircle(600, 300, 200, CLITERAL(Color){255,0,0,100});
    
    DrawFPS(0, 0);
    EndDrawing();
}
int main(void){
    //SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(800, 600, "Shapes Example");
    tex = LoadTextureFromImage(GenImageChecker(WHITE, BLACK, 100, 100, 10));

    #ifndef __EMSCRIPTEN__
    while(!WindowShouldClose()){
        mainloop();
    }
    
    #else
    emscripten_set_main_loop(mainloop, 0, 0);
    #endif
}
