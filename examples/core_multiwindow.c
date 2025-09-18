#include <raygpu.h>

int main(void){
    const int screenWidth = 800;
    const int screenHeight = 600;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Primary Window");

    int secondWidth = 400, secondHeight = 400;
    SubWindow second = OpenSubWindow(secondWidth, secondHeight, "Secondary Window");
    rassert(second->handle != 0, "Window can't have null handle");
    SubWindow third = OpenSubWindow(secondWidth, secondHeight, "Third Window");
    RenderTexture rtex = LoadRenderTexture(800, 800);
    while(!WindowShouldClose()){
        
        // Start drawing with the third window
        // Input functions, e.g. GetMouseX() are specific to this window
        // inside BeginWindowMode() / EndWindowMode() 
        BeginWindowMode(third);
            ClearBackground(RED);
            DrawCircle(GetMouseX(), GetMouseY(), 50.0f, GREEN);
        EndWindowMode();

        // Draw second window
        BeginWindowMode(second);
            BeginTextureMode(rtex);
            ClearBackground(GREEN);
            DrawCircle(GetMouseX(), GetMouseY(), 50.0f, WHITE);
            EndTextureMode();
            DrawTexturePro(
                rtex.texture,
                (Rectangle){0,0,800,800},
                (Rectangle){200,200,300,300},
                (Vector2){0,0}, 0.0f, WHITE
            );
        EndWindowMode();

        // Draw primary window
        BeginDrawing();
            ClearBackground(DARKBLUE);
            DrawCircle(GetMouseX(), GetMouseY(), 50.0f, RED);
        EndDrawing();

    }
}
