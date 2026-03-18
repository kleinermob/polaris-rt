/*
 * polaris_rt layer.cpp
 * Vulkan layer: emulates RT extensions on Polaris (RX 580/590) GPUs
 *
 * ── BUG FIXES ────────────────────────────────────────────────────────────────
 * FIX-1  DebugLog: hardcoded "C:/projects/..." path replaced with a path
 *         derived from the DLL's own location at runtime.
 * FIX-2  my_CreateDevice: previously captured nextDPA via
 *         nextGPA(VK_NULL_HANDLE, "vkGetDeviceProcAddr") *after* advancing
 *         pLayerInfo.  The correct pattern is to read pfnNextGetDeviceProcAddr
 *         from pLayerInfo *before* advancing it.
 * FIX-3  vkGetInstanceProcAddr: HOOK_INST(DestroyDevice) removed.  DestroyDevice
 *         is a device-level function and must only appear in vkGetDeviceProcAddr.
 * FIX-4  my_CreateRayTracingPipelinesKHR: was returning VK_NULL_HANDLE for every
 *         pipeline handle.  Apps that assert(pipe != VK_NULL_HANDLE) would crash.
 *         Now returns monotonically-increasing fake handles.
 * FIX-5  Added stubs for vkGetAccelerationStructureDeviceAddressKHR and the five
 *         deferred-host-operations entry points the layer advertises but never
 *         implemented (VK_KHR_deferred_host_operations).
 * ─────────────────────────────────────────────────────────────────────────────
 */

#define VK_USE_PLATFORM_WIN32_KHR

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <atomic>

#ifdef _WIN32
#define VK_LAYER_EXPORT __declspec(dllexport)
#include <windows.h>
#endif

// ============================================================================
// Debug log path  (FIX-1)
// ============================================================================
static char g_logPath[MAX_PATH] = {};
static HINSTANCE g_hModule = nullptr;

static void InitLogPath() {
    char dllPath[MAX_PATH] = {};
    if (g_hModule) {
        GetModuleFileNameA(g_hModule, dllPath, MAX_PATH);
        char* slash = strrchr(dllPath, '\\');
        if (slash) *(slash + 1) = '\0';
    } else {
        strcpy(dllPath, "C:\\projects\\polaris_rt\\build\\bin\\Release\\");
    }
    snprintf(g_logPath, MAX_PATH, "%slayer_debug.txt", dllPath);
    OutputDebugStringA("[PolarisRT] Log path: ");
    OutputDebugStringA(g_logPath);
    OutputDebugStringA("\n");
}

static void DebugLog(const char* msg) {
    OutputDebugStringA("[PolarisRT] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
    if (g_logPath[0]) {
        if (FILE* f = fopen(g_logPath, "a")) {
            fprintf(f, "%s\n", msg);
            fclose(f);
        }
    }
}

// ============================================================================
// Fake handle allocator  (FIX-4 support)
// ============================================================================
static std::atomic<uint64_t> g_nextHandle{0x10000ULL};
static uint64_t AllocFakeHandle() {
    return g_nextHandle.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================================
// RT extensions we advertise
// ============================================================================
static const char* k_fakeRTExts[] = {
    "VK_KHR_acceleration_structure",
    "VK_KHR_ray_tracing_pipeline",
    "VK_KHR_ray_query",
    "VK_KHR_deferred_host_operations",
    "VK_KHR_pipeline_library",
    "VK_KHR_ray_tracing_maintenance1",
};
static constexpr uint32_t k_fakeRTExtCount =
    (uint32_t)(sizeof(k_fakeRTExts) / sizeof(k_fakeRTExts[0]));

static bool IsRTExt(const char* name) {
    for (uint32_t i = 0; i < k_fakeRTExtCount; i++)
        if (strcmp(name, k_fakeRTExts[i]) == 0) return true;
    return false;
}

// ============================================================================
// Instance dispatch table
// ============================================================================
static std::mutex g_instMutex;
struct InstanceData {
    VkInstance instance = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr nextGPA = nullptr;
};
static std::vector<InstanceData> g_instances;

static PFN_vkGetInstanceProcAddr GetNextGPA(VkInstance inst = VK_NULL_HANDLE) {
    std::lock_guard<std::mutex> lock(g_instMutex);
    if (inst != VK_NULL_HANDLE) {
        for (auto& d : g_instances)
            if (d.instance == inst) return d.nextGPA;
    }
    if (!g_instances.empty()) return g_instances[0].nextGPA;
    return nullptr;
}

// ============================================================================
// Device dispatch table
// ============================================================================
static std::mutex g_devMutex;
struct DeviceData {
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr nextDPA = nullptr;
};
static std::vector<DeviceData> g_devices;

static PFN_vkGetDeviceProcAddr GetNextDPA(VkDevice dev) {
    std::lock_guard<std::mutex> lock(g_devMutex);
    for (auto& d : g_devices)
        if (d.device == dev) return d.nextDPA;
    return nullptr;
}

// ============================================================================
// Forward declarations
// ============================================================================
extern "C" VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*);
extern "C" VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char*);

// ============================================================================
// Instance functions
// ============================================================================

static VkResult my_CreateInstance_impl(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    DebugLog("my_CreateInstance entered");
    {
        char buf[80];
        snprintf(buf, sizeof(buf), "  pCreateInfo=%p pNext=%p",
            (void*)pCreateInfo, pCreateInfo ? (void*)pCreateInfo->pNext : nullptr);
        DebugLog(buf);
    }

    PFN_vkGetInstanceProcAddr nextGPA = nullptr;
    const VkBaseInStructure* chain =
        pCreateInfo ? (const VkBaseInStructure*)pCreateInfo->pNext : nullptr;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO) {
            VkLayerInstanceCreateInfo* info = (VkLayerInstanceCreateInfo*)chain;
            if (info->function == VK_LAYER_LINK_INFO && info->u.pLayerInfo) {
                nextGPA = info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
                info->u.pLayerInfo = info->u.pLayerInfo->pNext;
                DebugLog("  found nextGPA in pNext chain");
                break;
            }
        }
        chain = chain->pNext;
    }

    if (!nextGPA) {
        DebugLog("ERROR: nextGPA is null in CreateInstance!");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto fpCreateInstance = (PFN_vkCreateInstance)nextGPA(VK_NULL_HANDLE, "vkCreateInstance");
    if (!fpCreateInstance) {
        DebugLog("ERROR: could not resolve vkCreateInstance from next layer");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "next CreateInstance returned: %d", result);
        DebugLog(buf);
    }
    // Debug: log requested layers from the create-info for diagnosing VK_ERROR extensions
    if (pCreateInfo) {
        char buf2[128];
        snprintf(buf2, sizeof(buf2), "  enabledLayerCount=%u", (unsigned)pCreateInfo->enabledLayerCount);
        DebugLog(buf2);
        if (pCreateInfo->ppEnabledLayerNames && pCreateInfo->enabledLayerCount > 0) {
            for (uint32_t i = 0; i < pCreateInfo->enabledLayerCount; ++i) {
                const char* lay = pCreateInfo->ppEnabledLayerNames[i];
                if (lay) {
                    char tmp[256]; snprintf(tmp, sizeof(tmp), "  layer[%u] = %s", (unsigned)i, lay);
                    DebugLog(tmp);
                }
            }
        }
    }
    if (result != VK_SUCCESS) return result;

    {
        std::lock_guard<std::mutex> lock(g_instMutex);
        InstanceData d;
        d.instance = *pInstance;
        d.nextGPA  = nextGPA;
        g_instances.push_back(d);
    }
    DebugLog("Instance stored OK");
    DebugLog("Returning VK_SUCCESS from my_CreateInstance");
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL my_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    __try {
        return my_CreateInstance_impl(pCreateInfo, pAllocator, pInstance);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        char buf[64];
        snprintf(buf, sizeof(buf), "CRASH in my_CreateInstance! code=0x%08lX", (unsigned long)GetExceptionCode());
        DebugLog(buf);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
}

static VKAPI_ATTR void VKAPI_CALL my_DestroyInstance(
    VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    auto nextGPA = GetNextGPA(instance);
    if (nextGPA) {
        auto fn = (PFN_vkDestroyInstance)nextGPA(instance, "vkDestroyInstance");
        if (fn) fn(instance, pAllocator);
    }
    std::lock_guard<std::mutex> lock(g_instMutex);
    for (auto it = g_instances.begin(); it != g_instances.end(); ++it)
        if (it->instance == instance) { g_instances.erase(it); break; }
}

static VKAPI_ATTR VkResult VKAPI_CALL my_EnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount, VkLayerProperties* pProperties)
{
    DebugLog("my_EnumerateInstanceLayerProperties called");
    if (!pProperties) { if (pPropertyCount) *pPropertyCount = 1; return VK_SUCCESS; }
    if (*pPropertyCount > 0) {
        memset(&pProperties[0], 0, sizeof(VkLayerProperties));
        strncpy(pProperties[0].layerName, "VK_LAYER_POLARIS_RT", VK_MAX_EXTENSION_NAME_SIZE - 1);
        pProperties[0].specVersion = VK_MAKE_VERSION(1, 3, 0);
        pProperties[0].implementationVersion = 1;
        strncpy(pProperties[0].description, "Polaris GPU Raytracing Emulation Layer", VK_MAX_DESCRIPTION_SIZE - 1);
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }
    return VK_INCOMPLETE;
}

// ============================================================================
// Phase 2 – Device extension advertisement
// ============================================================================

static VKAPI_ATTR VkResult VKAPI_CALL my_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char*      pLayerName,
    uint32_t*        pPropertyCount,
    VkExtensionProperties* pProperties)
{
    if (pLayerName && strcmp(pLayerName, "VK_LAYER_POLARIS_RT") == 0) {
        if (!pProperties) {
            *pPropertyCount = k_fakeRTExtCount;
            return VK_SUCCESS;
        }
        uint32_t toCopy = (*pPropertyCount < k_fakeRTExtCount) ? *pPropertyCount : k_fakeRTExtCount;
        for (uint32_t i = 0; i < toCopy; i++) {
            memset(&pProperties[i], 0, sizeof(VkExtensionProperties));
            strncpy(pProperties[i].extensionName, k_fakeRTExts[i], VK_MAX_EXTENSION_NAME_SIZE - 1);
            pProperties[i].specVersion = 1;
        }
        *pPropertyCount = toCopy;
        return (toCopy < k_fakeRTExtCount) ? VK_INCOMPLETE : VK_SUCCESS;
    }

    PFN_vkEnumerateDeviceExtensionProperties nextEnum = nullptr;
    auto nextGPA = GetNextGPA();
    if (nextGPA) {
        VkInstance anyInst = VK_NULL_HANDLE;
        {
            std::lock_guard<std::mutex> lock(g_instMutex);
            if (!g_instances.empty()) anyInst = g_instances[0].instance;
        }
        nextEnum = (PFN_vkEnumerateDeviceExtensionProperties)
            nextGPA(anyInst, "vkEnumerateDeviceExtensionProperties");
    }

    if (pLayerName) {
        if (nextEnum) return nextEnum(physicalDevice, pLayerName, pPropertyCount, pProperties);
        if (pPropertyCount) *pPropertyCount = 0;
        return VK_SUCCESS;
    }

    uint32_t realCount = 0;
    std::vector<VkExtensionProperties> realExts;
    if (nextEnum) {
        nextEnum(physicalDevice, nullptr, &realCount, nullptr);
        realExts.resize(realCount);
        nextEnum(physicalDevice, nullptr, &realCount, realExts.data());
    }

    std::vector<VkExtensionProperties> merged = realExts;
    for (uint32_t i = 0; i < k_fakeRTExtCount; i++) {
        bool already = false;
        for (auto& e : realExts)
            if (strcmp(e.extensionName, k_fakeRTExts[i]) == 0) { already = true; break; }
        if (!already) {
            VkExtensionProperties prop{};
            strncpy(prop.extensionName, k_fakeRTExts[i], VK_MAX_EXTENSION_NAME_SIZE - 1);
            prop.specVersion = 1;
            merged.push_back(prop);
        }
    }

    if (!pProperties) {
        *pPropertyCount = (uint32_t)merged.size();
        return VK_SUCCESS;
    }
    uint32_t toCopy = (*pPropertyCount < (uint32_t)merged.size())
        ? *pPropertyCount : (uint32_t)merged.size();
    memcpy(pProperties, merged.data(), toCopy * sizeof(VkExtensionProperties));
    *pPropertyCount = toCopy;
    return (toCopy < (uint32_t)merged.size()) ? VK_INCOMPLETE : VK_SUCCESS;
}

// ============================================================================
// Phase 2 – RT features / properties
// ============================================================================
static VKAPI_ATTR void VKAPI_CALL my_GetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures)
{
    DebugLog("my_GetPhysicalDeviceFeatures2");
    auto nextGPA = GetNextGPA();
    if (nextGPA) {
        VkInstance anyInst = VK_NULL_HANDLE;
        { std::lock_guard<std::mutex> lock(g_instMutex); if (!g_instances.empty()) anyInst = g_instances[0].instance; }
        auto fn = (PFN_vkGetPhysicalDeviceFeatures2)nextGPA(anyInst, "vkGetPhysicalDeviceFeatures2");
        if (fn) fn(physicalDevice, pFeatures);
    }
    for (VkBaseOutStructure* s = (VkBaseOutStructure*)pFeatures->pNext; s; s = s->pNext) {
        if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR)
            ((VkPhysicalDeviceAccelerationStructureFeaturesKHR*)s)->accelerationStructure = VK_TRUE;
        if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR)
            ((VkPhysicalDeviceRayTracingPipelineFeaturesKHR*)s)->rayTracingPipeline = VK_TRUE;
        if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR)
            ((VkPhysicalDeviceRayQueryFeaturesKHR*)s)->rayQuery = VK_TRUE;
    }
}

static VKAPI_ATTR void VKAPI_CALL my_GetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties)
{
    DebugLog("my_GetPhysicalDeviceProperties2");
    auto nextGPA = GetNextGPA();
    if (nextGPA) {
        VkInstance anyInst = VK_NULL_HANDLE;
        { std::lock_guard<std::mutex> lock(g_instMutex); if (!g_instances.empty()) anyInst = g_instances[0].instance; }
        auto fn = (PFN_vkGetPhysicalDeviceProperties2)nextGPA(anyInst, "vkGetPhysicalDeviceProperties2");
        if (fn) fn(physicalDevice, pProperties);
    }
    for (VkBaseOutStructure* s = (VkBaseOutStructure*)pProperties->pNext; s; s = s->pNext) {
        if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR) {
            auto* p = (VkPhysicalDeviceAccelerationStructurePropertiesKHR*)s;
            p->maxGeometryCount  = 1 << 24;
            p->maxInstanceCount  = 1 << 24;
            p->maxPrimitiveCount = 1 << 29;
            p->maxPerStageDescriptorAccelerationStructures = 16;
            p->maxPerStageDescriptorUpdateAfterBindAccelerationStructures = 16;
            p->maxDescriptorSetAccelerationStructures = 64;
            p->maxDescriptorSetUpdateAfterBindAccelerationStructures = 64;
            p->minAccelerationStructureScratchOffsetAlignment = 128;
        }
        if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR) {
            auto* p = (VkPhysicalDeviceRayTracingPipelinePropertiesKHR*)s;
            p->shaderGroupHandleSize      = 32;
            p->maxRayRecursionDepth       = 1;
            p->maxShaderGroupStride       = 4096;
            p->shaderGroupBaseAlignment   = 64;
            p->shaderGroupHandleCaptureReplaySize = 32;
            p->maxRayDispatchInvocationCount = 1u << 30;
            p->shaderGroupHandleAlignment = 32;
            p->maxRayHitAttributeSize     = 32;
        }
    }
}

// ============================================================================
// Phase 3 – Device creation  (FIX-2)
// ============================================================================
static VKAPI_ATTR VkResult VKAPI_CALL my_CreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    DebugLog("my_CreateDevice");

    // FIX-2: capture BOTH nextGPA and nextDPA from the link-info block BEFORE
    // advancing pLayerInfo.  The original code only saved nextGPA and then
    // resolved nextDPA via nextGPA(VK_NULL_HANDLE, ...) after the advance,
    // which resolves the *layer's own* vkGetDeviceProcAddr, not the next one.
    PFN_vkGetInstanceProcAddr nextGPA = nullptr;
    PFN_vkGetDeviceProcAddr   nextDPA = nullptr;

    const VkBaseInStructure* chain = (const VkBaseInStructure*)pCreateInfo->pNext;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
            VkLayerDeviceCreateInfo* info = (VkLayerDeviceCreateInfo*)chain;
            if (info->function == VK_LAYER_LINK_INFO && info->u.pLayerInfo) {
                nextGPA = info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
                nextDPA = info->u.pLayerInfo->pfnNextGetDeviceProcAddr; // FIX-2
                info->u.pLayerInfo = info->u.pLayerInfo->pNext;
                break;
            }
        }
        chain = chain->pNext;
    }
    if (!nextGPA) nextGPA = GetNextGPA();

    // Strip fake RT extensions the app requested
    std::vector<const char*> filteredExts;
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        const char* e = pCreateInfo->ppEnabledExtensionNames[i];
        if (IsRTExt(e)) {
            char buf[128]; snprintf(buf, sizeof(buf), "  Stripped fake ext: %s", e); DebugLog(buf);
        } else {
            filteredExts.push_back(e);
        }
    }

    VkDeviceCreateInfo modCI = *pCreateInfo;
    modCI.enabledExtensionCount   = (uint32_t)filteredExts.size();
    modCI.ppEnabledExtensionNames = filteredExts.empty() ? nullptr : filteredExts.data();

    auto fpCreateDevice = (PFN_vkCreateDevice)nextGPA(VK_NULL_HANDLE, "vkCreateDevice");
    if (!fpCreateDevice) {
        DebugLog("ERROR: could not resolve vkCreateDevice");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = fpCreateDevice(physicalDevice, &modCI, pAllocator, pDevice);
    {
        char buf[64]; snprintf(buf, sizeof(buf), "next CreateDevice returned: %d", result);
        DebugLog(buf);
    }
    if (result != VK_SUCCESS) return result;

    // FIX-2: store the nextDPA captured before we advanced pLayerInfo
    {
        std::lock_guard<std::mutex> lock(g_devMutex);
        DeviceData d; d.device = *pDevice; d.nextDPA = nextDPA;
        g_devices.push_back(d);
    }
    DebugLog("Device stored OK");
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL my_DestroyDevice(
    VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    auto nextDPA = GetNextDPA(device);
    if (nextDPA) {
        auto fn = (PFN_vkDestroyDevice)nextDPA(device, "vkDestroyDevice");
        if (fn) fn(device, pAllocator);
    }
    std::lock_guard<std::mutex> lock(g_devMutex);
    for (auto it = g_devices.begin(); it != g_devices.end(); ++it)
        if (it->device == device) { g_devices.erase(it); break; }
}

// ============================================================================
// Phase 3 – Acceleration structure stubs
// ============================================================================
static VKAPI_ATTR VkResult VKAPI_CALL my_CreateAccelerationStructureKHR(
    VkDevice, const VkAccelerationStructureCreateInfoKHR*,
    const VkAllocationCallbacks*, VkAccelerationStructureKHR* pAS)
{
    DebugLog("my_CreateAccelerationStructureKHR (stub)");
    if (pAS) *pAS = (VkAccelerationStructureKHR)AllocFakeHandle();
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL my_DestroyAccelerationStructureKHR(
    VkDevice, VkAccelerationStructureKHR, const VkAllocationCallbacks*)
{ DebugLog("my_DestroyAccelerationStructureKHR (stub)"); }

static VKAPI_ATTR void VKAPI_CALL my_GetAccelerationStructureBuildSizesKHR(
    VkDevice, VkAccelerationStructureBuildTypeKHR,
    const VkAccelerationStructureBuildGeometryInfoKHR*,
    const uint32_t*, VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo)
{
    DebugLog("my_GetAccelerationStructureBuildSizesKHR (stub)");
    if (pSizeInfo) {
        pSizeInfo->accelerationStructureSize = 65536;
        pSizeInfo->updateScratchSize         = 65536;
        pSizeInfo->buildScratchSize          = 65536;
    }
}

static VKAPI_ATTR void VKAPI_CALL my_CmdBuildAccelerationStructuresKHR(
    VkCommandBuffer, uint32_t,
    const VkAccelerationStructureBuildGeometryInfoKHR*,
    const VkAccelerationStructureBuildRangeInfoKHR* const*)
{ DebugLog("my_CmdBuildAccelerationStructuresKHR (stub)"); }

// FIX-5: New stub – apps embed this GPU address in TLAS instance descriptors.
static VKAPI_ATTR VkDeviceAddress VKAPI_CALL my_GetAccelerationStructureDeviceAddressKHR(
    VkDevice, const VkAccelerationStructureDeviceAddressInfoKHR* pInfo)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
        "my_GetAccelerationStructureDeviceAddressKHR AS=0x%llX (stub)",
        (unsigned long long)(uintptr_t)pInfo->accelerationStructure);
    DebugLog(buf);
    // Return a stable fake address derived from the handle so each AS is unique.
    return (VkDeviceAddress)(uintptr_t)pInfo->accelerationStructure * 256;
}

// ============================================================================
// Phase 3 – RT pipeline stubs
// ============================================================================
static VKAPI_ATTR void VKAPI_CALL my_CmdTraceRaysKHR(
    VkCommandBuffer, const VkStridedDeviceAddressRegionKHR*,
    const VkStridedDeviceAddressRegionKHR*, const VkStridedDeviceAddressRegionKHR*,
    const VkStridedDeviceAddressRegionKHR*, uint32_t w, uint32_t h, uint32_t d)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "my_CmdTraceRaysKHR %ux%ux%u (compute stub)", w, h, d);
    DebugLog(buf);
    // TODO Phase 5: dispatch compute emulation pipeline here
}

// FIX-4: Return non-null fake pipeline handles.
static VKAPI_ATTR VkResult VKAPI_CALL my_CreateRayTracingPipelinesKHR(
    VkDevice, VkDeferredOperationKHR, VkPipelineCache,
    uint32_t count, const VkRayTracingPipelineCreateInfoKHR*,
    const VkAllocationCallbacks*, VkPipeline* pPipelines)
{
    DebugLog("my_CreateRayTracingPipelinesKHR (stub)");
    for (uint32_t i = 0; i < count; i++)
        pPipelines[i] = (VkPipeline)AllocFakeHandle(); // FIX-4: was VK_NULL_HANDLE
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL my_GetRayTracingShaderGroupHandlesKHR(
    VkDevice, VkPipeline, uint32_t, uint32_t, size_t dataSize, void* pData)
{
    DebugLog("my_GetRayTracingShaderGroupHandlesKHR (stub)");
    if (pData) memset(pData, 0, dataSize);
    return VK_SUCCESS;
}

// ============================================================================
// FIX-5: VK_KHR_deferred_host_operations stubs
// ============================================================================
static VKAPI_ATTR VkResult VKAPI_CALL my_CreateDeferredOperationKHR(
    VkDevice, const VkAllocationCallbacks*, VkDeferredOperationKHR* pOp)
{
    DebugLog("my_CreateDeferredOperationKHR (stub)");
    if (pOp) *pOp = (VkDeferredOperationKHR)AllocFakeHandle();
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL my_DestroyDeferredOperationKHR(
    VkDevice, VkDeferredOperationKHR, const VkAllocationCallbacks*)
{ DebugLog("my_DestroyDeferredOperationKHR (stub)"); }

static VKAPI_ATTR uint32_t VKAPI_CALL my_GetDeferredOperationMaxConcurrencyKHR(
    VkDevice, VkDeferredOperationKHR)
{ DebugLog("my_GetDeferredOperationMaxConcurrencyKHR -> 1"); return 1; }

static VKAPI_ATTR VkResult VKAPI_CALL my_GetDeferredOperationResultKHR(
    VkDevice, VkDeferredOperationKHR)
{ DebugLog("my_GetDeferredOperationResultKHR -> VK_SUCCESS"); return VK_SUCCESS; }

static VKAPI_ATTR VkResult VKAPI_CALL my_DeferredOperationJoinKHR(
    VkDevice, VkDeferredOperationKHR)
{ DebugLog("my_DeferredOperationJoinKHR -> VK_SUCCESS"); return VK_SUCCESS; }

// ============================================================================
// Instance proc addr dispatch
// ============================================================================
extern "C" VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    if (!pName) return nullptr;

    char logBuf[256];
    snprintf(logBuf, sizeof(logBuf), "vkGetInstanceProcAddr(instance=%p, pName=%s)", (void*)instance, pName);
    DebugLog(logBuf);

#define HOOK_INST(fn) if (strcmp(pName, "vk" #fn) == 0) { \
    DebugLog("  Hooking vk" #fn); \
    return (PFN_vkVoidFunction)my_##fn; \
}

    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    HOOK_INST(CreateInstance);
    HOOK_INST(DestroyInstance);
    HOOK_INST(CreateDevice);
    // FIX-3: DestroyDevice intentionally NOT hooked here.
    //   vkDestroyDevice is a device-level function; it belongs only in
    //   vkGetDeviceProcAddr.  The old HOOK_INST(DestroyDevice) here caused the
    //   loader to call our stub for instance-level lookups with a device that
    //   may not be registered in g_devices yet, leading to a null-DPA deref.
    HOOK_INST(EnumerateInstanceLayerProperties);
    HOOK_INST(EnumerateDeviceExtensionProperties);
    HOOK_INST(GetPhysicalDeviceFeatures2);
    HOOK_INST(GetPhysicalDeviceProperties2);
#undef HOOK_INST

    auto nextGPA = GetNextGPA(instance);
    if (nextGPA) return nextGPA(instance, pName);
    return nullptr;
}

// ============================================================================
// Device proc addr dispatch
// ============================================================================
extern "C" VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    if (!pName) return nullptr;

#define HOOK_DEV(fn) if (strcmp(pName, "vk" #fn) == 0) return (PFN_vkVoidFunction)my_##fn
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    HOOK_DEV(DestroyDevice);
    HOOK_DEV(CreateAccelerationStructureKHR);
    HOOK_DEV(DestroyAccelerationStructureKHR);
    HOOK_DEV(GetAccelerationStructureBuildSizesKHR);
    HOOK_DEV(GetAccelerationStructureDeviceAddressKHR);    // FIX-5 new
    HOOK_DEV(CmdBuildAccelerationStructuresKHR);
    HOOK_DEV(CmdTraceRaysKHR);
    HOOK_DEV(CreateRayTracingPipelinesKHR);
    HOOK_DEV(GetRayTracingShaderGroupHandlesKHR);
    HOOK_DEV(CreateDeferredOperationKHR);                  // FIX-5 new
    HOOK_DEV(DestroyDeferredOperationKHR);                 // FIX-5 new
    HOOK_DEV(GetDeferredOperationMaxConcurrencyKHR);       // FIX-5 new
    HOOK_DEV(GetDeferredOperationResultKHR);               // FIX-5 new
    HOOK_DEV(DeferredOperationJoinKHR);                    // FIX-5 new
#undef HOOK_DEV

    auto nextDPA = GetNextDPA(device);
    if (nextDPA) return nextDPA(device, pName);
    return nullptr;
}

// ============================================================================
// Loader negotiation
// ============================================================================
extern "C" VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pStruct)
{
    DebugLog("vkNegotiateLoaderLayerInterfaceVersion");
    if (!pStruct) return VK_ERROR_INITIALIZATION_FAILED;
    pStruct->loaderLayerInterfaceVersion = 2;
    pStruct->pfnGetInstanceProcAddr       = vkGetInstanceProcAddr;
    pStruct->pfnGetDeviceProcAddr         = vkGetDeviceProcAddr;
    pStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    DebugLog("Negotiate complete");
    return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t* pCount, VkLayerProperties* pProps)
{
    return my_EnumerateInstanceLayerProperties(pCount, pProps);
}

#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hInst;
        InitLogPath(); // FIX-1: derive log path from DLL location
        DebugLog("DLL loaded");
    }
    if (reason == DLL_PROCESS_DETACH) DebugLog("DLL unloaded");
    return TRUE;
}
#endif
