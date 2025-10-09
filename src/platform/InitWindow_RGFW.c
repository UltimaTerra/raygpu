// begin file src/InitWindow_RGFW.cpp
#define RGFW_IMPLEMENTATION
#define RGFW_USE_XDL
#ifndef VK_KHR_xlib_surface
    #define VK_KHR_xlib_surface 1
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#define RGFW_IMPLEMENTATION
#define Font rlFont
    #include <raygpu.h>
#undef Font

enum RGFW_key_alt {
	alt_RGFW_keyNULL = 0,
	alt_RGFW_escape = '\033',
	alt_RGFW_backtick = '`',
	alt_RGFW_0 = '0',
	alt_RGFW_1 = '1',
	alt_RGFW_2 = '2',
	alt_RGFW_3 = '3',
	alt_RGFW_4 = '4',
	alt_RGFW_5 = '5',
	alt_RGFW_6 = '6',
	alt_RGFW_7 = '7',
	alt_RGFW_8 = '8',
	alt_RGFW_9 = '9',
	alt_RGFW_minus = '-',
	alt_RGFW_equals = '=',
	alt_RGFW_backSpace = '\b',
	alt_RGFW_tab = '\t',
	alt_RGFW_space = ' ',
	alt_RGFW_a = 'a',
	alt_RGFW_b = 'b',
	alt_RGFW_c = 'c',
	alt_RGFW_d = 'd',
	alt_RGFW_e = 'e',
	alt_RGFW_f = 'f',
	alt_RGFW_g = 'g',
	alt_RGFW_h = 'h',
	alt_RGFW_i = 'i',
	alt_RGFW_j = 'j',
	alt_RGFW_k = 'k',
	alt_RGFW_l = 'l',
	alt_RGFW_m = 'm',
	alt_RGFW_n = 'n',
	alt_RGFW_o = 'o',
	alt_RGFW_p = 'p',
	alt_RGFW_q = 'q',
	alt_RGFW_r = 'r',
	alt_RGFW_s = 's',
	alt_RGFW_t = 't',
	alt_RGFW_u = 'u',
	alt_RGFW_v = 'v',
	alt_RGFW_w = 'w',
	alt_RGFW_x = 'x',
	alt_RGFW_y = 'y',
	alt_RGFW_z = 'z',
	alt_RGFW_period = '.',
	alt_RGFW_comma = ',',
	alt_RGFW_slash = '/',
	alt_RGFW_bracket = '{',
	alt_RGFW_closeBracket = '}',
	alt_RGFW_semicolon = ';',
	alt_RGFW_apostrophe = '\'',
	alt_RGFW_backSlash = '\\',
	alt_RGFW_return = '\n',
	alt_RGFW_delete = '\177', /* 127 */
	alt_RGFW_F1,
	alt_RGFW_F2,
	alt_RGFW_F3,
	alt_RGFW_F4,
	alt_RGFW_F5,
	alt_RGFW_F6,
	alt_RGFW_F7,
	alt_RGFW_F8,
	alt_RGFW_F9,
	alt_RGFW_F10,
	alt_RGFW_F11,
	alt_RGFW_F12,
	alt_RGFW_capsLock,
	alt_RGFW_shiftL,
	alt_RGFW_controlL,
	alt_RGFW_altL,
	alt_RGFW_superL,
	alt_RGFW_shiftR,
	alt_RGFW_controlR,
	alt_RGFW_altR,
	alt_RGFW_superR,
	alt_RGFW_up,
	alt_RGFW_down,
	alt_RGFW_left,
	alt_RGFW_right,
	alt_RGFW_insert,
	alt_RGFW_end,
	alt_RGFW_home,
	alt_RGFW_pageUp,
	alt_RGFW_pageDown,
	alt_RGFW_numLock,
	alt_RGFW_KP_Slash,
	alt_RGFW_multiply,
	alt_RGFW_KP_Minus,
	alt_RGFW_KP_1,
	alt_RGFW_KP_2,
	alt_RGFW_KP_3,
	alt_RGFW_KP_4,
	alt_RGFW_KP_5,
	alt_RGFW_KP_6,
	alt_RGFW_KP_7,
	alt_RGFW_KP_8,
	alt_RGFW_KP_9,
	alt_RGFW_KP_0,
	alt_RGFW_KP_Period,
	alt_RGFW_KP_Return,
	alt_RGFW_scrollLock,
	alt_RGFW_keyLast
};

const int keyMappingRGFW_(int RGFWKey){
    switch(RGFWKey){
        default:
        case alt_RGFW_keyNULL:return KEY_NULL;
        case alt_RGFW_return:return KEY_ENTER;
        case alt_RGFW_apostrophe:return KEY_APOSTROPHE;
        case alt_RGFW_comma:return KEY_COMMA;
        case alt_RGFW_minus:return KEY_MINUS;
        case alt_RGFW_period:return KEY_PERIOD;
        case alt_RGFW_slash:return KEY_SLASH;
        case alt_RGFW_escape:return KEY_ESCAPE;
        case alt_RGFW_F1:return KEY_F1;
        case alt_RGFW_F2:return KEY_F2;
        case alt_RGFW_F3:return KEY_F3;
        case alt_RGFW_F4:return KEY_F4;
        case alt_RGFW_F5:return KEY_F5;
        case alt_RGFW_F6:return KEY_F6;
        case alt_RGFW_F7:return KEY_F7;
        case alt_RGFW_F8:return KEY_F8;
        case alt_RGFW_F9:return KEY_F9;
        case alt_RGFW_F10:return KEY_F10;
        case alt_RGFW_F11:return KEY_F11;
        case alt_RGFW_F12:return KEY_F12;
        case alt_RGFW_backtick:return KEY_GRAVE;
        case alt_RGFW_0:return KEY_ZERO;
        case alt_RGFW_1:return KEY_ONE;
        case alt_RGFW_2:return KEY_TWO;
        case alt_RGFW_3:return KEY_THREE;
        case alt_RGFW_4:return KEY_FOUR;
        case alt_RGFW_5:return KEY_FIVE;
        case alt_RGFW_6:return KEY_SIX;
        case alt_RGFW_7:return KEY_SEVEN;
        case alt_RGFW_8:return KEY_EIGHT;
        case alt_RGFW_9:return KEY_NINE;
        case alt_RGFW_equals:return KEY_EQUAL;
        case alt_RGFW_backSpace:return KEY_BACKSPACE;
        case alt_RGFW_tab:return KEY_TAB;
        case alt_RGFW_capsLock:return KEY_CAPS_LOCK;
        case alt_RGFW_shiftL:return KEY_LEFT_SHIFT;
        case alt_RGFW_controlL:return KEY_LEFT_CONTROL;
        case alt_RGFW_altL:return KEY_LEFT_ALT;
        case alt_RGFW_superL:return KEY_LEFT_SUPER;
        case alt_RGFW_shiftR:return KEY_RIGHT_SHIFT;
        case alt_RGFW_altR:return KEY_RIGHT_ALT;
        case alt_RGFW_space:return KEY_SPACE;
        case alt_RGFW_a:return KEY_A;
        case alt_RGFW_b:return KEY_B;
        case alt_RGFW_c:return KEY_C;
        case alt_RGFW_d:return KEY_D;
        case alt_RGFW_e:return KEY_E;
        case alt_RGFW_f:return KEY_F;
        case alt_RGFW_g:return KEY_G;
        case alt_RGFW_h:return KEY_H;
        case alt_RGFW_i:return KEY_I;
        case alt_RGFW_j:return KEY_J;
        case alt_RGFW_k:return KEY_K;
        case alt_RGFW_l:return KEY_L;
        case alt_RGFW_m:return KEY_M;
        case alt_RGFW_n:return KEY_N;
        case alt_RGFW_o:return KEY_O;
        case alt_RGFW_p:return KEY_P;
        case alt_RGFW_q:return KEY_Q;
        case alt_RGFW_r:return KEY_R;
        case alt_RGFW_s:return KEY_S;
        case alt_RGFW_t:return KEY_T;
        case alt_RGFW_u:return KEY_U;
        case alt_RGFW_v:return KEY_V;
        case alt_RGFW_w:return KEY_W;
        case alt_RGFW_x:return KEY_X;
        case alt_RGFW_y:return KEY_Y;
        case alt_RGFW_z:return KEY_Z;
        case alt_RGFW_bracket:return KEY_LEFT_BRACKET;
        case alt_RGFW_backSlash:return KEY_BACKSLASH;
        case alt_RGFW_closeBracket:return KEY_RIGHT_BRACKET;
        case alt_RGFW_semicolon:return KEY_SEMICOLON;
        case alt_RGFW_insert:return KEY_INSERT;
        case alt_RGFW_home:return KEY_HOME;
        case alt_RGFW_pageUp:return KEY_PAGE_UP;
        case alt_RGFW_delete:return KEY_DELETE;
        case alt_RGFW_end:return KEY_END;
        case alt_RGFW_pageDown:return KEY_PAGE_DOWN;
        case alt_RGFW_right:return KEY_RIGHT;
        case alt_RGFW_left:return KEY_LEFT;
        case alt_RGFW_down:return KEY_DOWN;
        case alt_RGFW_up:return KEY_UP;
        case alt_RGFW_numLock:return KEY_NUM_LOCK;
        case alt_RGFW_KP_Slash:return KEY_KP_DIVIDE;
        case alt_RGFW_multiply:return KEY_KP_MULTIPLY;
        case alt_RGFW_KP_Minus:return KEY_KP_SUBTRACT;
        case alt_RGFW_KP_Return:return KEY_KP_ENTER;
        case alt_RGFW_KP_1:return KEY_KP_1;
        case alt_RGFW_KP_2:return KEY_KP_2;
        case alt_RGFW_KP_3:return KEY_KP_3;
        case alt_RGFW_KP_4:return KEY_KP_4;
        case alt_RGFW_KP_5:return KEY_KP_5;
        case alt_RGFW_KP_6:return KEY_KP_6;
        case alt_RGFW_KP_7:return KEY_KP_7;
        case alt_RGFW_KP_8:return KEY_KP_8;
        case alt_RGFW_KP_9:return KEY_KP_9;
        case alt_RGFW_KP_0:return KEY_KP_0;
        case alt_RGFW_KP_Period:return KEY_KP_DECIMAL;
        case alt_RGFW_scrollLock:return KEY_SCROLL_LOCK;
    }
};
#if SUPPORT_VULKAN_BACKEND == 1
    #define RGFW_VULKAN
    #include <external/volk.h>
#else
    #ifdef __EMSCRIPTEN__
        #define RGFW_WASM
    #endif
#endif
#define RGFWDEF
#define RGFW_USE_XDL
#define RGFW_NO_DPI
#include <external/RGFW.h>
#include <internals.h>
#include <renderstate.h>
#if SUPPORT_VULKAN_BACKEND == 1
    #include <wgvk_structs_impl.h>
#endif
#if SUPPORT_VULKAN_BACKEND == 1
VkBool32 RGFW_getVKPresentationSupport_noinline(VkInstance instance, VkPhysicalDevice pd, uint32_t i){
    return RGFW_getVKPresentationSupport(instance, pd, i);
}
#endif
void keyfunc_rgfw(RGFW_window* window, RGFW_key key, unsigned char keyChar, RGFW_keymod keyMod, RGFW_bool pressed) {
    
    KeyboardKey kii = keyMappingRGFW_(keyMod);
    CreatedWindowMap_get(&g_renderstate.createdSubwindows, window)->input_state.keydown[kii] = pressed ? 1 : 0;
}
void mouseMotionfunc_rgfw(RGFW_window* win, RGFW_point point, RGFW_point vector){
    CreatedWindowMap_get(&g_renderstate.createdSubwindows, win)->input_state.mousePos = CLITERAL(Vector2){(float)point.x, (float)point.y};
}
void windowQuitfunc_rgfw(RGFW_window* window){
    g_renderstate.closeFlag = true;
}
void windowResizedfunc_rgfw(RGFW_window* window, RGFW_rect rect){
    FullSurface* const surface = &CreatedWindowMap_get(&g_renderstate.createdSubwindows, window)->surface;
    ResizeSurface(surface, rect.w, rect.h);
    if((void*)window == (void*)g_renderstate.window){
        g_renderstate.mainWindowRenderTarget = surface->renderTarget;
    }
    Matrix newcamera = ScreenMatrix(rect.w, rect.h);
}

void PollEvents_RGFW(){
    for(size_t i = 0;i < g_renderstate.createdSubwindows.current_capacity;i++){
        RGWindowImpl* pptr = &g_renderstate.createdSubwindows.table[i].value;
        if(pptr->type == windowType_rgfw){
            RGFW_window_checkEvents((RGFW_window*)pptr->handle, 1);
        }
    }
}
void setupRGFWCallbacks(RGFW_window* window){
    RGFW_setKeyCallback(keyfunc_rgfw);
    RGFW_setWindowResizedCallback(windowResizedfunc_rgfw);
    RGFW_setMousePosCallback(mouseMotionfunc_rgfw);
    RGFW_setWindowQuitCallback(windowQuitfunc_rgfw);
}

RGAPI WGPUSurface CreateSurfaceForWindow_RGFW(void* windowHandle){    
    WGPUSurface surf = (WGPUSurface)RGFW_GetWGPUSurface(GetInstance(), (RGFW_window*) windowHandle);
    CreatedWindowMap_get(&g_renderstate.createdSubwindows, windowHandle)->scaleFactor = 1;
    return surf;
}

SubWindow InitWindow_RGFW(int width, int height, const char* title){
    SubWindow ret = callocnew(RGWindowImpl);
    ret->handle = RGFW_createWindow(title, (RGFW_rect){0, 0, width, height}, RGFW_windowNoInitAPI | RGFW_windowNoResize);
    ret->type = windowType_rgfw;
    ret->scaleFactor = 1.0f;
    CreatedWindowMap_put(&g_renderstate.createdSubwindows, ret->handle, *ret);
    ret = CreatedWindowMap_get(&g_renderstate.createdSubwindows, ret->handle);
    setupRGFWCallbacks((RGFW_window*)ret->handle);
    return ret;
}



// end file src/InitWindow_RGFW.cpp