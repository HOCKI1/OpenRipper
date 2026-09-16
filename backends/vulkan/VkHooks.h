#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "../../core/VertexTypes.h"
#include "../../core/VertexParser.h"
#include "../../core/ModelFilter.h"
#include "../../core/ObjExporter.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

namespace OpenRipper {

struct PipelineInfo {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
};

struct BoundBufferInfo {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize stride = 0;
};

#pragma pack(push, 1)
struct RSXVertexLayoutEntry {
    uint32_t vertex_base_index;
    uint32_t vertex_index_offset;
    uint32_t draw_id;
    uint32_t xform_constants_offset;
    uint32_t vs_context_offset;
    uint32_t fs_constants_offset;
    uint32_t fs_context_offset;
    uint32_t fs_texture_base_index;
    uint32_t fs_stipple_pattern_offset;
    uint32_t reserved;
    uint32_t attrib_data[32]; // 16 attributes * 2 dwords each
};
#pragma pack(pop)

struct RSXAttributeDesc {
    uint32_t stride = 0;
    uint32_t frequency = 1;
    uint32_t type = 0;
    uint32_t attribute_size = 0;
    uint32_t starting_offset = 0;
    bool swap_bytes = false;
    bool is_volatile = false;
    bool modulo = false;
};

struct CommandBufferState {
    VkPipeline currentPipeline = VK_NULL_HANDLE;
    std::unordered_map<uint32_t, BoundBufferInfo> vertexBuffers; // binding -> buffer info
    BoundBufferInfo indexBuffer;
    VkIndexType indexType = VK_INDEX_TYPE_UINT16;
    uint32_t lastVertexPushConstant = 0;
    bool hasVertexPushConstant = false;
};

struct BufferMemoryInfo {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize memoryOffset = 0;
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    bool isHostVisible = false;
    void* mappedPtr = nullptr;
};

class VulkanRipper {
public:
    static VulkanRipper& Instance() {
        static VulkanRipper instance;
        return instance;
    }

    void SetOutputDir(const std::string& dir) { outputDir = dir; }
    void TriggerCapture() { captureRequested = true; }
    bool IsCaptureRequested() const { return captureRequested; }

    // Interception hooks
    void OnCreateGraphicsPipelines(VkDevice device, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkPipeline* pPipelines);
    void OnDestroyPipeline(VkDevice device, VkPipeline pipeline);

    void OnCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, VkBuffer buffer);
    void OnBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);
    void OnDestroyBuffer(VkDevice device, VkBuffer buffer);
    void OnCreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, VkBufferView view);

    void OnAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, VkDeviceMemory memory);
    void OnFreeMemory(VkDevice device, VkDeviceMemory memory);
    void OnMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, void* ppData);

    void OnCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
    void OnCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets);
    void OnCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes, const VkDeviceSize* pStrides);
    void OnCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
    void OnCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues);

    void OnCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
    void OnCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);

    void OnQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

    void LogDebug(const std::string& msg);

private:
    VulkanRipper();
    ~VulkanRipper();

    std::string outputDir = "C:\\OpenRipperDumps";
    std::atomic<bool> captureRequested{false};
    std::atomic<int> captureFramesRemaining{0};
    uint32_t frameCounter = 0;
    uint32_t currentDrawCallIndex = 0;

    std::mutex stateMutex;
    std::unordered_map<VkPipeline, PipelineInfo> pipelineCache;
    std::unordered_map<VkCommandBuffer, CommandBufferState> cmdBufferStates;
    std::unordered_map<VkBuffer, BufferMemoryInfo> bufferCache;
    std::unordered_map<VkDeviceMemory, bool> memoryHostVisible;
    std::unordered_map<VkDeviceMemory, void*> memoryMappedPointers;

    VkBuffer attribRingBuffer = VK_NULL_HANDLE;
    VkBuffer vertexLayoutBuffer = VK_NULL_HANDLE;
    std::unordered_map<VkBufferView, VkBuffer> bufferViews;

    FilterSettings filterSettings;
};

} // namespace OpenRipper
