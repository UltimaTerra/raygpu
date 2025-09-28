#include <webgpu/webgpu.h>
#include <emscripten/html5.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    bool gotAdapter;
    bool requestedDevice;
    bool gotDevice;
} App;

static void onAdapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void* userdata1,
    void* userdata2)
{
    App* app = (App*)userdata1;
    if (status == WGPURequestAdapterStatus_Success) {
        app->adapter = adapter;
        app->gotAdapter = true;
        printf("Adapter OK: %p\n", adapter);
    } else {
        printf("Adapter failed: %.*s\n", (int)message.length, message.data);
    }
}

static void onDevice(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void* userdata1,
    void* userdata2)
{
    App* app = (App*)userdata1;
    if (status == WGPURequestDeviceStatus_Success) {
        app->device = device;
        app->gotDevice = true;
        printf("Device OK: %p\n", device);
    } else {
        printf("Device failed: %.*s\n", (int)message.length, message.data);
    }
}
static EM_BOOL tigg(double time, void* userdata) {
    printf("kuedder\n");
    return EM_FALSE;
}
static EM_BOOL tick(double time, void* userdata) {
    App* app = (App*)userdata;
    (void)time;
    if (!app->gotAdapter) {
        // still waiting for adapter callback
        return EM_TRUE;
    }

    if (!app->requestedDevice) {
        WGPUDeviceDescriptor devDesc = {0};
        WGPURequestDeviceCallbackInfo cb = {
            .callback = onDevice,
            .mode = WGPUCallbackMode_AllowSpontaneous,
            .userdata1 = app,
        };
        wgpuAdapterRequestDevice(app->adapter, &devDesc, cb);
        app->requestedDevice = true;
        return EM_TRUE;
    }

    if (!app->gotDevice) {
        // still waiting for device callback
        return EM_TRUE;
    }

    // You have a device here. Do setup or draw.
    // Keep returning EM_TRUE if you want continuous frames.
    // Return EM_FALSE to stop this RAF loop once init is done.
    emscripten_request_animation_frame_loop(tigg, NULL);
    return EM_FALSE;
}

int main() {
    static App app = {0};

    app.instance = wgpuCreateInstance(NULL);
    printf("Instance: %p\n", app.instance);

    WGPURequestAdapterOptions adapterOpts = {0};
    adapterOpts.featureLevel = WGPUFeatureLevel_Core;
    // adapterOpts.compatibleSurface = <your WGPUSurface> if targeting a canvas

    WGPURequestAdapterCallbackInfo cb = {
        .callback  = onAdapter,
        .mode      = WGPUCallbackMode_AllowSpontaneous, // required on web
        .userdata1 = &app,
    };

    wgpuInstanceRequestAdapter(app.instance, &adapterOpts, cb);

    // Drive progress on the browser event loop.
    emscripten_request_animation_frame_loop(tick, &app);
    printf("When is this called\n");
    return 0;
}
