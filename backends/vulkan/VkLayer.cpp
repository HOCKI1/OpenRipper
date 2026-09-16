#include "VkHooks.h"
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <cstring>
#include <unordered_map>
#include <mutex>
#include <iostream>

#define VK_LAYER_EXPORT extern "C" __declspec(dllexport)

static std::mutex g_DispatchMutex;
static PFN_vkGetInstanceProcAddr g_NextGetInstanceProcAddr = nullptr;
static PFN_vkGetDeviceProcAddr g_NextGetDeviceProcAddr = nullptr;

struct DeviceDispatchTable {
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = nullptr;
    PFN_vkDestroyPipeline DestroyPipeline = nullptr;
    PFN_vkCreateBuffer CreateBuffer = nullptr;
    PFN_vkBindBufferMemory BindBufferMemory = nullptr;
    PFN_vkBindBufferMemory2 BindBufferMemory2 = nullptr;
    PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
    PFN_vkGetDeviceQueue2 GetDeviceQueue2 = nullptr;
    PFN_vkDestroyBuffer DestroyBuffer = nullptr;
    PFN_vkMapMemory MapMemory = nullptr;
    PFN_vkCreateBufferView CreateBufferView = nullptr;
    PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
    PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers = nullptr;
    PFN_vkCmdBindVertexBuffers2 CmdBindVertexBuffers2 = nullptr;
    PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer = nullptr;
    PFN_vkCmdPushConstants CmdPushConstants = nullptr;
    PFN_vkCmdDrawIndexed CmdDrawIndexed = nullptr;
    PFN_vkCmdDraw CmdDraw = nullptr;
    PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
};

static std::unordered_map<void*, DeviceDispatchTable> g_DeviceDispatchMap;

static inline void* GetKey(void* obj) {
    return *reinterpret_cast<void**>(obj);
}

// Forward declarations
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL OpenRipper_GetDeviceProcAddr(VkDevice device, const char* pName);
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL OpenRipper_GetInstanceProcAddr(VkInstance instance, const char* pName);

// Layer Properties
static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkEnumerateInstanceLayerProperties(uint32_t *pCount, VkLayerProperties *pProperties) {
    if (!pCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (pProperties == nullptr) {
        *pCount = 1;
        return VK_SUCCESS;
    }
    if (*pCount < 1) return VK_INCOMPLETE;

    std::strncpy(pProperties[0].layerName, "VK_LAYER_OPENRIPPER_capture", VK_MAX_EXTENSION_NAME_SIZE);
    pProperties[0].specVersion = VK_API_VERSION_1_3;
    pProperties[0].implementationVersion = 1;
    std::strncpy(pProperties[0].description, "OpenRipper Vulkan 3D Model Ripper", VK_MAX_DESCRIPTION_SIZE);
    *pCount = 1;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pCount, VkExtensionProperties *pProperties) {
    if (!pCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (pLayerName && std::strcmp(pLayerName, "VK_LAYER_OPENRIPPER_capture") == 0) {
        *pCount = 0;
        return VK_SUCCESS;
    }
    return VK_ERROR_LAYER_NOT_PRESENT;
}

// Intercepted vkCreateInstance
static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    VkLayerInstanceCreateInfo* chain_info = const_cast<VkLayerInstanceCreateInfo*>(reinterpret_cast<const VkLayerInstanceCreateInfo*>(pCreateInfo->pNext));
    while (chain_info && (chain_info->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || chain_info->function != VK_LAYER_LINK_INFO)) {
        chain_info = const_cast<VkLayerInstanceCreateInfo*>(reinterpret_cast<const VkLayerInstanceCreateInfo*>(chain_info->pNext));
    }

    if (!chain_info || !chain_info->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    PFN_vkCreateInstance fpCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(fpGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!fpCreateInstance) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult res = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (res == VK_SUCCESS) {
        std::lock_guard<std::mutex> lock(g_DispatchMutex);
        g_NextGetInstanceProcAddr = fpGetInstanceProcAddr;
    }
    return res;
}

// Intercepted vkCreateDevice
static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    VkLayerDeviceCreateInfo* chain_info = const_cast<VkLayerDeviceCreateInfo*>(reinterpret_cast<const VkLayerDeviceCreateInfo*>(pCreateInfo->pNext));
    while (chain_info && (chain_info->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || chain_info->function != VK_LAYER_LINK_INFO)) {
        chain_info = const_cast<VkLayerDeviceCreateInfo*>(reinterpret_cast<const VkLayerDeviceCreateInfo*>(chain_info->pNext));
    }

    if (!chain_info || !chain_info->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    PFN_vkCreateDevice fpCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(fpGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice"));
    if (!fpCreateDevice) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult res = fpCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (res == VK_SUCCESS && pDevice && *pDevice) {
        DeviceDispatchTable dt;
        dt.GetDeviceProcAddr = fpGetDeviceProcAddr;
        dt.CreateGraphicsPipelines = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(fpGetDeviceProcAddr(*pDevice, "vkCreateGraphicsPipelines"));
        dt.DestroyPipeline = reinterpret_cast<PFN_vkDestroyPipeline>(fpGetDeviceProcAddr(*pDevice, "vkDestroyPipeline"));
        dt.CreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(fpGetDeviceProcAddr(*pDevice, "vkCreateBuffer"));
        dt.BindBufferMemory = reinterpret_cast<PFN_vkBindBufferMemory>(fpGetDeviceProcAddr(*pDevice, "vkBindBufferMemory"));
        dt.BindBufferMemory2 = reinterpret_cast<PFN_vkBindBufferMemory2>(fpGetDeviceProcAddr(*pDevice, "vkBindBufferMemory2"));
        if (!dt.BindBufferMemory2) dt.BindBufferMemory2 = reinterpret_cast<PFN_vkBindBufferMemory2>(fpGetDeviceProcAddr(*pDevice, "vkBindBufferMemory2KHR"));
        dt.GetDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(fpGetDeviceProcAddr(*pDevice, "vkGetDeviceQueue"));
        dt.GetDeviceQueue2 = reinterpret_cast<PFN_vkGetDeviceQueue2>(fpGetDeviceProcAddr(*pDevice, "vkGetDeviceQueue2"));
        dt.DestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(fpGetDeviceProcAddr(*pDevice, "vkDestroyBuffer"));
        dt.MapMemory = reinterpret_cast<PFN_vkMapMemory>(fpGetDeviceProcAddr(*pDevice, "vkMapMemory"));
        dt.CreateBufferView = reinterpret_cast<PFN_vkCreateBufferView>(fpGetDeviceProcAddr(*pDevice, "vkCreateBufferView"));
        dt.CmdBindPipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(fpGetDeviceProcAddr(*pDevice, "vkCmdBindPipeline"));
        dt.CmdBindVertexBuffers = reinterpret_cast<PFN_vkCmdBindVertexBuffers>(fpGetDeviceProcAddr(*pDevice, "vkCmdBindVertexBuffers"));
        dt.CmdBindVertexBuffers2 = reinterpret_cast<PFN_vkCmdBindVertexBuffers2>(fpGetDeviceProcAddr(*pDevice, "vkCmdBindVertexBuffers2"));
        if (!dt.CmdBindVertexBuffers2) dt.CmdBindVertexBuffers2 = reinterpret_cast<PFN_vkCmdBindVertexBuffers2>(fpGetDeviceProcAddr(*pDevice, "vkCmdBindVertexBuffers2EXT"));
        dt.CmdBindIndexBuffer = reinterpret_cast<PFN_vkCmdBindIndexBuffer>(fpGetDeviceProcAddr(*pDevice, "vkCmdBindIndexBuffer"));
        dt.CmdPushConstants = reinterpret_cast<PFN_vkCmdPushConstants>(fpGetDeviceProcAddr(*pDevice, "vkCmdPushConstants"));
        dt.CmdDrawIndexed = reinterpret_cast<PFN_vkCmdDrawIndexed>(fpGetDeviceProcAddr(*pDevice, "vkCmdDrawIndexed"));
        dt.CmdDraw = reinterpret_cast<PFN_vkCmdDraw>(fpGetDeviceProcAddr(*pDevice, "vkCmdDraw"));
        dt.QueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(fpGetDeviceProcAddr(*pDevice, "vkQueuePresentKHR"));

        std::lock_guard<std::mutex> lock(g_DispatchMutex);
        g_NextGetDeviceProcAddr = fpGetDeviceProcAddr;
        g_DeviceDispatchMap[GetKey(*pDevice)] = dt;
    }
    return res;
}

static DeviceDispatchTable* GetDT(void* handle) {
    std::lock_guard<std::mutex> lock(g_DispatchMutex);
    auto it = g_DeviceDispatchMap.find(GetKey(handle));
    return (it != g_DeviceDispatchMap.end()) ? &it->second : nullptr;
}

// Hook implementations
static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) {
    auto* dt = GetDT(device);
    VkResult res = dt && dt->CreateGraphicsPipelines ? dt->CreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines) : VK_ERROR_INITIALIZATION_FAILED;
    if (res == VK_SUCCESS) {
        OpenRipper::VulkanRipper::Instance().OnCreateGraphicsPipelines(device, createInfoCount, pCreateInfos, pPipelines);
    }
    return res;
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) {
    OpenRipper::VulkanRipper::Instance().OnDestroyPipeline(device, pipeline);
    auto* dt = GetDT(device);
    if (dt && dt->DestroyPipeline) dt->DestroyPipeline(device, pipeline, pAllocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) {
    auto* dt = GetDT(device);
    VkResult res = dt && dt->CreateBuffer ? dt->CreateBuffer(device, pCreateInfo, pAllocator, pBuffer) : VK_ERROR_INITIALIZATION_FAILED;
    if (res == VK_SUCCESS) {
        OpenRipper::VulkanRipper::Instance().OnCreateBuffer(device, pCreateInfo, *pBuffer);
    }
    return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    auto* dt = GetDT(device);
    VkResult res = dt && dt->BindBufferMemory ? dt->BindBufferMemory(device, buffer, memory, memoryOffset) : VK_ERROR_INITIALIZATION_FAILED;
    if (res == VK_SUCCESS) {
        OpenRipper::VulkanRipper::Instance().OnBindBufferMemory(device, buffer, memory, memoryOffset);
    }
    return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) {
    auto* dt = GetDT(device);
    VkResult res = dt && dt->BindBufferMemory2 ? dt->BindBufferMemory2(device, bindInfoCount, pBindInfos) : VK_ERROR_INITIALIZATION_FAILED;
    if (res == VK_SUCCESS && pBindInfos) {
        for (uint32_t i = 0; i < bindInfoCount; ++i) {
            OpenRipper::VulkanRipper::Instance().OnBindBufferMemory(device, pBindInfos[i].buffer, pBindInfos[i].memory, pBindInfos[i].memoryOffset);
        }
    }
    return res;
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) {
    auto* dt = GetDT(device);
    if (dt && dt->GetDeviceQueue) {
        dt->GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
        if (pQueue && *pQueue) {
            std::lock_guard<std::mutex> lock(g_DispatchMutex);
            g_DeviceDispatchMap[GetKey(*pQueue)] = *dt;
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) {
    auto* dt = GetDT(device);
    if (dt && dt->GetDeviceQueue2) {
        dt->GetDeviceQueue2(device, pQueueInfo, pQueue);
        if (pQueue && *pQueue) {
            std::lock_guard<std::mutex> lock(g_DispatchMutex);
            g_DeviceDispatchMap[GetKey(*pQueue)] = *dt;
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) {
    OpenRipper::VulkanRipper::Instance().OnDestroyBuffer(device, buffer);
    auto* dt = GetDT(device);
    if (dt && dt->DestroyBuffer) dt->DestroyBuffer(device, buffer, pAllocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) {
    auto* dt = GetDT(device);
    VkResult res = dt && dt->MapMemory ? dt->MapMemory(device, memory, offset, size, flags, ppData) : VK_ERROR_INITIALIZATION_FAILED;
    if (res == VK_SUCCESS && ppData) {
        OpenRipper::VulkanRipper::Instance().OnMapMemory(device, memory, offset, size, *ppData);
    }
    return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkCreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) {
    auto* dt = GetDT(device);
    VkResult res = dt && dt->CreateBufferView ? dt->CreateBufferView(device, pCreateInfo, pAllocator, pView) : VK_ERROR_INITIALIZATION_FAILED;
    if (res == VK_SUCCESS && pCreateInfo && pView) {
        OpenRipper::VulkanRipper::Instance().OnCreateBufferView(device, pCreateInfo, *pView);
    }
    return res;
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    OpenRipper::VulkanRipper::Instance().OnCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
    auto* dt = GetDT(commandBuffer);
    if (dt && dt->CmdBindPipeline) dt->CmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) {
    OpenRipper::VulkanRipper::Instance().OnCmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
    auto* dt = GetDT(commandBuffer);
    if (dt && dt->CmdBindVertexBuffers) dt->CmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes, const VkDeviceSize* pStrides) {
    OpenRipper::VulkanRipper::Instance().OnCmdBindVertexBuffers2(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);
    auto* dt = GetDT(commandBuffer);
    if (dt && dt->CmdBindVertexBuffers2) dt->CmdBindVertexBuffers2(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    OpenRipper::VulkanRipper::Instance().OnCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
    auto* dt = GetDT(commandBuffer);
    if (dt && dt->CmdBindIndexBuffer) dt->CmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) {
    OpenRipper::VulkanRipper::Instance().OnCmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
    auto* dt = GetDT(commandBuffer);
    if (dt && dt->CmdPushConstants) dt->CmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    OpenRipper::VulkanRipper::Instance().OnCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    auto* dt = GetDT(commandBuffer);
    if (dt && dt->CmdDrawIndexed) dt->CmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    OpenRipper::VulkanRipper::Instance().OnCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    auto* dt = GetDT(commandBuffer);
    if (dt && dt->CmdDraw) dt->CmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    OpenRipper::VulkanRipper::Instance().OnQueuePresentKHR(queue, pPresentInfo);
    auto* dt = GetDT(queue);
    return (dt && dt->QueuePresentKHR) ? dt->QueuePresentKHR(queue, pPresentInfo) : VK_SUCCESS;
}

// ProcAddr resolution
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL OpenRipper_GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(OpenRipper_GetDeviceProcAddr);
    if (std::strcmp(pName, "vkCreateGraphicsPipelines") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCreateGraphicsPipelines);
    if (std::strcmp(pName, "vkDestroyPipeline") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkDestroyPipeline);
    if (std::strcmp(pName, "vkCreateBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCreateBuffer);
    if (std::strcmp(pName, "vkBindBufferMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkBindBufferMemory);
    if (std::strcmp(pName, "vkBindBufferMemory2") == 0 || std::strcmp(pName, "vkBindBufferMemory2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkBindBufferMemory2);
    if (std::strcmp(pName, "vkGetDeviceQueue") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkGetDeviceQueue);
    if (std::strcmp(pName, "vkGetDeviceQueue2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkGetDeviceQueue2);
    if (std::strcmp(pName, "vkDestroyBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkDestroyBuffer);
    if (std::strcmp(pName, "vkMapMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkMapMemory);
    if (std::strcmp(pName, "vkCreateBufferView") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCreateBufferView);
    if (std::strcmp(pName, "vkCmdBindPipeline") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdBindPipeline);
    if (std::strcmp(pName, "vkCmdBindVertexBuffers") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdBindVertexBuffers);
    if (std::strcmp(pName, "vkCmdBindVertexBuffers2") == 0 || std::strcmp(pName, "vkCmdBindVertexBuffers2EXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdBindVertexBuffers2);
    if (std::strcmp(pName, "vkCmdBindIndexBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdBindIndexBuffer);
    if (std::strcmp(pName, "vkCmdPushConstants") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdPushConstants);
    if (std::strcmp(pName, "vkCmdDrawIndexed") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdDrawIndexed);
    if (std::strcmp(pName, "vkCmdDraw") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdDraw);
    if (std::strcmp(pName, "vkQueuePresentKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkQueuePresentKHR);

    auto* dt = GetDT(device);
    if (dt && dt->GetDeviceProcAddr) {
        return dt->GetDeviceProcAddr(device, pName);
    }
    if (g_NextGetDeviceProcAddr) {
        return g_NextGetDeviceProcAddr(device, pName);
    }
    return nullptr;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL OpenRipper_GetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(OpenRipper_GetInstanceProcAddr);
    if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkEnumerateInstanceLayerProperties);
    if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkEnumerateInstanceExtensionProperties);
    if (std::strcmp(pName, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCreateInstance);
    if (std::strcmp(pName, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCreateDevice);

    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(OpenRipper_GetDeviceProcAddr);
    if (std::strcmp(pName, "vkCreateGraphicsPipelines") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCreateGraphicsPipelines);
    if (std::strcmp(pName, "vkDestroyPipeline") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkDestroyPipeline);
    if (std::strcmp(pName, "vkCreateBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCreateBuffer);
    if (std::strcmp(pName, "vkBindBufferMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkBindBufferMemory);
    if (std::strcmp(pName, "vkBindBufferMemory2") == 0 || std::strcmp(pName, "vkBindBufferMemory2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkBindBufferMemory2);
    if (std::strcmp(pName, "vkGetDeviceQueue") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkGetDeviceQueue);
    if (std::strcmp(pName, "vkGetDeviceQueue2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkGetDeviceQueue2);
    if (std::strcmp(pName, "vkDestroyBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkDestroyBuffer);
    if (std::strcmp(pName, "vkMapMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkMapMemory);
    if (std::strcmp(pName, "vkCreateBufferView") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCreateBufferView);
    if (std::strcmp(pName, "vkCmdBindPipeline") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdBindPipeline);
    if (std::strcmp(pName, "vkCmdBindVertexBuffers") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdBindVertexBuffers);
    if (std::strcmp(pName, "vkCmdBindVertexBuffers2") == 0 || std::strcmp(pName, "vkCmdBindVertexBuffers2EXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdBindVertexBuffers2);
    if (std::strcmp(pName, "vkCmdBindIndexBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdBindIndexBuffer);
    if (std::strcmp(pName, "vkCmdPushConstants") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdPushConstants);
    if (std::strcmp(pName, "vkCmdDrawIndexed") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdDrawIndexed);
    if (std::strcmp(pName, "vkCmdDraw") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdDraw);
    if (std::strcmp(pName, "vkQueuePresentKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkQueuePresentKHR);

    if (g_NextGetInstanceProcAddr) {
        return g_NextGetInstanceProcAddr(instance, pName);
    }
    return nullptr;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL OpenRipper_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    if (!pVersionStruct || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
        pVersionStruct->pfnGetInstanceProcAddr = OpenRipper_GetInstanceProcAddr;
        pVersionStruct->pfnGetDeviceProcAddr = OpenRipper_GetDeviceProcAddr;
        pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
        return VK_SUCCESS;
    }

    return VK_ERROR_INITIALIZATION_FAILED;
}
