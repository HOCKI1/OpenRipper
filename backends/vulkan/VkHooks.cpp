#include "VkHooks.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>

namespace OpenRipper {

static std::mutex g_LogMutex;

void VulkanRipper::LogDebug(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    try {
        std::filesystem::create_directories(outputDir);
        std::ofstream logFile(outputDir + "\\debug.log", std::ios::out | std::ios::app);
        if (logFile.is_open()) {
            logFile << msg << "\n";
            logFile.flush();
        }
    } catch (...) {}
    OutputDebugStringA(("[OpenRipper] " + msg + "\n").c_str());
}

VulkanRipper::VulkanRipper() {
    // Default filter settings
    filterSettings.minVertexCount = 8;
    filterSettings.minIndexCount = 12;
    filterSettings.filterFlat2DQuads = true;
    filterSettings.filterScreenSpaceUI = true;
    filterSettings.minDepthSpan = 0.001f;
    LogDebug("VulkanRipper Core Initialized.");
}

VulkanRipper::~VulkanRipper() {
}

void VulkanRipper::OnCreateGraphicsPipelines(VkDevice device, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkPipeline* pPipelines) {
    std::lock_guard<std::mutex> lock(stateMutex);
    for (uint32_t i = 0; i < createInfoCount; ++i) {
        const auto& ci = pCreateInfos[i];
        if (ci.pVertexInputState) {
            PipelineInfo info;
            const auto* vi = ci.pVertexInputState;
            if (vi->pVertexBindingDescriptions && vi->vertexBindingDescriptionCount > 0) {
                info.bindings.assign(vi->pVertexBindingDescriptions, vi->pVertexBindingDescriptions + vi->vertexBindingDescriptionCount);
            }
            if (vi->pVertexAttributeDescriptions && vi->vertexAttributeDescriptionCount > 0) {
                info.attributes.assign(vi->pVertexAttributeDescriptions, vi->pVertexAttributeDescriptions + vi->vertexAttributeDescriptionCount);
            }
            pipelineCache[pPipelines[i]] = info;
        }
    }
}

void VulkanRipper::OnDestroyPipeline(VkDevice device, VkPipeline pipeline) {
    std::lock_guard<std::mutex> lock(stateMutex);
    pipelineCache.erase(pipeline);
}

void VulkanRipper::OnCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, VkBuffer buffer) {
    std::lock_guard<std::mutex> lock(stateMutex);
    BufferMemoryInfo info;
    info.size = pCreateInfo->size;
    info.usage = pCreateInfo->usage;
    bufferCache[buffer] = info;

    if (pCreateInfo->usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) {
        attribRingBuffer = buffer;
        std::ostringstream ss;
        ss << "[OpenRipper] Detected RSX attribRingBuffer: " << buffer << " (Size: " << pCreateInfo->size << ")";
        LogDebug(ss.str());
    }
    if (pCreateInfo->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        if (pCreateInfo->size >= 1024 * 1024) {
            vertexLayoutBuffer = buffer;
            std::ostringstream ss;
            ss << "[OpenRipper] Detected RSX candidate vertexLayoutBuffer: " << buffer << " (Size: " << pCreateInfo->size << ")";
            LogDebug(ss.str());
        }
    }
}

void VulkanRipper::OnCreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, VkBufferView view) {
    std::lock_guard<std::mutex> lock(stateMutex);
    bufferViews[view] = pCreateInfo->buffer;
    if (pCreateInfo->format == VK_FORMAT_R8_UINT) {
        attribRingBuffer = pCreateInfo->buffer;
        std::ostringstream ss;
        ss << "[OpenRipper] Confirmed attribRingBuffer via BufferView: " << pCreateInfo->buffer
           << " offset=" << pCreateInfo->offset << " range=" << pCreateInfo->range;
        LogDebug(ss.str());
    }
}

void VulkanRipper::OnBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    std::lock_guard<std::mutex> lock(stateMutex);
    auto it = bufferCache.find(buffer);
    if (it != bufferCache.end()) {
        it->second.memory = memory;
        it->second.memoryOffset = memoryOffset;
        it->second.isHostVisible = memoryHostVisible[memory];
        it->second.mappedPtr = memoryMappedPointers[memory];
    }
}

void VulkanRipper::OnDestroyBuffer(VkDevice device, VkBuffer buffer) {
    std::lock_guard<std::mutex> lock(stateMutex);
    bufferCache.erase(buffer);
}

void VulkanRipper::OnAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, VkDeviceMemory memory) {
    std::lock_guard<std::mutex> lock(stateMutex);
    // Track memory
    memoryHostVisible[memory] = false;
}

void VulkanRipper::OnFreeMemory(VkDevice device, VkDeviceMemory memory) {
    std::lock_guard<std::mutex> lock(stateMutex);
    memoryHostVisible.erase(memory);
    memoryMappedPointers.erase(memory);
}

void VulkanRipper::OnMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, void* ppData) {
    std::lock_guard<std::mutex> lock(stateMutex);
    memoryHostVisible[memory] = true;
    uint8_t* basePtr = static_cast<uint8_t*>(ppData) - offset;
    memoryMappedPointers[memory] = basePtr;

    // Update existing bound buffers pointing to this memory
    for (auto& pair : bufferCache) {
        if (pair.second.memory == memory) {
            pair.second.isHostVisible = true;
            pair.second.mappedPtr = basePtr;
        }
    }
}

void VulkanRipper::OnCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) return;
    std::lock_guard<std::mutex> lock(stateMutex);
    cmdBufferStates[commandBuffer].currentPipeline = pipeline;
}

void VulkanRipper::OnCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) {
    std::lock_guard<std::mutex> lock(stateMutex);
    auto& state = cmdBufferStates[commandBuffer];
    for (uint32_t i = 0; i < bindingCount; ++i) {
        state.vertexBuffers[firstBinding + i] = BoundBufferInfo{ pBuffers[i], pOffsets[i], 0 };
    }
}

void VulkanRipper::OnCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes, const VkDeviceSize* pStrides) {
    std::lock_guard<std::mutex> lock(stateMutex);
    auto& state = cmdBufferStates[commandBuffer];
    for (uint32_t i = 0; i < bindingCount; ++i) {
        VkDeviceSize stride = pStrides ? pStrides[i] : 0;
        state.vertexBuffers[firstBinding + i] = BoundBufferInfo{ pBuffers[i], pOffsets[i], stride };
    }
}

void VulkanRipper::OnCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    std::lock_guard<std::mutex> lock(stateMutex);
    auto& state = cmdBufferStates[commandBuffer];
    state.indexBuffer = BoundBufferInfo{ buffer, offset, 0 };
    state.indexType = indexType;
}

void VulkanRipper::OnCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) {
    if ((stageFlags & VK_SHADER_STAGE_VERTEX_BIT) && offset == 0 && size >= 4 && pValues) {
        std::lock_guard<std::mutex> lock(stateMutex);
        cmdBufferStates[commandBuffer].lastVertexPushConstant = *reinterpret_cast<const uint32_t*>(pValues);
        cmdBufferStates[commandBuffer].hasVertexPushConstant = true;
    }
}

#if defined(_MSC_VER)
    #include <stdlib.h>
    #define BSWAP32(x) _byteswap_ulong(x)
    #define BSWAP16(x) _byteswap_ushort(x)
#else
    #define BSWAP32(x) __builtin_bswap32(x)
    #define BSWAP16(x) __builtin_bswap16(x)
#endif

static RSXAttributeDesc ParseRSXAttr(uint32_t attribX, uint32_t attribY) {
    RSXAttributeDesc d;
    d.stride = attribX & 0xFF;
    d.frequency = (attribX >> 8) & 0xFFFF;
    d.type = (attribX >> 24) & 0x7;
    d.attribute_size = (attribX >> 27) & 0x7;
    d.starting_offset = attribY & 0x1FFFFFFF;
    d.swap_bytes = (attribY & (1u << 29)) != 0;
    d.is_volatile = (attribY & (1u << 30)) != 0;
    d.modulo = (attribY & (1u << 31)) != 0;
    return d;
}

static Vector3 FetchRSXPos(const RSXAttributeDesc& desc, int vertex_id, const uint8_t* pStream, size_t streamSize) {
    Vector3 pos{0.f, 0.f, 0.f};
    if (!pStream || desc.stride == 0 || vertex_id < 0) return pos;
    int64_t byteOffsetSigned = static_cast<int64_t>(desc.starting_offset) + static_cast<int64_t>(vertex_id) * static_cast<int64_t>(desc.stride);
    if (byteOffsetSigned < 0 || static_cast<size_t>(byteOffsetSigned + 12) > streamSize) return pos;
    size_t byteOffset = static_cast<size_t>(byteOffsetSigned);
    const uint8_t* pData = pStream + byteOffset;

    if (desc.type == 2) { // Float32
        for (uint32_t n = 0; n < std::min(desc.attribute_size, 3u); ++n) {
            if (byteOffset + (n + 1) * 4 > streamSize) break;
            uint32_t raw;
            std::memcpy(&raw, pData + n * 4, 4);
            if (desc.swap_bytes) raw = BSWAP32(raw);
            float f;
            std::memcpy(&f, &raw, 4);
            if (n == 0) pos.x = f;
            else if (n == 1) pos.y = f;
            else if (n == 2) pos.z = f;
        }
    } else if (desc.type == 3) { // Float16 (Half)
        for (uint32_t n = 0; n < std::min(desc.attribute_size, 3u); ++n) {
            if (byteOffset + (n + 1) * 2 > streamSize) break;
            uint16_t raw;
            std::memcpy(&raw, pData + n * 2, 2);
            if (desc.swap_bytes) raw = BSWAP16(raw);
            float f = VertexParser::HalfToFloat(raw);
            if (n == 0) pos.x = f;
            else if (n == 1) pos.y = f;
            else if (n == 2) pos.z = f;
        }
    } else if (desc.type == 1) { // SNORM16
        for (uint32_t n = 0; n < std::min(desc.attribute_size, 3u); ++n) {
            if (byteOffset + (n + 1) * 2 > streamSize) break;
            int16_t raw;
            std::memcpy(&raw, pData + n * 2, 2);
            if (desc.swap_bytes) raw = static_cast<int16_t>(BSWAP16(static_cast<uint16_t>(raw)));
            float f = (raw + 0.5f) / 32767.5f;
            if (n == 0) pos.x = f;
            else if (n == 1) pos.y = f;
            else if (n == 2) pos.z = f;
        }
    } else if (desc.type == 5) { // SINT16
        for (uint32_t n = 0; n < std::min(desc.attribute_size, 3u); ++n) {
            if (byteOffset + (n + 1) * 2 > streamSize) break;
            int16_t raw;
            std::memcpy(&raw, pData + n * 2, 2);
            if (desc.swap_bytes) raw = static_cast<int16_t>(BSWAP16(static_cast<uint16_t>(raw)));
            float f = static_cast<float>(raw);
            if (n == 0) pos.x = f;
            else if (n == 1) pos.y = f;
            else if (n == 2) pos.z = f;
        }
    } else if (desc.type == 4) { // UNORM8
        for (uint32_t n = 0; n < std::min(desc.attribute_size, 3u); ++n) {
            if (byteOffset + n + 1 > streamSize) break;
            uint8_t raw = pData[n];
            float f = raw / 255.0f;
            if (n == 0) pos.x = f;
            else if (n == 1) pos.y = f;
            else if (n == 2) pos.z = f;
        }
    } else if (desc.type == 6) { // CMP32
        if (byteOffset + 4 <= streamSize) {
            uint32_t raw;
            std::memcpy(&raw, pData, 4);
            if (desc.swap_bytes) raw = BSWAP32(raw);
            int32_t x_raw = static_cast<int32_t>((raw & 0x7FF) << 21) >> 21;
            int32_t y_raw = static_cast<int32_t>(((raw >> 11) & 0x7FF) << 21) >> 21;
            int32_t z_raw = static_cast<int32_t>(((raw >> 22) & 0x3FF) << 22) >> 22;
            pos.x = (x_raw << 5) / 32767.0f;
            pos.y = (y_raw << 5) / 32767.0f;
            pos.z = (z_raw << 6) / 32767.0f;
        }
    }
    return pos;
}

static Vector2 FetchRSXTexCoord(const RSXAttributeDesc& desc, int vertex_id, const uint8_t* pStream, size_t streamSize) {
    Vector2 uv{0.f, 0.f};
    if (!pStream || desc.stride == 0 || vertex_id < 0) return uv;
    int64_t byteOffsetSigned = static_cast<int64_t>(desc.starting_offset) + static_cast<int64_t>(vertex_id) * static_cast<int64_t>(desc.stride);
    if (byteOffsetSigned < 0 || static_cast<size_t>(byteOffsetSigned + 8) > streamSize) return uv;
    size_t byteOffset = static_cast<size_t>(byteOffsetSigned);
    const uint8_t* pData = pStream + byteOffset;

    if (desc.type == 2) { // Float32
        for (uint32_t n = 0; n < std::min(desc.attribute_size, 2u); ++n) {
            if (byteOffset + (n + 1) * 4 > streamSize) break;
            uint32_t raw;
            std::memcpy(&raw, pData + n * 4, 4);
            if (desc.swap_bytes) raw = BSWAP32(raw);
            float f;
            std::memcpy(&f, &raw, 4);
            if (n == 0) uv.u = f;
            else if (n == 1) uv.v = f;
        }
    } else if (desc.type == 3) { // Float16
        for (uint32_t n = 0; n < std::min(desc.attribute_size, 2u); ++n) {
            if (byteOffset + (n + 1) * 2 > streamSize) break;
            uint16_t raw;
            std::memcpy(&raw, pData + n * 2, 2);
            if (desc.swap_bytes) raw = BSWAP16(raw);
            float f = VertexParser::HalfToFloat(raw);
            if (n == 0) uv.u = f;
            else if (n == 1) uv.v = f;
        }
    } else if (desc.type == 4) { // UNORM8
        for (uint32_t n = 0; n < std::min(desc.attribute_size, 2u); ++n) {
            if (byteOffset + n + 1 > streamSize) break;
            uint8_t raw = pData[n];
            float f = raw / 255.0f;
            if (n == 0) uv.u = f;
            else if (n == 1) uv.v = f;
        }
    } else if (desc.type == 1) { // SNORM16
        for (uint32_t n = 0; n < std::min(desc.attribute_size, 2u); ++n) {
            if (byteOffset + (n + 1) * 2 > streamSize) break;
            int16_t raw;
            std::memcpy(&raw, pData + n * 2, 2);
            if (desc.swap_bytes) raw = static_cast<int16_t>(BSWAP16(static_cast<uint16_t>(raw)));
            float f = (raw + 0.5f) / 32767.5f;
            if (n == 0) uv.u = f;
            else if (n == 1) uv.v = f;
        }
    }
    return uv;
}

void VulkanRipper::OnCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (captureFramesRemaining.load() <= 0) return;

    std::lock_guard<std::mutex> lock(stateMutex);
    auto cmdIt = cmdBufferStates.find(commandBuffer);
    if (cmdIt == cmdBufferStates.end()) {
        LogDebug("[DrawIndexed] Skipped: Command buffer state not tracked.");
        return;
    }
    const auto& state = cmdIt->second;

    // =========================================================================
    // RSX PROGRAMMABLE VERTEX PULLING EXTRACTION PATH
    // RPCS3 decodes RSX 3D draw calls via vertex pulling into ring buffers.
    // =========================================================================
    if (state.hasVertexPushConstant) {
        uint32_t pushVal = state.lastVertexPushConstant;
        uint32_t candidateOffsets[2] = { pushVal * 168u, pushVal };

        const RSXVertexLayoutEntry* pEntry = nullptr;
        for (uint32_t layoutOffset : candidateOffsets) {
            for (const auto& pair : bufferCache) {
                if ((pair.second.usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) && pair.second.mappedPtr) {
                    if (layoutOffset + sizeof(RSXVertexLayoutEntry) <= pair.second.size) {
                        const uint8_t* bufBase = static_cast<const uint8_t*>(pair.second.mappedPtr) + pair.second.memoryOffset;
                        const auto* candidate = reinterpret_cast<const RSXVertexLayoutEntry*>(bufBase + layoutOffset);

                        RSXAttributeDesc testPos = ParseRSXAttr(candidate->attrib_data[0], candidate->attrib_data[1]);
                        if (testPos.stride >= 8 && testPos.stride <= 256 &&
                            testPos.type >= 1 && testPos.type <= 7 &&
                            testPos.attribute_size >= 2 && testPos.attribute_size <= 4) {
                            pEntry = candidate;
                            vertexLayoutBuffer = pair.first;
                            break;
                        }
                    }
                }
            }
            if (pEntry) break;
        }

        if (pEntry) {
            RSXAttributeDesc posDesc = ParseRSXAttr(pEntry->attrib_data[0], pEntry->attrib_data[1]);

            // Normal attribute: attribute 2 (NV4097_SET_VERTEX_DATA_ARRAY_OFFSET + 2)
            RSXAttributeDesc normDesc = ParseRSXAttr(pEntry->attrib_data[2 * 2 + 0], pEntry->attrib_data[2 * 2 + 1]);
            bool hasNorm = (normDesc.stride > 0 && normDesc.type >= 1 && normDesc.type <= 6 && normDesc.attribute_size >= 3);

            // TexCoord attribute: attribute 8 or scan for float2/half2/unorm8_2
            RSXAttributeDesc uvDesc{};
            bool hasUv = false;
            RSXAttributeDesc testUv = ParseRSXAttr(pEntry->attrib_data[8 * 2 + 0], pEntry->attrib_data[8 * 2 + 1]);
            if (testUv.stride > 0 && testUv.type >= 1 && testUv.type <= 4 && testUv.attribute_size >= 2) {
                uvDesc = testUv;
                hasUv = true;
            } else {
                for (int a = 1; a < 16; ++a) {
                    if (a == 2 && hasNorm) continue;
                    RSXAttributeDesc aDesc = ParseRSXAttr(pEntry->attrib_data[a * 2 + 0], pEntry->attrib_data[a * 2 + 1]);
                    if (aDesc.stride > 0 && (aDesc.type == 2 || aDesc.type == 3 || aDesc.type == 4) && aDesc.attribute_size == 2) {
                        uvDesc = aDesc;
                        hasUv = true;
                        break;
                    }
                }
            }

            // Find attribute ring buffer (UNIFORM_TEXEL_BUFFER_BIT)
            const uint8_t* pAttribStream = nullptr;
            size_t attribStreamSize = 0;
            if (attribRingBuffer != VK_NULL_HANDLE) {
                auto aIt = bufferCache.find(attribRingBuffer);
                if (aIt != bufferCache.end()) {
                    void* ptr = aIt->second.mappedPtr;
                    if (!ptr && aIt->second.memory != VK_NULL_HANDLE) {
                        auto mIt = memoryMappedPointers.find(aIt->second.memory);
                        if (mIt != memoryMappedPointers.end()) ptr = mIt->second;
                    }
                    if (ptr) {
                        pAttribStream = static_cast<const uint8_t*>(ptr) + aIt->second.memoryOffset;
                        attribStreamSize = aIt->second.size;
                    }
                }
            }
            if (!pAttribStream) {
                for (const auto& pair : bufferCache) {
                    if ((pair.second.usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)) {
                        void* ptr = pair.second.mappedPtr;
                        if (!ptr && pair.second.memory != VK_NULL_HANDLE) {
                            auto mIt = memoryMappedPointers.find(pair.second.memory);
                            if (mIt != memoryMappedPointers.end()) ptr = mIt->second;
                        }
                        if (ptr) {
                            pAttribStream = static_cast<const uint8_t*>(ptr) + pair.second.memoryOffset;
                            attribStreamSize = pair.second.size;
                            attribRingBuffer = pair.first;
                            break;
                        }
                    }
                }
            }

            // Find index buffer
            auto iMemIt = bufferCache.find(state.indexBuffer.buffer);
            const uint8_t* pIndexBase = nullptr;
            size_t indexBufSize = 0;
            if (iMemIt != bufferCache.end()) {
                void* ptr = iMemIt->second.mappedPtr;
                if (!ptr && iMemIt->second.memory != VK_NULL_HANDLE) {
                    auto mIt = memoryMappedPointers.find(iMemIt->second.memory);
                    if (mIt != memoryMappedPointers.end()) ptr = mIt->second;
                }
                if (ptr) {
                    pIndexBase = static_cast<const uint8_t*>(ptr) + iMemIt->second.memoryOffset + state.indexBuffer.offset;
                    indexBufSize = (iMemIt->second.size > state.indexBuffer.offset) ? (iMemIt->second.size - state.indexBuffer.offset) : 0;
                }
            }

            if (pAttribStream && pIndexBase && indexCount >= 3) {
                ExtractedMesh mesh;
                mesh.drawCallId = currentDrawCallIndex;
                mesh.name = "Model_RSX_Draw" + std::to_string(currentDrawCallIndex) + "_Tris" + std::to_string(indexCount / 3);

                mesh.positions.reserve(indexCount);
                if (hasNorm) mesh.normals.reserve(indexCount);
                if (hasUv) mesh.uvs.reserve(indexCount);
                mesh.indices.reserve(indexCount);

                std::unordered_map<uint32_t, uint32_t> indexRemap;

                for (uint32_t i = 0; i < indexCount; ++i) {
                    uint32_t rawIndex = 0;
                    if (state.indexType == VK_INDEX_TYPE_UINT16) {
                        if ((firstIndex + i + 1) * sizeof(uint16_t) > indexBufSize) break;
                        rawIndex = reinterpret_cast<const uint16_t*>(pIndexBase)[firstIndex + i];
                        if (rawIndex == 0xFFFF) continue; // Primitive restart
                    } else if (state.indexType == VK_INDEX_TYPE_UINT32) {
                        if ((firstIndex + i + 1) * sizeof(uint32_t) > indexBufSize) break;
                        rawIndex = reinterpret_cast<const uint32_t*>(pIndexBase)[firstIndex + i];
                        if (rawIndex == 0xFFFFFFFF) continue; // Primitive restart
                    } else if (state.indexType == VK_INDEX_TYPE_UINT8_EXT) {
                        if ((firstIndex + i + 1) > indexBufSize) break;
                        rawIndex = pIndexBase[firstIndex + i];
                        if (rawIndex == 0xFF) continue;
                    }

                    int32_t gl_VertexIndex = static_cast<int32_t>(rawIndex) + vertexOffset;

                    int32_t pos_vertex_id = 0;
                    if (posDesc.frequency == 0) {
                        pos_vertex_id = 0;
                    } else if (posDesc.modulo) {
                        pos_vertex_id = (gl_VertexIndex + static_cast<int32_t>(pEntry->vertex_index_offset)) % static_cast<int32_t>(posDesc.frequency);
                    } else {
                        if (gl_VertexIndex >= static_cast<int32_t>(pEntry->vertex_base_index)) {
                            pos_vertex_id = (gl_VertexIndex - static_cast<int32_t>(pEntry->vertex_base_index)) / static_cast<int32_t>(posDesc.frequency);
                        } else {
                            pos_vertex_id = gl_VertexIndex / static_cast<int32_t>(posDesc.frequency);
                        }
                    }
                    if (pos_vertex_id < 0) pos_vertex_id = 0;

                    auto remapIt = indexRemap.find(rawIndex);
                    if (remapIt != indexRemap.end()) {
                        mesh.indices.push_back(remapIt->second);
                    } else {
                        Vector3 pos = FetchRSXPos(posDesc, pos_vertex_id, pAttribStream, attribStreamSize);
                        uint32_t newIdx = static_cast<uint32_t>(mesh.positions.size());
                        mesh.positions.push_back(pos);

                        if (hasNorm) {
                            int32_t norm_vertex_id = 0;
                            if (normDesc.frequency == 0) norm_vertex_id = 0;
                            else if (normDesc.modulo) norm_vertex_id = (gl_VertexIndex + static_cast<int32_t>(pEntry->vertex_index_offset)) % static_cast<int32_t>(normDesc.frequency);
                            else if (gl_VertexIndex >= static_cast<int32_t>(pEntry->vertex_base_index)) norm_vertex_id = (gl_VertexIndex - static_cast<int32_t>(pEntry->vertex_base_index)) / static_cast<int32_t>(normDesc.frequency);
                            else norm_vertex_id = gl_VertexIndex / static_cast<int32_t>(normDesc.frequency);
                            if (norm_vertex_id < 0) norm_vertex_id = 0;
                            Vector3 norm = FetchRSXPos(normDesc, norm_vertex_id, pAttribStream, attribStreamSize);
                            mesh.normals.push_back(norm);
                        }

                        if (hasUv) {
                            int32_t uv_vertex_id = 0;
                            if (uvDesc.frequency == 0) uv_vertex_id = 0;
                            else if (uvDesc.modulo) uv_vertex_id = (gl_VertexIndex + static_cast<int32_t>(pEntry->vertex_index_offset)) % static_cast<int32_t>(uvDesc.frequency);
                            else if (gl_VertexIndex >= static_cast<int32_t>(pEntry->vertex_base_index)) uv_vertex_id = (gl_VertexIndex - static_cast<int32_t>(pEntry->vertex_base_index)) / static_cast<int32_t>(uvDesc.frequency);
                            else uv_vertex_id = gl_VertexIndex / static_cast<int32_t>(uvDesc.frequency);
                            if (uv_vertex_id < 0) uv_vertex_id = 0;
                            Vector2 uv = FetchRSXTexCoord(uvDesc, uv_vertex_id, pAttribStream, attribStreamSize);
                            mesh.uvs.push_back(uv);
                        }

                        indexRemap[rawIndex] = newIdx;
                        mesh.indices.push_back(newIdx);
                    }
                }

                if (mesh.indices.size() >= 3 && !mesh.positions.empty()) {
                    // Sanitize isolated corrupt vertices before filtering and export
                    Vector3 lastGoodPos{0.f, 0.f, 0.f};
                    for (auto& pos : mesh.positions) {
                        if (std::isnan(pos.x) || std::isnan(pos.y) || std::isnan(pos.z) ||
                            std::isinf(pos.x) || std::isinf(pos.y) || std::isinf(pos.z) ||
                            std::abs(pos.x) > 1e7f || std::abs(pos.y) > 1e7f || std::abs(pos.z) > 1e7f) {
                            pos = lastGoodPos;
                        } else {
                            lastGoodPos = pos;
                        }
                    }

                    std::string rejectReason;
                    if (ModelFilter::ShouldKeepMesh(mesh, filterSettings, &rejectReason)) {
                        std::filesystem::path folder = std::filesystem::path(outputDir) / ("Frame_" + std::to_string(frameCounter));
                        std::filesystem::create_directories(folder);

                        std::filesystem::path objPath = folder / (mesh.name + ".obj");
                        if (ObjExporter::ExportToFile(mesh, objPath.string())) {
                            std::ostringstream logMsg;
                            logMsg << "[SUCCESS RSX] Exported: " << objPath.string()
                                   << " (Verts: " << mesh.positions.size() << ", Tris: " << mesh.indices.size() / 3
                                   << ", Stride: " << posDesc.stride << ", Type: " << posDesc.type << ")";
                            LogDebug(logMsg.str());
                        }
                    } else {
                        std::ostringstream logMsg;
                        logMsg << "[Filtered RSX] Dropped " << mesh.name << " Reason: " << rejectReason
                               << " (Stride: " << posDesc.stride << ", Type: " << posDesc.type << ", Verts: " << mesh.positions.size() << ")";
                        LogDebug(logMsg.str());
                    }
                }

                currentDrawCallIndex++;
                return; // RSX extraction completed!
            } else {
                std::ostringstream ss;
                ss << "[RSX] Layout found (stride=" << posDesc.stride << ", type=" << posDesc.type
                   << ") but pAttribStream=" << (pAttribStream != nullptr)
                   << ", pIndexBase=" << (pIndexBase != nullptr) << " - falling back to pipeline.";
                LogDebug(ss.str());
            }
        }
    }

    // =========================================================================
    // STANDARD VULKAN PIPELINE FALLBACK PATH
    // =========================================================================
    auto pipeIt = pipelineCache.find(state.currentPipeline);
    if (pipeIt == pipelineCache.end()) {
        LogDebug("[DrawIndexed] Skipped: Pipeline not found in cache.");
        return;
    }
    const auto& pipeline = pipeIt->second;

    // Find binding for position attribute (usually location 0, or float3/float4 format)
    uint32_t posBinding = 0;
    bool foundPosAttr = false;
    for (const auto& attr : pipeline.attributes) {
        if (attr.location == 0) {
            posBinding = attr.binding;
            foundPosAttr = true;
            break;
        }
    }
    if (!foundPosAttr) {
        for (const auto& attr : pipeline.attributes) {
            if (attr.format == VK_FORMAT_R32G32B32_SFLOAT || attr.format == VK_FORMAT_R32G32B32A32_SFLOAT ||
                attr.format == VK_FORMAT_R16G16B16A16_SFLOAT || attr.format == VK_FORMAT_R16G16B16A16_SNORM) {
                posBinding = attr.binding;
                foundPosAttr = true;
                break;
            }
        }
    }

    auto vBufIt = state.vertexBuffers.find(posBinding);
    if (vBufIt == state.vertexBuffers.end()) {
        if (!state.vertexBuffers.empty()) {
            vBufIt = state.vertexBuffers.begin();
            posBinding = vBufIt->first;
            std::ostringstream ss;
            ss << "[DrawIndexed] posBinding fallback to bound binding " << posBinding;
            LogDebug(ss.str());
        } else {
            std::ostringstream ss;
            ss << "[DrawIndexed] Skipped: No vertex buffers bound in state (total 0).";
            LogDebug(ss.str());
            return;
        }
    }

    auto vMemIt = bufferCache.find(vBufIt->second.buffer);
    auto iMemIt = bufferCache.find(state.indexBuffer.buffer);
    if (vMemIt == bufferCache.end()) {
        LogDebug("[DrawIndexed] Skipped: Vertex buffer not in bufferCache.");
        return;
    }
    if (iMemIt == bufferCache.end()) {
        LogDebug("[DrawIndexed] Skipped: Index buffer not in bufferCache.");
        return;
    }

    if (!vMemIt->second.mappedPtr || !iMemIt->second.mappedPtr) {
        std::ostringstream ss;
        ss << "[DrawIndexed] Skipped: Buffer memory unmapped! (vMem mapped="
           << (vMemIt->second.mappedPtr != nullptr) << ", iMem mapped="
           << (iMemIt->second.mappedPtr != nullptr) << ")";
        LogDebug(ss.str());
        return;
    }

    uint32_t vertexStride = 0;
    for (const auto& b : pipeline.bindings) {
        if (b.binding == posBinding) {
            vertexStride = b.stride;
            break;
        }
    }
    if (vertexStride == 0 && vBufIt->second.stride > 0) {
        vertexStride = static_cast<uint32_t>(vBufIt->second.stride);
    }
    if (vertexStride == 0) {
        uint32_t maxAttrEnd = 16; // Float4 default (16 bytes)
        for (const auto& a : pipeline.attributes) {
            if (a.binding == posBinding) {
                uint32_t attrSize = 16;
                if (a.format == VK_FORMAT_R32G32B32A32_SFLOAT) attrSize = 16;
                else if (a.format == VK_FORMAT_R32G32B32_SFLOAT) attrSize = 16; // Float4 SIMD vector alignment
                else if (a.format == VK_FORMAT_R32G32_SFLOAT) attrSize = 8;
                maxAttrEnd = std::max(maxAttrEnd, a.offset + attrSize);
            }
        }
        vertexStride = (maxAttrEnd + 15) & ~15;
    }

    if (vertexStride <= 12) {
        vertexStride = 16;
    }

    std::vector<VkVertexInputAttributeDescription> activeAttributes = pipeline.attributes;
    if (activeAttributes.empty()) {
        VkVertexInputAttributeDescription defAttr{};
        defAttr.location = 0;
        defAttr.binding = posBinding;
        defAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
        defAttr.offset = 0;
        activeAttributes.push_back(defAttr);
    }

    RawBufferView vView;
    vView.data = reinterpret_cast<const uint8_t*>(vMemIt->second.mappedPtr) + vMemIt->second.memoryOffset + vBufIt->second.offset;
    vView.size = (vMemIt->second.size > vBufIt->second.offset) ? (vMemIt->second.size - vBufIt->second.offset) : 0;

    RawBufferView iView;
    iView.data = reinterpret_cast<const uint8_t*>(iMemIt->second.mappedPtr) + iMemIt->second.memoryOffset + state.indexBuffer.offset;
    iView.size = (iMemIt->second.size > state.indexBuffer.offset) ? (iMemIt->second.size - state.indexBuffer.offset) : 0;

    std::ostringstream nameStream;
    nameStream << "Model_Draw" << currentDrawCallIndex++
               << "_Tris" << (indexCount / 3);

    ExtractedMesh mesh = VertexParser::ParseIndexed(
        nameStream.str(),
        currentDrawCallIndex,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(state.currentPipeline)),
        vView,
        vertexStride,
        activeAttributes,
        iView,
        state.indexType,
        firstIndex,
        indexCount,
        vertexOffset
    );

    std::string rejectReason;
    if (ModelFilter::ShouldKeepMesh(mesh, filterSettings, &rejectReason)) {
        std::filesystem::path folder = std::filesystem::path(outputDir) / ("Frame_" + std::to_string(frameCounter));
        std::filesystem::create_directories(folder);

        std::filesystem::path objPath = folder / (mesh.name + ".obj");
        if (ObjExporter::ExportToFile(mesh, objPath.string())) {
            std::ostringstream ss;
            ss << "[SUCCESS] Exported: " << objPath.string()
               << " (Verts: " << mesh.positions.size() << ", Indices: " << mesh.indices.size() << ")";
            LogDebug(ss.str());
        }
    } else {
        std::ostringstream ss;
        ss << "[Filtered] Dropped " << mesh.name
           << " (Verts: " << mesh.positions.size() << ", Indices: " << mesh.indices.size() << ") Reason: " << rejectReason;
        LogDebug(ss.str());
    }
}

void VulkanRipper::OnCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (captureFramesRemaining.load() <= 0) return;

    std::lock_guard<std::mutex> lock(stateMutex);
    auto cmdIt = cmdBufferStates.find(commandBuffer);
    if (cmdIt == cmdBufferStates.end()) return;
    const auto& state = cmdIt->second;

    auto pipeIt = pipelineCache.find(state.currentPipeline);
    if (pipeIt == pipelineCache.end()) return;
    const auto& pipeline = pipeIt->second;

    uint32_t posBinding = 0;
    bool foundPosAttr = false;
    for (const auto& attr : pipeline.attributes) {
        if (attr.location == 0) {
            posBinding = attr.binding;
            foundPosAttr = true;
            break;
        }
    }
    if (!foundPosAttr) {
        for (const auto& attr : pipeline.attributes) {
            if (attr.format == VK_FORMAT_R32G32B32_SFLOAT || attr.format == VK_FORMAT_R32G32B32A32_SFLOAT ||
                attr.format == VK_FORMAT_R16G16B16A16_SFLOAT || attr.format == VK_FORMAT_R16G16B16A16_SNORM) {
                posBinding = attr.binding;
                foundPosAttr = true;
                break;
            }
        }
    }

    auto vBufIt = state.vertexBuffers.find(posBinding);
    if (vBufIt == state.vertexBuffers.end()) {
        if (!state.vertexBuffers.empty()) {
            vBufIt = state.vertexBuffers.begin();
            posBinding = vBufIt->first;
        } else {
            return;
        }
    }

    auto vMemIt = bufferCache.find(vBufIt->second.buffer);
    if (vMemIt == bufferCache.end() || !vMemIt->second.mappedPtr) return;

    uint32_t vertexStride = 0;
    for (const auto& b : pipeline.bindings) {
        if (b.binding == posBinding) {
            vertexStride = b.stride;
            break;
        }
    }
    if (vertexStride == 0 && vBufIt->second.stride > 0) {
        vertexStride = static_cast<uint32_t>(vBufIt->second.stride);
    }
    if (vertexStride <= 12) {
        vertexStride = 16;
    }

    RawBufferView vView;
    vView.data = reinterpret_cast<const uint8_t*>(vMemIt->second.mappedPtr) + vMemIt->second.memoryOffset + vBufIt->second.offset;
    vView.size = (vMemIt->second.size > vBufIt->second.offset) ? (vMemIt->second.size - vBufIt->second.offset) : 0;

    std::ostringstream nameStream;
    nameStream << "Model_DrawNonIdx" << currentDrawCallIndex++;

    ExtractedMesh mesh = VertexParser::ParseNonIndexed(
        nameStream.str(),
        currentDrawCallIndex,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(state.currentPipeline)),
        vView,
        vertexStride,
        pipeline.attributes,
        firstVertex,
        vertexCount
    );

    std::string rejectReason;
    if (ModelFilter::ShouldKeepMesh(mesh, filterSettings, &rejectReason)) {
        std::filesystem::path folder = std::filesystem::path(outputDir) / ("Frame_" + std::to_string(frameCounter));
        std::filesystem::create_directories(folder);

        std::filesystem::path objPath = folder / (mesh.name + ".obj");
        if (ObjExporter::ExportToFile(mesh, objPath.string())) {
            std::ostringstream ss;
            ss << "[SUCCESS NonIndexed] Exported: " << objPath.string()
               << " (Verts: " << mesh.positions.size() << ")";
            LogDebug(ss.str());
        }
    } else {
        std::ostringstream ss;
        ss << "[Filtered NonIndexed] Dropped " << mesh.name
           << " (Verts: " << mesh.positions.size() << ") Reason: " << rejectReason;
        LogDebug(ss.str());
    }
}

void VulkanRipper::OnQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    static bool g_WasKeyPressed = false;
    bool keyPressed = (GetAsyncKeyState('5') & 0x8000) ||
                      (GetAsyncKeyState(VK_F8) & 0x8000) ||
                      (GetAsyncKeyState(VK_INSERT) & 0x8000) ||
                      (GetAsyncKeyState(VK_DELETE) & 0x8000) ||
                      (GetAsyncKeyState(VK_F9) & 0x8000);

    if (captureFramesRemaining.load() > 0) {
        int rem = --captureFramesRemaining;
        if (rem == 0) {
            Beep(1500, 100);
            LogDebug(">>> Capture window FINISHED <<<");
        }
    } else if (keyPressed && !g_WasKeyPressed) {
        captureFramesRemaining = 2; // Capture the next complete frame
        frameCounter++;
        currentDrawCallIndex = 0;
        Beep(1000, 150);
        LogDebug(">>> HOTKEY DETECTED: Capture armed for next frame <<<");
        try {
            std::filesystem::create_directories(outputDir);
        } catch (...) {}
    }

    g_WasKeyPressed = keyPressed;
}

} // namespace OpenRipper
