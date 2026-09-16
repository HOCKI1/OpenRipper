#pragma once
#include "VertexTypes.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace OpenRipper {

struct RawBufferView {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

class VertexParser {
public:
    static ExtractedMesh ParseIndexed(
        const std::string& meshName,
        uint32_t drawCallId,
        uint32_t pipelineId,
        const RawBufferView& vertexBuffer,
        uint32_t vertexStride,
        const std::vector<VkVertexInputAttributeDescription>& attributes,
        const RawBufferView& indexBuffer,
        VkIndexType indexType,
        uint32_t firstIndex,
        uint32_t indexCount,
        int32_t vertexOffset
    );

    static ExtractedMesh ParseNonIndexed(
        const std::string& meshName,
        uint32_t drawCallId,
        uint32_t pipelineId,
        const RawBufferView& vertexBuffer,
        uint32_t vertexStride,
        const std::vector<VkVertexInputAttributeDescription>& attributes,
        uint32_t firstVertex,
        uint32_t vertexCount
    );

    static float HalfToFloat(uint16_t half);

private:
    static Vector3 ReadPosition(const uint8_t* ptr, VkFormat format);
    static Vector2 ReadUV(const uint8_t* ptr, VkFormat format);
    static Vector3 ReadNormal(const uint8_t* ptr, VkFormat format);
};

} // namespace OpenRipper
