#include "D3D11Capture.h"
#include "D3D11Hooks.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <unordered_map>
#include <iostream>

namespace OpenRipper {
namespace D3D11 {

static std::string PadNumber(uint32_t num, int width = 4) {
    std::ostringstream ss;
    ss << std::setw(width) << std::setfill('0') << num;
    return ss.str();
}

static float HalfToFloat(uint16_t h) {
    uint32_t sign = (h >> 15) & 0x0001;
    uint32_t exp  = (h >> 10) & 0x001f;
    uint32_t mant = h & 0x03ff;

    if (exp == 0) {
        if (mant == 0) return sign ? -0.0f : 0.0f;
        while (!(mant & 0x0400)) {
            mant <<= 1;
            exp--;
        }
        exp++;
        mant &= ~0x0400;
    } else if (exp == 31) {
        return (mant == 0) ? (sign ? -INFINITY : INFINITY) : NAN;
    }

    exp = exp + (127 - 15);
    mant = mant << 13;
    uint32_t result = (sign << 31) | (exp << 23) | mant;
    float f;
    memcpy(&f, &result, sizeof(f));
    return f;
}

D3D11Capture& D3D11Capture::Get() {
    static D3D11Capture s_Instance;
    return s_Instance;
}

void D3D11Capture::Init(const std::string& outputDir) {
    m_OutputDir = outputDir;
    CreateDirectoryA(m_OutputDir.c_str(), NULL);
}

void D3D11Capture::StartFrameCapture(uint32_t frameNumber) {
    m_FrameNumber = frameNumber;
    m_DrawCallCounter = 0;
    m_SavedMeshCounter = 0;

    m_CurrentFrameDir = m_OutputDir + "\\Frame_" + PadNumber(m_FrameNumber);
    CreateDirectoryA(m_CurrentFrameDir.c_str(), NULL);

    m_IsCapturing = true;
    Beep(750, 150);
}

void D3D11Capture::EndFrameCapture() {
    if (m_IsCapturing) {
        m_IsCapturing = false;
        Beep(1000, 150);
    }
}

void D3D11Capture::RegisterInputLayout(ID3D11InputLayout* pLayout, const D3D11_INPUT_ELEMENT_DESC* pDescs, UINT numElements) {
    if (!pLayout || !pDescs || numElements == 0) return;

    std::vector<LayoutElement> elems;
    elems.reserve(numElements);

    // Track running offsets per slot for D3D11_APPEND_ALIGNED_ELEMENT
    UINT slotOffsets[16] = {};

    for (UINT i = 0; i < numElements; ++i) {
        const auto& desc = pDescs[i];
        LayoutElement elem;
        elem.semanticName = desc.SemanticName ? desc.SemanticName : "";
        elem.semanticIndex = desc.SemanticIndex;
        elem.format = desc.Format;
        elem.inputSlot = desc.InputSlot;

        UINT elemSize = 0;
        switch (desc.Format) {
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
            case DXGI_FORMAT_R32G32B32A32_UINT:
            case DXGI_FORMAT_R32G32B32A32_SINT:
                elemSize = 16; break;
            case DXGI_FORMAT_R32G32B32_FLOAT:
            case DXGI_FORMAT_R32G32B32_UINT:
            case DXGI_FORMAT_R32G32B32_SINT:
                elemSize = 12; break;
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_R16G16B16A16_UNORM:
            case DXGI_FORMAT_R16G16B16A16_UINT:
            case DXGI_FORMAT_R16G16B16A16_SNORM:
            case DXGI_FORMAT_R16G16B16A16_SINT:
            case DXGI_FORMAT_R32G32_FLOAT:
            case DXGI_FORMAT_R32G32_UINT:
            case DXGI_FORMAT_R32G32_SINT:
                elemSize = 8; break;
            case DXGI_FORMAT_R10G10B10A2_UNORM:
            case DXGI_FORMAT_R10G10B10A2_UINT:
            case DXGI_FORMAT_R11G11B10_FLOAT:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_R8G8B8A8_UINT:
            case DXGI_FORMAT_R8G8B8A8_SNORM:
            case DXGI_FORMAT_R8G8B8A8_SINT:
            case DXGI_FORMAT_R16G16_FLOAT:
            case DXGI_FORMAT_R16G16_UNORM:
            case DXGI_FORMAT_R16G16_UINT:
            case DXGI_FORMAT_R16G16_SNORM:
            case DXGI_FORMAT_R16G16_SINT:
            case DXGI_FORMAT_R32_FLOAT:
            case DXGI_FORMAT_R32_UINT:
            case DXGI_FORMAT_R32_SINT:
                elemSize = 4; break;
            case DXGI_FORMAT_R8G8_UNORM:
            case DXGI_FORMAT_R8G8_UINT:
            case DXGI_FORMAT_R8G8_SNORM:
            case DXGI_FORMAT_R8G8_SINT:
            case DXGI_FORMAT_R16_FLOAT:
            case DXGI_FORMAT_R16_UNORM:
            case DXGI_FORMAT_R16_UINT:
            case DXGI_FORMAT_R16_SNORM:
            case DXGI_FORMAT_R16_SINT:
                elemSize = 2; break;
            case DXGI_FORMAT_R8_UNORM:
            case DXGI_FORMAT_R8_UINT:
            case DXGI_FORMAT_R8_SNORM:
            case DXGI_FORMAT_R8_SINT:
                elemSize = 1; break;
            default:
                elemSize = 4; break;
        }

        UINT slot = desc.InputSlot < 16 ? desc.InputSlot : 0;
        if (desc.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT) {
            elem.alignedByteOffset = slotOffsets[slot];
        } else {
            elem.alignedByteOffset = desc.AlignedByteOffset;
        }
        slotOffsets[slot] = elem.alignedByteOffset + elemSize;

        elems.push_back(elem);
    }

    m_LayoutMap[pLayout] = std::move(elems);
    Log("[OpenRipper D3D11] RegisterInputLayout: layout=%p stored, elements=%u, total layouts in map=%zu\n",
        pLayout, numElements, m_LayoutMap.size());
}

void D3D11Capture::OnSetInputLayout(ID3D11InputLayout* pLayout) {
    m_CurrentLayout = pLayout;
}

void D3D11Capture::OnSetVertexBuffers(UINT startSlot, UINT numBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) {
    for (UINT i = 0; i < numBuffers; ++i) {
        UINT slot = startSlot + i;
        if (slot >= 16) break;
        m_VertexBuffers[slot].pBuffer = ppVertexBuffers ? ppVertexBuffers[i] : nullptr;
        m_VertexBuffers[slot].stride  = pStrides ? pStrides[i] : 0;
        m_VertexBuffers[slot].offset  = pOffsets ? pOffsets[i] : 0;
    }
}

void D3D11Capture::OnSetIndexBuffer(ID3D11Buffer* pIndexBuffer, DXGI_FORMAT format, UINT offset) {
    m_CurrentIB = pIndexBuffer;
    m_IndexFormat = format;
    m_IndexOffset = offset;
}

void D3D11Capture::OnSetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology) {
    m_Topology = topology;
}

ID3D11Buffer* D3D11Capture::GetOrCreateStagingBuffer(ID3D11Device* pDevice, UINT sizeInBytes) {
    for (const auto& pair : m_StagingBuffers) {
        if (pair.first >= sizeInBytes) {
            return pair.second;
        }
    }

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = ((sizeInBytes + 65535) / 65536) * 65536; // Round up to 64KB
    if (desc.ByteWidth < 65536) desc.ByteWidth = 65536;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Buffer* pStaging = nullptr;
    HRESULT hr = pDevice->CreateBuffer(&desc, NULL, &pStaging);
    if (SUCCEEDED(hr) && pStaging) {
        m_StagingBuffers.push_back({desc.ByteWidth, pStaging});
        return pStaging;
    }
    return nullptr;
}

bool D3D11Capture::ReadBufferData(ID3D11DeviceContext* pContext, ID3D11Buffer* pGpuBuffer, UINT byteOffset, UINT byteLength, std::vector<uint8_t>& outData) {
    if (!pContext || !pGpuBuffer || byteLength == 0) return false;

    ID3D11Device* pDevice = nullptr;
    pContext->GetDevice(&pDevice);
    if (!pDevice) return false;

    D3D11_BUFFER_DESC srcDesc = {};
    pGpuBuffer->GetDesc(&srcDesc);

    if (byteOffset + byteLength > srcDesc.ByteWidth) {
        if (byteOffset >= srcDesc.ByteWidth) {
            pDevice->Release();
            return false;
        }
        byteLength = srcDesc.ByteWidth - byteOffset;
    }

    ID3D11Buffer* pStaging = GetOrCreateStagingBuffer(pDevice, byteLength);
    pDevice->Release();
    if (!pStaging) return false;

    D3D11_BOX box = {};
    box.left = byteOffset;
    box.right = byteOffset + byteLength;
    box.top = 0;
    box.bottom = 1;
    box.front = 0;
    box.back = 1;

    pContext->CopySubresourceRegion(pStaging, 0, 0, 0, 0, pGpuBuffer, 0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = pContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr) || !mapped.pData) return false;

    outData.resize(byteLength);
    memcpy(outData.data(), mapped.pData, byteLength);
    pContext->Unmap(pStaging, 0);
    return true;
}

static void DecodeFloat3(DXGI_FORMAT fmt, const uint8_t* src, Vector3& out) {
    switch (fmt) {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32_FLOAT: {
            const float* f = (const float*)src;
            out = {f[0], f[1], f[2]};
            break;
        }
        case DXGI_FORMAT_R16G16B16A16_FLOAT: {
            const uint16_t* h = (const uint16_t*)src;
            out = {HalfToFloat(h[0]), HalfToFloat(h[1]), HalfToFloat(h[2])};
            break;
        }
        case DXGI_FORMAT_R16G16B16A16_SNORM: {
            const int16_t* s = (const int16_t*)src;
            out = {(float)s[0] / 32767.0f, (float)s[1] / 32767.0f, (float)s[2] / 32767.0f};
            break;
        }
        default:
            out = {0.0f, 0.0f, 0.0f};
            break;
    }
}

static void DecodeFloat2(DXGI_FORMAT fmt, const uint8_t* src, Vector2& out) {
    switch (fmt) {
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_FLOAT: {
            const float* f = (const float*)src;
            out = {f[0], f[1]};
            break;
        }
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_FLOAT: {
            const uint16_t* h = (const uint16_t*)src;
            out = {HalfToFloat(h[0]), HalfToFloat(h[1])};
            break;
        }
        case DXGI_FORMAT_R16G16_UNORM: {
            const uint16_t* u = (const uint16_t*)src;
            out = {(float)u[0] / 65535.0f, (float)u[1] / 65535.0f};
            break;
        }
        case DXGI_FORMAT_R16G16_SNORM: {
            const int16_t* s = (const int16_t*)src;
            out = {(float)s[0] / 32767.0f, (float)s[1] / 32767.0f};
            break;
        }
        default:
            out = {0.0f, 0.0f};
            break;
    }
}

bool D3D11Capture::ExtractMesh(ID3D11DeviceContext* pContext,
                              const std::vector<uint32_t>& indices,
                              INT baseVertexLocation,
                              ExtractedMesh& outMesh) {
    if (indices.empty()) {
        Log("[OpenRipper D3D11] ExtractMesh failed: indices empty\n");
        return false;
    }
    if (!m_CurrentLayout) {
        Log("[OpenRipper D3D11] ExtractMesh failed: m_CurrentLayout is NULL\n");
        return false;
    }

    std::vector<LayoutElement> fallbackLayout;
    const std::vector<LayoutElement>* pLayoutElems = nullptr;

    auto it = m_LayoutMap.find(m_CurrentLayout);
    if (it != m_LayoutMap.end() && !it->second.empty()) {
        pLayoutElems = &it->second;
    } else {
        // Fallback: The application created its ID3D11InputLayout before OpenRipper was injected.
        // Synthesize a standard vertex layout according to the buffer stride.
        UINT stride0 = m_VertexBuffers[0].stride;
        Log("[OpenRipper D3D11] Layout %p not hooked during creation. Synthesizing layout for stride %u\n", m_CurrentLayout, stride0);

        LayoutElement posElem;
        posElem.semanticName = "POSITION";
        posElem.semanticIndex = 0;
        posElem.format = DXGI_FORMAT_R32G32B32_FLOAT;
        posElem.inputSlot = 0;
        posElem.alignedByteOffset = 0;
        fallbackLayout.push_back(posElem);

        if (stride0 >= 24) { // e.g. pos (12) + normal (12) or pos (12) + uv (8) + ...
            if (stride0 == 32) {
                // pos(12) + norm(12) + uv(8)
                LayoutElement normElem;
                normElem.semanticName = "NORMAL";
                normElem.semanticIndex = 0;
                normElem.format = DXGI_FORMAT_R32G32B32_FLOAT;
                normElem.inputSlot = 0;
                normElem.alignedByteOffset = 12;
                fallbackLayout.push_back(normElem);

                LayoutElement uvElem;
                uvElem.semanticName = "TEXCOORD";
                uvElem.semanticIndex = 0;
                uvElem.format = DXGI_FORMAT_R32G32_FLOAT;
                uvElem.inputSlot = 0;
                uvElem.alignedByteOffset = 24;
                fallbackLayout.push_back(uvElem);
            } else if (stride0 >= 36) {
                // pos(12) + norm(12) + uv(8) + tangents/blendweights...
                LayoutElement normElem;
                normElem.semanticName = "NORMAL";
                normElem.semanticIndex = 0;
                normElem.format = DXGI_FORMAT_R32G32B32_FLOAT;
                normElem.inputSlot = 0;
                normElem.alignedByteOffset = 12;
                fallbackLayout.push_back(normElem);

                LayoutElement uvElem;
                uvElem.semanticName = "TEXCOORD";
                uvElem.semanticIndex = 0;
                uvElem.format = DXGI_FORMAT_R32G32_FLOAT;
                uvElem.inputSlot = 0;
                uvElem.alignedByteOffset = 24;
                fallbackLayout.push_back(uvElem);
            } else if (stride0 == 28) {
                // pos(12) + norm(12) + color/half2(4)
                LayoutElement normElem;
                normElem.semanticName = "NORMAL";
                normElem.semanticIndex = 0;
                normElem.format = DXGI_FORMAT_R32G32B32_FLOAT;
                normElem.inputSlot = 0;
                normElem.alignedByteOffset = 12;
                fallbackLayout.push_back(normElem);
            } else if (stride0 == 24) {
                // pos(12) + norm(12)
                LayoutElement normElem;
                normElem.semanticName = "NORMAL";
                normElem.semanticIndex = 0;
                normElem.format = DXGI_FORMAT_R32G32B32_FLOAT;
                normElem.inputSlot = 0;
                normElem.alignedByteOffset = 12;
                fallbackLayout.push_back(normElem);
            } else if (stride0 == 20) {
                // pos(12) + uv(8)
                LayoutElement uvElem;
                uvElem.semanticName = "TEXCOORD";
                uvElem.semanticIndex = 0;
                uvElem.format = DXGI_FORMAT_R32G32_FLOAT;
                uvElem.inputSlot = 0;
                uvElem.alignedByteOffset = 12;
                fallbackLayout.push_back(uvElem);
            } else if (stride0 == 16) {
                // pos(12) + color/pad(4)
            }
        }

        m_LayoutMap[m_CurrentLayout] = fallbackLayout;
        pLayoutElems = &m_LayoutMap[m_CurrentLayout];
    }

    const auto& layoutElems = *pLayoutElems;
    const LayoutElement* pPosElem = nullptr;
    const LayoutElement* pNormElem = nullptr;
    const LayoutElement* pUvElem = nullptr;

    for (const auto& elem : layoutElems) {
        if (_stricmp(elem.semanticName.c_str(), "POSITION") == 0 && elem.semanticIndex == 0 && !pPosElem) {
            pPosElem = &elem;
        } else if (_stricmp(elem.semanticName.c_str(), "NORMAL") == 0 && elem.semanticIndex == 0 && !pNormElem) {
            pNormElem = &elem;
        } else if (_stricmp(elem.semanticName.c_str(), "TEXCOORD") == 0 && elem.semanticIndex == 0 && !pUvElem) {
            pUvElem = &elem;
        }
    }

    if (!pPosElem) {
        Log("[OpenRipper D3D11] ExtractMesh failed: no POSITION element in layout\n");
        return false;
    }

    // Cache downloaded raw vertex buffers
    std::unordered_map<UINT, std::vector<uint8_t>> slotData;
    auto GetSlotBytes = [&](UINT slot) -> const std::vector<uint8_t>* {
        auto sit = slotData.find(slot);
        if (sit != slotData.end()) return &sit->second;
        if (slot >= 16 || !m_VertexBuffers[slot].pBuffer) return nullptr;

        D3D11_BUFFER_DESC bdesc = {};
        m_VertexBuffers[slot].pBuffer->GetDesc(&bdesc);
        std::vector<uint8_t> data;
        if (!ReadBufferData(pContext, m_VertexBuffers[slot].pBuffer, 0, bdesc.ByteWidth, data)) {
            return nullptr;
        }
        slotData[slot] = std::move(data);
        return &slotData[slot];
    };

    const auto* posBytes = GetSlotBytes(pPosElem->inputSlot);
    if (!posBytes || posBytes->empty()) {
        Log("[OpenRipper D3D11] ExtractMesh failed: could not read posBytes from slot %u (pBuffer=%p)\n",
            pPosElem->inputSlot, m_VertexBuffers[pPosElem->inputSlot].pBuffer);
        return false;
    }
    UINT posStride = m_VertexBuffers[pPosElem->inputSlot].stride;
    if (posStride == 0) {
        Log("[OpenRipper D3D11] ExtractMesh failed: posStride is 0 for slot %u\n", pPosElem->inputSlot);
        return false;
    }

    const auto* normBytes = pNormElem ? GetSlotBytes(pNormElem->inputSlot) : nullptr;
    UINT normStride = pNormElem ? m_VertexBuffers[pNormElem->inputSlot].stride : 0;

    const auto* uvBytes = pUvElem ? GetSlotBytes(pUvElem->inputSlot) : nullptr;
    UINT uvStride = pUvElem ? m_VertexBuffers[pUvElem->inputSlot].stride : 0;

    std::unordered_map<uint32_t, uint32_t> indexRemap;
    outMesh.positions.clear();
    outMesh.normals.clear();
    outMesh.uvs.clear();
    outMesh.indices.clear();

    for (uint32_t rawIdx : indices) {
        int64_t fullVtxIdx = (int64_t)rawIdx + baseVertexLocation;
        if (fullVtxIdx < 0) continue;
        uint32_t vIdx = (uint32_t)fullVtxIdx;

        auto remapIt = indexRemap.find(vIdx);
        if (remapIt != indexRemap.end()) {
            outMesh.indices.push_back(remapIt->second);
            continue;
        }

        size_t posOffset = (size_t)vIdx * posStride + pPosElem->alignedByteOffset;
        if (posOffset + 12 > posBytes->size()) continue;

        Vector3 pos;
        DecodeFloat3(pPosElem->format, posBytes->data() + posOffset, pos);
        outMesh.positions.push_back(pos);

        if (pNormElem && normBytes) {
            size_t normOffset = (size_t)vIdx * normStride + pNormElem->alignedByteOffset;
            Vector3 norm = {0.0f, 0.0f, 0.0f};
            if (normOffset + 4 <= normBytes->size()) {
                DecodeFloat3(pNormElem->format, normBytes->data() + normOffset, norm);
            }
            outMesh.normals.push_back(norm);
        }

        if (pUvElem && uvBytes) {
            size_t uvOffset = (size_t)vIdx * uvStride + pUvElem->alignedByteOffset;
            Vector2 uv = {0.0f, 0.0f};
            if (uvOffset + 4 <= uvBytes->size()) {
                DecodeFloat2(pUvElem->format, uvBytes->data() + uvOffset, uv);
            }
            outMesh.uvs.push_back(uv);
        }

        uint32_t newIdx = (uint32_t)outMesh.positions.size() - 1;
        indexRemap[vIdx] = newIdx;
        outMesh.indices.push_back(newIdx);
    }

    return !outMesh.positions.empty() && !outMesh.indices.empty();
}

void D3D11Capture::OnDraw(ID3D11DeviceContext* pContext, UINT vertexCount, UINT startVertexLocation) {
    if (!m_IsCapturing || vertexCount < 3) return;
    m_DrawCallCounter++;

    if (m_Topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST && m_Topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP) return;

    std::vector<uint32_t> rawIndices(vertexCount);
    for (UINT i = 0; i < vertexCount; ++i) rawIndices[i] = startVertexLocation + i;

    std::vector<uint32_t> triIndices;
    if (m_Topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
        triIndices = std::move(rawIndices);
    } else if (m_Topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP) {
        for (size_t i = 0; i + 2 < vertexCount; ++i) {
            uint32_t a = rawIndices[i];
            uint32_t b = rawIndices[i + 1];
            uint32_t c = rawIndices[i + 2];
            if (a == b || b == c || a == c) continue;
            if (i % 2 == 0) {
                triIndices.push_back(a); triIndices.push_back(b); triIndices.push_back(c);
            } else {
                triIndices.push_back(b); triIndices.push_back(a); triIndices.push_back(c);
            }
        }
    }

    ExtractedMesh mesh;
    mesh.drawCallId = m_DrawCallCounter;
    if (ExtractMesh(pContext, triIndices, 0, mesh)) {
        if (ModelFilter::ShouldKeepMesh(mesh)) {
            std::string path = m_CurrentFrameDir + "\\mesh_" + PadNumber(m_SavedMeshCounter++) + ".obj";
            ObjExporter::ExportToFile(mesh, path);
        }
    }
}

void D3D11Capture::OnDrawIndexed(ID3D11DeviceContext* pContext, UINT indexCount, UINT startIndexLocation, INT baseVertexLocation) {
    if (!m_IsCapturing) return;
    if (!m_CurrentIB) {
        Log("[OpenRipper D3D11] OnDrawIndexed: m_CurrentIB is NULL!\n");
        return;
    }
    if (indexCount < 3) {
        Log("[OpenRipper D3D11] OnDrawIndexed: indexCount < 3 (%u)\n", indexCount);
        return;
    }
    m_DrawCallCounter++;

    if (m_Topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST && m_Topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP) {
        Log("[OpenRipper D3D11] OnDrawIndexed: topology not supported (%u)\n", (UINT)m_Topology);
        return;
    }

    UINT indexElemSize = (m_IndexFormat == DXGI_FORMAT_R32_UINT) ? 4 : 2;
    UINT byteOffset = m_IndexOffset + startIndexLocation * indexElemSize;
    UINT byteLength = indexCount * indexElemSize;

    std::vector<uint8_t> rawBytes;
    if (!ReadBufferData(pContext, m_CurrentIB, byteOffset, byteLength, rawBytes)) {
        Log("[OpenRipper D3D11] OnDrawIndexed: ReadBufferData failed for IB=%p, offset=%u, len=%u\n", m_CurrentIB, byteOffset, byteLength);
        return;
    }

    std::vector<uint32_t> rawIndices(indexCount);
    if (m_IndexFormat == DXGI_FORMAT_R32_UINT) {
        const uint32_t* p32 = (const uint32_t*)rawBytes.data();
        for (UINT i = 0; i < indexCount; ++i) rawIndices[i] = p32[i];
    } else {
        const uint16_t* p16 = (const uint16_t*)rawBytes.data();
        for (UINT i = 0; i < indexCount; ++i) rawIndices[i] = p16[i];
    }

    std::vector<uint32_t> triIndices;
    if (m_Topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
        triIndices = std::move(rawIndices);
    } else if (m_Topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP) {
        for (size_t i = 0; i + 2 < indexCount; ++i) {
            uint32_t a = rawIndices[i];
            uint32_t b = rawIndices[i + 1];
            uint32_t c = rawIndices[i + 2];
            if (a == b || b == c || a == c) continue;
            if (a == 0xFFFF || b == 0xFFFF || c == 0xFFFF) continue;
            if (a == 0xFFFFFFFF || b == 0xFFFFFFFF || c == 0xFFFFFFFF) continue;

            if (i % 2 == 0) {
                triIndices.push_back(a); triIndices.push_back(b); triIndices.push_back(c);
            } else {
                triIndices.push_back(b); triIndices.push_back(a); triIndices.push_back(c);
            }
        }
    }

    ExtractedMesh mesh;
    mesh.drawCallId = m_DrawCallCounter;
    if (ExtractMesh(pContext, triIndices, baseVertexLocation, mesh)) {
        Log("[OpenRipper D3D11] ExtractMesh succeeded! Vertices=%zu, Indices=%zu\n", mesh.positions.size(), mesh.indices.size());
        if (ModelFilter::ShouldKeepMesh(mesh)) {
            std::string path = m_CurrentFrameDir + "\\mesh_" + PadNumber(m_SavedMeshCounter++) + ".obj";
            bool ok = ObjExporter::ExportToFile(mesh, path);
            Log("[OpenRipper D3D11] ExportToFile %s: %d\n", path.c_str(), ok);
        } else {
            Log("[OpenRipper D3D11] ModelFilter filtered out mesh!\n");
        }
    } else {
        Log("[OpenRipper D3D11] ExtractMesh returned false!\n");
    }
}

void D3D11Capture::OnDrawInstanced(ID3D11DeviceContext* pContext, UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation) {
    OnDraw(pContext, vertexCountPerInstance, startVertexLocation);
}

void D3D11Capture::OnDrawIndexedInstanced(ID3D11DeviceContext* pContext, UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation) {
    OnDrawIndexed(pContext, indexCountPerInstance, startIndexLocation, baseVertexLocation);
}

} // namespace D3D11
} // namespace OpenRipper
