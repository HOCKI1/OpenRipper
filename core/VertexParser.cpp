#include "VertexParser.h"
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <algorithm>

namespace OpenRipper {

float VertexParser::HalfToFloat(uint16_t half) {
    uint32_t sign = (half >> 15) & 0x00000001;
    uint32_t exp  = (half >> 10) & 0x0000001F;
    uint32_t mant = half & 0x000003FF;

    if (exp == 0) {
        if (mant == 0) {
            uint32_t result = sign << 31;
            float f;
            std::memcpy(&f, &result, sizeof(f));
            return f;
        } else {
            while (!(mant & 0x00000400)) {
                mant <<= 1;
                exp -= 1;
            }
            exp++;
            mant &= ~0x00000400;
        }
    } else if (exp == 31) {
        exp = 255;
    } else {
        exp = exp + (127 - 15);
    }

    uint32_t result = (sign << 31) | (exp << 23) | (mant << 13);
    float f;
    std::memcpy(&f, &result, sizeof(f));
    return f;
}

Vector3 VertexParser::ReadPosition(const uint8_t* ptr, VkFormat format) {
    Vector3 pos{0.0f, 0.0f, 0.0f};
    if (!ptr) return pos;

    switch (format) {
        case VK_FORMAT_R32G32B32_SFLOAT:
        case VK_FORMAT_R32G32B32A32_SFLOAT: {
            const float* fptr = reinterpret_cast<const float*>(ptr);
            pos.x = fptr[0];
            pos.y = fptr[1];
            pos.z = fptr[2];
            break;
        }
        case VK_FORMAT_R16G16B16A16_SFLOAT: {
            const uint16_t* hptr = reinterpret_cast<const uint16_t*>(ptr);
            pos.x = HalfToFloat(hptr[0]);
            pos.y = HalfToFloat(hptr[1]);
            pos.z = HalfToFloat(hptr[2]);
            break;
        }
        case VK_FORMAT_R16G16B16A16_SNORM: {
            const int16_t* sptr = reinterpret_cast<const int16_t*>(ptr);
            pos.x = sptr[0] / 32767.0f;
            pos.y = sptr[1] / 32767.0f;
            pos.z = sptr[2] / 32767.0f;
            break;
        }
        default:
            break;
    }

    if (std::isnan(pos.x) || std::isinf(pos.x)) pos.x = 0.0f;
    if (std::isnan(pos.y) || std::isinf(pos.y)) pos.y = 0.0f;
    if (std::isnan(pos.z) || std::isinf(pos.z)) pos.z = 0.0f;

    return pos;
}

Vector2 VertexParser::ReadUV(const uint8_t* ptr, VkFormat format) {
    Vector2 uv{0.0f, 0.0f};
    if (!ptr) return uv;

    switch (format) {
        case VK_FORMAT_R32G32_SFLOAT: {
            const float* fptr = reinterpret_cast<const float*>(ptr);
            uv.u = fptr[0];
            uv.v = fptr[1];
            break;
        }
        case VK_FORMAT_R16G16_SFLOAT: {
            const uint16_t* hptr = reinterpret_cast<const uint16_t*>(ptr);
            uv.u = HalfToFloat(hptr[0]);
            uv.v = HalfToFloat(hptr[1]);
            break;
        }
        case VK_FORMAT_R16G16_UNORM: {
            const uint16_t* uptr = reinterpret_cast<const uint16_t*>(ptr);
            uv.u = uptr[0] / 65535.0f;
            uv.v = uptr[1] / 65535.0f;
            break;
        }
        case VK_FORMAT_R16G16_SNORM: {
            const int16_t* sptr = reinterpret_cast<const int16_t*>(ptr);
            uv.u = sptr[0] / 32767.0f;
            uv.v = sptr[1] / 32767.0f;
            break;
        }
        default:
            break;
    }
    return uv;
}

Vector3 VertexParser::ReadNormal(const uint8_t* ptr, VkFormat format) {
    Vector3 norm{0.0f, 1.0f, 0.0f};
    if (!ptr) return norm;

    switch (format) {
        case VK_FORMAT_R32G32B32_SFLOAT:
        case VK_FORMAT_R32G32B32A32_SFLOAT: {
            const float* fptr = reinterpret_cast<const float*>(ptr);
            norm.x = fptr[0];
            norm.y = fptr[1];
            norm.z = fptr[2];
            break;
        }
        case VK_FORMAT_R8G8B8A8_SNORM: {
            const int8_t* bptr = reinterpret_cast<const int8_t*>(ptr);
            norm.x = bptr[0] / 127.0f;
            norm.y = bptr[1] / 127.0f;
            norm.z = bptr[2] / 127.0f;
            break;
        }
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: {
            uint32_t val = *reinterpret_cast<const uint32_t*>(ptr);
            int32_t r = (val & 0x3FF);
            int32_t g = ((val >> 10) & 0x3FF);
            int32_t b = ((val >> 20) & 0x3FF);
            norm.x = (r / 511.5f) - 1.0f;
            norm.y = (g / 511.5f) - 1.0f;
            norm.z = (b / 511.5f) - 1.0f;
            break;
        }
        default:
            break;
    }
    return norm;
}

ExtractedMesh VertexParser::ParseIndexed(
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
) {
    ExtractedMesh mesh;
    mesh.name = meshName;
    mesh.drawCallId = drawCallId;
    mesh.pipelineId = pipelineId;

    if (!vertexBuffer.data || vertexStride == 0 || !indexBuffer.data || indexCount == 0) {
        return mesh;
    }

    // Find semantic locations: Location 0 is usually Position, Location 1 / 2 are Normal / UV
    const VkVertexInputAttributeDescription* posAttr = nullptr;
    const VkVertexInputAttributeDescription* uvAttr = nullptr;
    const VkVertexInputAttributeDescription* normAttr = nullptr;

    for (const auto& attr : attributes) {
        if (attr.location == 0 && (
            attr.format == VK_FORMAT_R32G32B32_SFLOAT ||
            attr.format == VK_FORMAT_R32G32B32A32_SFLOAT ||
            attr.format == VK_FORMAT_R16G16B16A16_SFLOAT ||
            attr.format == VK_FORMAT_R16G16B16A16_SNORM)) {
            posAttr = &attr;
            break;
        }
    }
    if (!posAttr) {
        for (const auto& attr : attributes) {
            if (attr.format == VK_FORMAT_R32G32B32_SFLOAT ||
                attr.format == VK_FORMAT_R32G32B32A32_SFLOAT ||
                attr.format == VK_FORMAT_R16G16B16A16_SFLOAT ||
                attr.format == VK_FORMAT_R16G16B16A16_SNORM) {
                posAttr = &attr;
                break;
            }
        }
    }
    if (!posAttr && !attributes.empty()) {
        posAttr = &attributes[0];
    }

    if (!posAttr) return mesh;

    for (const auto& attr : attributes) {
        if (&attr == posAttr) continue;
        if (attr.binding != posAttr->binding) continue;

        if (attr.format == VK_FORMAT_R32G32_SFLOAT || attr.format == VK_FORMAT_R16G16_UNORM || attr.format == VK_FORMAT_R16G16_SFLOAT) {
            if (!uvAttr) uvAttr = &attr;
        } else if (attr.format == VK_FORMAT_R32G32B32_SFLOAT || attr.format == VK_FORMAT_R32G32B32A32_SFLOAT ||
                   attr.format == VK_FORMAT_R8G8B8A8_SNORM || attr.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
            if (!normAttr) normAttr = &attr;
        }
    }

    // Read index buffer and track referenced unique vertices
    std::vector<uint32_t> rawIndices;
    rawIndices.reserve(indexCount);

    if (indexType == VK_INDEX_TYPE_UINT16) {
        const uint16_t* idx16 = reinterpret_cast<const uint16_t*>(indexBuffer.data);
        for (uint32_t i = 0; i < indexCount; ++i) {
            uint32_t idx = static_cast<uint32_t>(idx16[firstIndex + i]) + vertexOffset;
            rawIndices.push_back(idx);
        }
    } else if (indexType == VK_INDEX_TYPE_UINT32) {
        const uint32_t* idx32 = reinterpret_cast<const uint32_t*>(indexBuffer.data);
        for (uint32_t i = 0; i < indexCount; ++i) {
            uint32_t idx = idx32[firstIndex + i] + vertexOffset;
            rawIndices.push_back(idx);
        }
    }

    // Remap vertices so that the exported OBJ contains only vertices used by this submesh
    std::unordered_map<uint32_t, uint32_t> indexRemap;
    uint32_t newIndexCounter = 0;

    for (uint32_t origIdx : rawIndices) {
        auto it = indexRemap.find(origIdx);
        if (it == indexRemap.end()) {
            size_t byteOffset = static_cast<size_t>(origIdx) * vertexStride;
            if (byteOffset + vertexStride <= vertexBuffer.size) {
                const uint8_t* vPtr = vertexBuffer.data + byteOffset;

                mesh.positions.push_back(ReadPosition(vPtr + posAttr->offset, posAttr->format));
                if (uvAttr) {
                    mesh.uvs.push_back(ReadUV(vPtr + uvAttr->offset, uvAttr->format));
                }
                if (normAttr) {
                    mesh.normals.push_back(ReadNormal(vPtr + normAttr->offset, normAttr->format));
                }

                indexRemap[origIdx] = newIndexCounter++;
            }
        }
        mesh.indices.push_back(indexRemap[origIdx]);
    }

    return mesh;
}

ExtractedMesh VertexParser::ParseNonIndexed(
    const std::string& meshName,
    uint32_t drawCallId,
    uint32_t pipelineId,
    const RawBufferView& vertexBuffer,
    uint32_t vertexStride,
    const std::vector<VkVertexInputAttributeDescription>& attributes,
    uint32_t firstVertex,
    uint32_t vertexCount
) {
    ExtractedMesh mesh;
    mesh.name = meshName;
    mesh.drawCallId = drawCallId;
    mesh.pipelineId = pipelineId;

    if (!vertexBuffer.data || vertexStride == 0 || vertexCount == 0) {
        return mesh;
    }

    const VkVertexInputAttributeDescription* posAttr = nullptr;
    const VkVertexInputAttributeDescription* uvAttr = nullptr;
    const VkVertexInputAttributeDescription* normAttr = nullptr;

    for (const auto& attr : attributes) {
        if (attr.location == 0 && (
            attr.format == VK_FORMAT_R32G32B32_SFLOAT ||
            attr.format == VK_FORMAT_R32G32B32A32_SFLOAT ||
            attr.format == VK_FORMAT_R16G16B16A16_SFLOAT ||
            attr.format == VK_FORMAT_R16G16B16A16_SNORM)) {
            posAttr = &attr;
            break;
        }
    }
    if (!posAttr) {
        for (const auto& attr : attributes) {
            if (attr.format == VK_FORMAT_R32G32B32_SFLOAT ||
                attr.format == VK_FORMAT_R32G32B32A32_SFLOAT ||
                attr.format == VK_FORMAT_R16G16B16A16_SFLOAT ||
                attr.format == VK_FORMAT_R16G16B16A16_SNORM) {
                posAttr = &attr;
                break;
            }
        }
    }
    if (!posAttr && !attributes.empty()) {
        posAttr = &attributes[0];
    }

    if (!posAttr) return mesh;

    for (const auto& attr : attributes) {
        if (&attr == posAttr) continue;
        if (attr.binding != posAttr->binding) continue;

        if (attr.format == VK_FORMAT_R32G32_SFLOAT || attr.format == VK_FORMAT_R16G16_UNORM || attr.format == VK_FORMAT_R16G16_SFLOAT) {
            if (!uvAttr) uvAttr = &attr;
        } else if (attr.format == VK_FORMAT_R32G32B32_SFLOAT || attr.format == VK_FORMAT_R32G32B32A32_SFLOAT ||
                   attr.format == VK_FORMAT_R8G8B8A8_SNORM || attr.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
            if (!normAttr) normAttr = &attr;
        }
    }

    for (uint32_t i = 0; i < vertexCount; ++i) {
        size_t byteOffset = static_cast<size_t>(firstVertex + i) * vertexStride;
        if (byteOffset + vertexStride <= vertexBuffer.size) {
            const uint8_t* vPtr = vertexBuffer.data + byteOffset;
            mesh.positions.push_back(ReadPosition(vPtr + posAttr->offset, posAttr->format));
            if (uvAttr) mesh.uvs.push_back(ReadUV(vPtr + uvAttr->offset, uvAttr->format));
            if (normAttr) mesh.normals.push_back(ReadNormal(vPtr + normAttr->offset, normAttr->format));
        }
    }

    return mesh;
}

} // namespace OpenRipper
