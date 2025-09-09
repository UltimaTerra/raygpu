#include <vector>
#include <mutex>
#include <unordered_map>
#include <map>
#include <deque>
#include <array>



struct wgpustate{
    WGPUInstance instance{};
    WGPUAdapter adapter{};
    WGPUDevice device{};
    WGPUQueue queue{};
};

#include "renderstate.hpp"