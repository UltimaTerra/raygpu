
typedef struct wgpustate{
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
}wgpustate;

#include "renderstate.hpp"