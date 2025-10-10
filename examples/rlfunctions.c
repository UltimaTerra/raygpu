#include <raygpu.h>

void mainloop(){
    BeginDrawing();
    ClearBackground(DARKBLUE);
    DrawRectangle(100,100,100,100,RED);
    drawCurrentBatch();
    rlScalef(100, 0, 0);
    DrawRectangle(100,100,100,10,GREEN);
    EndDrawing();
}
void setup(){

}

int main(){
    ProgramInfo prog = {
        .windowWidth = 1600,
        .windowHeight = 900,
        .setupFunction = setup,
        .renderFunction = mainloop,
    };
    InitProgram(prog);
}
