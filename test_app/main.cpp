/*
 * rt_test_app.cpp – Test application for polaris_rt_layer
 *
 * Tests:
 *  1. Instance creation with VK_LAYER_POLARIS_RT
 *  2. Physical device enumeration and RT extension advertisement
 *  3. Logical device creation with all advertised RT extensions
 *  4. vkGetPhysicalDeviceFeatures2 / Properties2 (RT feature flags)
 *  5. vkCreateAccelerationStructureKHR / vkGetAccelerationStructureDeviceAddressKHR
 *  6. vkCreateRayTracingPipelinesKHR (verifies non-null handles)
 *  7. vkCreateDeferredOperationKHR (new deferred-ops stubs)
 */

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_ENABLE_BETA_EXTENSIONS

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>

#define VK_CHECK(call) \
    do { \
        VkResult _r = (call); \
        if (_r != VK_SUCCESS) { \
            std::cerr << "FAIL [" << #call << "] result=" << _r \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return 1; \
        } \
    } while(0)

#define LOAD_DEV(name) \
    PFN_##name fp##name = (PFN_##name)vkGetDeviceProcAddr(device, #name); \
    if (!fp##name) { std::cerr << "FAIL: could not load " #name "\n"; return 1; }

// ─────────────────────────────────────────────────────────────────────────────
static int checkRTExtensions(VkPhysicalDevice pd) {
    uint32_t cnt = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &cnt, nullptr);
    std::vector<VkExtensionProperties> exts(cnt);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &cnt, exts.data());

    const char* wantedExts[] = {
        "VK_KHR_acceleration_structure",
        "VK_KHR_ray_tracing_pipeline",
        "VK_KHR_ray_query",
        "VK_KHR_deferred_host_operations",
    };

    int found = 0;
    for (auto& want : wantedExts) {
        for (auto& e : exts)
            if (strcmp(e.extensionName, want) == 0) { ++found; break; }
    }

    std::cout << "RT extension check: " << found << "/" << (int)(sizeof(wantedExts)/sizeof(wantedExts[0]))
              << " present\n";
    return (found == (int)(sizeof(wantedExts)/sizeof(wantedExts[0]))) ? 0 : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/) {
    std::cout << "=== Polaris RT Layer Test App ===\n";

    // ── 1. Instance ──────────────────────────────────────────────────────────
    const char* layerName = "VK_LAYER_POLARIS_RT";

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "Polaris RT Test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "No Engine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_MAKE_VERSION(1, 3, 0);

    VkInstanceCreateInfo instCI{};
    instCI.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo        = &appInfo;
    instCI.enabledLayerCount       = 1;
    instCI.ppEnabledLayerNames     = &layerName;

    VkInstance instance = VK_NULL_HANDLE;
    VK_CHECK(vkCreateInstance(&instCI, nullptr, &instance));
    std::cout << "[PASS] vkCreateInstance\n";

    // ── 2. Physical device ───────────────────────────────────────────────────
    uint32_t pdCount = 0;
    vkEnumeratePhysicalDevices(instance, &pdCount, nullptr);
    if (pdCount == 0) { std::cerr << "No Vulkan devices found\n"; return 1; }

    std::vector<VkPhysicalDevice> pds(pdCount);
    vkEnumeratePhysicalDevices(instance, &pdCount, pds.data());
    VkPhysicalDevice pd = pds[0];

    VkPhysicalDeviceProperties baseProps{};
    vkGetPhysicalDeviceProperties(pd, &baseProps);
    std::cout << "GPU: " << baseProps.deviceName << "\n";

    if (checkRTExtensions(pd)) { std::cerr << "FAIL: RT extensions missing\n"; return 1; }
    std::cout << "[PASS] RT extensions advertised\n";

    // ── 3. Feature flags ─────────────────────────────────────────────────────
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipFeats{};
    rtPipFeats.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeat{};
    asFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    asFeat.pNext = &rtPipFeats;

    VkPhysicalDeviceFeatures2 feats2{};
    feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feats2.pNext = &asFeat;
    vkGetPhysicalDeviceFeatures2(pd, &feats2);

    if (!asFeat.accelerationStructure || !rtPipFeats.rayTracingPipeline) {
        std::cerr << "FAIL: RT feature flags not set\n"; return 1;
    }
    std::cout << "[PASS] RT feature flags\n";

    // ── 4. Logical device with all RT extensions ──────────────────────────────
    const char* devExts[] = {
        "VK_KHR_acceleration_structure",
        "VK_KHR_ray_tracing_pipeline",
        "VK_KHR_ray_query",
        "VK_KHR_deferred_host_operations",
        "VK_KHR_pipeline_library",
    };

    float qPrio = 1.0f;
    VkDeviceQueueCreateInfo qCI{};
    qCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qCI.queueFamilyIndex = 0;
    qCI.queueCount       = 1;
    qCI.pQueuePriorities = &qPrio;

    VkDeviceCreateInfo devCI{};
    devCI.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCI.queueCreateInfoCount    = 1;
    devCI.pQueueCreateInfos       = &qCI;
    devCI.enabledExtensionCount   = (uint32_t)(sizeof(devExts) / sizeof(devExts[0]));
    devCI.ppEnabledExtensionNames = devExts;

    VkDevice device = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(pd, &devCI, nullptr, &device));
    std::cout << "[PASS] vkCreateDevice (RT extensions stripped cleanly)\n";

    // ── 5. Acceleration structure ─────────────────────────────────────────────
    LOAD_DEV(vkCreateAccelerationStructureKHR);
    LOAD_DEV(vkDestroyAccelerationStructureKHR);
    LOAD_DEV(vkGetAccelerationStructureDeviceAddressKHR);
    LOAD_DEV(vkGetAccelerationStructureBuildSizesKHR);

    // Query build sizes first
    VkAccelerationStructureGeometryKHR geom{};
    geom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &geom;

    uint32_t primCount = 0;
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    fpvkGetAccelerationStructureBuildSizesKHR(
        device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo, &primCount, &sizeInfo);
    std::cout << "  AS size=" << sizeInfo.accelerationStructureSize
              << " scratch=" << sizeInfo.buildScratchSize << "\n";

    VkAccelerationStructureCreateInfoKHR asCI{};
    asCI.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCI.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    asCI.size   = sizeInfo.accelerationStructureSize;

    VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
    VK_CHECK(fpvkCreateAccelerationStructureKHR(device, &asCI, nullptr, &blas));
    assert(blas != VK_NULL_HANDLE);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = blas;
    VkDeviceAddress blasAddr = fpvkGetAccelerationStructureDeviceAddressKHR(device, &addrInfo);
    assert(blasAddr != 0);
    std::cout << "  BLAS device address = 0x" << std::hex << blasAddr << std::dec << "\n";
    std::cout << "[PASS] Acceleration structure create + device address\n";

    fpvkDestroyAccelerationStructureKHR(device, blas, nullptr);

    // ── 6. RT pipeline ───────────────────────────────────────────────────────
    LOAD_DEV(vkCreateRayTracingPipelinesKHR);

    VkRayTracingPipelineCreateInfoKHR rtPipeCI{};
    rtPipeCI.sType      = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtPipeCI.maxPipelineRayRecursionDepth = 1;

    VkPipeline rtPipeline = VK_NULL_HANDLE;
    VK_CHECK(fpvkCreateRayTracingPipelinesKHR(
        device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rtPipeCI, nullptr, &rtPipeline));

    // FIX-4 verification: handle must not be VK_NULL_HANDLE
    if (rtPipeline == VK_NULL_HANDLE) {
        std::cerr << "FAIL: CreateRayTracingPipelinesKHR returned VK_NULL_HANDLE\n";
        return 1;
    }
    std::cout << "[PASS] vkCreateRayTracingPipelinesKHR (non-null handle)\n";

    // ── 7. Deferred operations ───────────────────────────────────────────────
    LOAD_DEV(vkCreateDeferredOperationKHR);
    LOAD_DEV(vkGetDeferredOperationMaxConcurrencyKHR);
    LOAD_DEV(vkGetDeferredOperationResultKHR);
    LOAD_DEV(vkDestroyDeferredOperationKHR);

    VkDeferredOperationKHR defOp = VK_NULL_HANDLE;
    VK_CHECK(fpvkCreateDeferredOperationKHR(device, nullptr, &defOp));
    assert(defOp != VK_NULL_HANDLE);

    uint32_t maxConc = fpvkGetDeferredOperationMaxConcurrencyKHR(device, defOp);
    VkResult defResult = fpvkGetDeferredOperationResultKHR(device, defOp);
    std::cout << "  DeferredOp maxConcurrency=" << maxConc
              << " result=" << defResult << "\n";
    fpvkDestroyDeferredOperationKHR(device, defOp, nullptr);
    std::cout << "[PASS] Deferred host operations\n";

    // ── Cleanup ───────────────────────────────────────────────────────────────
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    std::cout << "\n=== All tests passed ===\n";
    return 0;
}
