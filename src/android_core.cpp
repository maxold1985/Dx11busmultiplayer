// Android NDK portable simulation entry points.
// No Win32, D3D11, JNI or Android Activity required to link this library.
#include "simulation.hpp"
#include <new>
#include <stdint.h>

#if defined(__GNUC__)
#define BUS_ANDROID_API __attribute__((visibility("default")))
#else
#define BUS_ANDROID_API
#endif

extern "C" {

BUS_ANDROID_API void* bus_android_create() {
    return new (std::nothrow) sim::Dynamics();
}

BUS_ANDROID_API void bus_android_destroy(void* handle) {
    delete static_cast<sim::Dynamics*>(handle);
}

BUS_ANDROID_API void bus_android_set_input(
    void* handle,
    float throttle,
    float steering,
    float brake,
    uint32_t flags
) {
    if(!handle) return;
    sim::input(*static_cast<sim::Dynamics*>(handle),
        throttle, steering, brake, flags);
}

BUS_ANDROID_API void bus_android_step(void* handle, float dt) {
    if(!handle || dt<=0.0f || dt>0.1f) return;
    sim::step(*static_cast<sim::Dynamics*>(handle), dt);
}

BUS_ANDROID_API int bus_android_get_state(void* handle, BusState* out) {
    if(!handle || !out) return 0;
    *out=static_cast<sim::Dynamics*>(handle)->b;
    return 1;
}

BUS_ANDROID_API int bus_android_state_size() {
    return (int)sizeof(BusState);
}

}
