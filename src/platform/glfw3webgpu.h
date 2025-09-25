// begin file src/glfw3webgpu.h
#ifndef GLFW3_WEBGPU_H_INCLUDED
#define GLFW3_WEBGPU_H_INCLUDED

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>

#ifdef __cplusplus
extern "C" {
#endif
WGPUSurface glfwCreateWindowWGPUSurface(WGPUInstance instance, GLFWwindow* window);
#ifdef __cplusplus
}
#endif

#endif // GLFW3_WEBGPU_H_INCLUDED


// end file src/glfw3webgpu.h