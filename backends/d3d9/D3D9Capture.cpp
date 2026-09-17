#include "D3D9Capture.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <unordered_map>
#include <iostream>

namespace OpenRipper {
namespace D3D9 {

static std::string PadNumber(uint32_t num, int width = 4) {
    std::ostringstream ss;
    ss << std::setw(width) << std::setfill('0') << num;
    return ss.str();
}

// Convert 16-bit half float to 32-bit float
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

D3D9Capture& D3D9Capture::Get() {
    static D3D9Capture s_Instance;
    return s_Instance;
}

void D3D9Capture::Init(const std::string& outputDir) {
    m_OutputDir = outputDir;
    CreateDirectoryA(m_OutputDir.c_str(), NULL);
}

void D3D9Capture::StartFrameCapture(uint32_t frameNumber) {
    m_FrameNumber = frameNumber;
    m_DrawCallCounter = 0;
    m_SavedMeshCounter = 0;

    m_CurrentFrameDir = m_OutputDir + "\\Frame_" + PadNumber(m_FrameNumber);
    CreateDirectoryA(m_CurrentFrameDir.c_str(), NULL);

    m_IsCapturing = true;
    Beep(1200, 120);
    OutputDebugStringA("[OpenRipper D3D9] >>> Frame capture started <<<\n");
}

void D3D9Capture::EndFrameCapture() {
    if (!m_IsCapturing) return;
    m_IsCapturing = false;
    Beep(1800, 120);

    char buf[256];
    snprintf(buf, sizeof(buf), "[OpenRipper D3D9] <<< Frame capture ended. Total meshes saved: %u (Draw calls: %u) >>>\n",
             m_SavedMeshCounter, m_DrawCallCounter);
    OutputDebugStringA(buf);
}

void D3D9Capture::OnSetStreamSource(UINT streamNumber, IDirect3DVertexBuffer9* pStreamData, UINT offsetInBytes, UINT stride) {
    if (streamNumber < 16) {
        m_Streams[streamNumber].pVB = pStreamData;
        m_Streams[streamNumber].offset = offsetInBytes;
        m_Streams[streamNumber].stride = stride;
    }
}

void D3D9Capture::OnSetIndices(IDirect3DIndexBuffer9* pIndexData) {
    m_CurrentIB = pIndexData;
}

void D3D9Capture::OnSetFVF(DWORD fvf) {
    m_CurrentFVF = fvf;
    m_CurrentDecl = nullptr;
}

void D3D9Capture::OnSetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) {
    m_CurrentDecl = pDecl;
    m_CurrentFVF = 0;
}

bool D3D9Capture::DecodeFVF(DWORD fvf, std::vector<D3DVERTEXELEMENT9>& elements, UINT& outStride) {
    elements.clear();
    WORD offset = 0;

    // Position
    DWORD posFmt = fvf & D3DFVF_POSITION_MASK;
    if (posFmt == D3DFVF_XYZ) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0});
        offset += 12;
    } else if (posFmt == D3DFVF_XYZRHW) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0});
        offset += 16;
    } else if (posFmt == D3DFVF_XYZB1) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0});
        offset += 12 + 4;
    } else if (posFmt == D3DFVF_XYZB2) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0});
        offset += 12 + 8;
    } else if (posFmt == D3DFVF_XYZB3) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0});
        offset += 12 + 12;
    } else if (posFmt == D3DFVF_XYZB4) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0});
        offset += 12 + 16;
    } else if (posFmt == D3DFVF_XYZW) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0});
        offset += 16;
    } else {
        return false;
    }

    // Normal
    if (fvf & D3DFVF_NORMAL) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0});
        offset += 12;
    }

    // Point size
    if (fvf & D3DFVF_PSIZE) {
        offset += 4;
    }

    // Diffuse color
    if (fvf & D3DFVF_DIFFUSE) {
        elements.push_back({0, offset, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0});
        offset += 4;
    }

    // Specular color
    if (fvf & D3DFVF_SPECULAR) {
        elements.push_back({0, offset, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1});
        offset += 4;
    }

    // Texture coordinates
    UINT numTex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (UINT t = 0; t < numTex && t < 8; ++t) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, (BYTE)t});
        offset += 8;
    }

    // End marker
    elements.push_back({0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0});
    outStride = offset;
    return true;
}

static void ReadVec3(const uint8_t* ptr, BYTE type, Vector3& out) {
    if (type == D3DDECLTYPE_FLOAT3) {
        const float* f = (const float*)ptr;
        out.x = f[0]; out.y = f[1]; out.z = f[2];
    } else if (type == D3DDECLTYPE_FLOAT4) {
        const float* f = (const float*)ptr;
        out.x = f[0]; out.y = f[1]; out.z = f[2];
    } else if (type == D3DDECLTYPE_FLOAT2) {
        const float* f = (const float*)ptr;
        out.x = f[0]; out.y = f[1]; out.z = 0.0f;
    } else if (type == D3DDECLTYPE_SHORT4) {
        const int16_t* s = (const int16_t*)ptr;
        out.x = (float)s[0]; out.y = (float)s[1]; out.z = (float)s[2];
    } else if (type == D3DDECLTYPE_SHORT2) {
        const int16_t* s = (const int16_t*)ptr;
        out.x = (float)s[0]; out.y = (float)s[1]; out.z = 0.0f;
    }
}

static void ReadUV(const uint8_t* ptr, BYTE type, Vector2& out) {
    if (type == D3DDECLTYPE_FLOAT2) {
        const float* f = (const float*)ptr;
        out.u = f[0]; out.v = f[1];
    } else if (type == D3DDECLTYPE_FLOAT1) {
        out.u = *(const float*)ptr; out.v = 0.0f;
    } else if (type == D3DDECLTYPE_FLOAT3 || type == D3DDECLTYPE_FLOAT4) {
        const float* f = (const float*)ptr;
        out.u = f[0]; out.v = f[1];
    } else if (type == D3DDECLTYPE_FLOAT16_2) {
        const uint16_t* h = (const uint16_t*)ptr;
        out.u = HalfToFloat(h[0]);
        out.v = HalfToFloat(h[1]);
    } else if (type == D3DDECLTYPE_SHORT2N) {
        const int16_t* s = (const int16_t*)ptr;
        out.u = (float)s[0] / 32767.0f;
        out.v = (float)s[1] / 32767.0f;
    }
}

bool D3D9Capture::ExtractMeshFromStreams(const std::vector<D3DVERTEXELEMENT9>& elements,
                                        const std::vector<uint32_t>& rawIndices,
                                        const void* upVertexData, UINT upStride,
                                        ExtractedMesh& outMesh) {
    if (rawIndices.empty()) return false;

    // Find position, normal, texcoord elements
    const D3DVERTEXELEMENT9* pPosElem = nullptr;
    const D3DVERTEXELEMENT9* pNormElem = nullptr;
    const D3DVERTEXELEMENT9* pUvElem = nullptr;

    for (const auto& elem : elements) {
        if (elem.Stream == 0xFF) break;
        if (elem.Usage == D3DDECLUSAGE_POSITION && elem.UsageIndex == 0 && !pPosElem) {
            pPosElem = &elem;
        } else if (elem.Usage == D3DDECLUSAGE_NORMAL && elem.UsageIndex == 0 && !pNormElem) {
            pNormElem = &elem;
        } else if (elem.Usage == D3DDECLUSAGE_TEXCOORD && elem.UsageIndex == 0 && !pUvElem) {
            pUvElem = &elem;
        }
    }

    if (!pPosElem) return false;

    // Lock buffers for relevant streams
    const uint8_t* lockedBuffers[16] = {};
    UINT streamStrides[16] = {};
    UINT streamOffsets[16] = {};

    if (upVertexData) {
        lockedBuffers[0] = (const uint8_t*)upVertexData;
        streamStrides[0] = upStride;
        streamOffsets[0] = 0;
    } else {
        std::vector<UINT> neededStreams;
        neededStreams.push_back(pPosElem->Stream);
        if (pNormElem && pNormElem->Stream != pPosElem->Stream) neededStreams.push_back(pNormElem->Stream);
        if (pUvElem && pUvElem->Stream != pPosElem->Stream) neededStreams.push_back(pUvElem->Stream);

        for (UINT s : neededStreams) {
            if (s >= 16 || !m_Streams[s].pVB) return false;
            void* pData = nullptr;
            HRESULT hr = m_Streams[s].pVB->Lock(0, 0, &pData, D3DLOCK_READONLY);
            if (FAILED(hr) || !pData) return false;
            lockedBuffers[s] = (const uint8_t*)pData;
            streamStrides[s] = m_Streams[s].stride ? m_Streams[s].stride : upStride;
            streamOffsets[s] = m_Streams[s].offset;
        }
    }

    // Remap vertices referenced by rawIndices
    std::unordered_map<uint32_t, uint32_t> indexMap;
    outMesh.positions.clear();
    outMesh.normals.clear();
    outMesh.uvs.clear();
    outMesh.indices.clear();

    for (uint32_t idx : rawIndices) {
        auto it = indexMap.find(idx);
        if (it != indexMap.end()) {
            outMesh.indices.push_back(it->second);
        } else {
            uint32_t newIdx = (uint32_t)outMesh.positions.size();
            indexMap[idx] = newIdx;
            outMesh.indices.push_back(newIdx);

            // Read Position
            UINT posStream = pPosElem->Stream;
            const uint8_t* pPosData = lockedBuffers[posStream] + streamOffsets[posStream] + idx * streamStrides[posStream] + pPosElem->Offset;
            Vector3 pos;
            ReadVec3(pPosData, pPosElem->Type, pos);
            outMesh.positions.push_back(pos);

            // Read Normal
            if (pNormElem && lockedBuffers[pNormElem->Stream]) {
                UINT normStream = pNormElem->Stream;
                const uint8_t* pNormData = lockedBuffers[normStream] + streamOffsets[normStream] + idx * streamStrides[normStream] + pNormElem->Offset;
                Vector3 norm;
                ReadVec3(pNormData, pNormElem->Type, norm);
                outMesh.normals.push_back(norm);
            }

            // Read UV
            if (pUvElem && lockedBuffers[pUvElem->Stream]) {
                UINT uvStream = pUvElem->Stream;
                const uint8_t* pUvData = lockedBuffers[uvStream] + streamOffsets[uvStream] + idx * streamStrides[uvStream] + pUvElem->Offset;
                Vector2 uv;
                ReadUV(pUvData, pUvElem->Type, uv);
                outMesh.uvs.push_back(uv);
            }
        }
    }

    // Unlock buffers
    if (!upVertexData) {
        for (int s = 0; s < 16; ++s) {
            if (lockedBuffers[s] && m_Streams[s].pVB) {
                m_Streams[s].pVB->Unlock();
            }
        }
    }

    return true;
}

// Convert D3D primitive type and indices into clean triangle list
static void Triangulate(D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
                        const std::vector<uint32_t>& srcIndices, std::vector<uint32_t>& outTriIndices) {
    outTriIndices.clear();
    if (primitiveType == D3DPT_TRIANGLELIST) {
        size_t count = std::min(srcIndices.size(), (size_t)primitiveCount * 3);
        outTriIndices.assign(srcIndices.begin(), srcIndices.begin() + count);
    } else if (primitiveType == D3DPT_TRIANGLESTRIP) {
        if (srcIndices.size() < primitiveCount + 2) return;
        for (UINT i = 0; i < primitiveCount; ++i) {
            uint32_t a = srcIndices[i];
            uint32_t b = srcIndices[i + 1];
            uint32_t c = srcIndices[i + 2];
            // Skip degenerate / primitive restart triangles
            if (a == b || b == c || a == c) continue;
            if (a == 0xFFFF || b == 0xFFFF || c == 0xFFFF) continue;
            if (a == 0xFFFFFFFF || b == 0xFFFFFFFF || c == 0xFFFFFFFF) continue;

            if (i % 2 == 0) {
                outTriIndices.push_back(a);
                outTriIndices.push_back(b);
                outTriIndices.push_back(c);
            } else {
                outTriIndices.push_back(b);
                outTriIndices.push_back(a);
                outTriIndices.push_back(c);
            }
        }
    } else if (primitiveType == D3DPT_TRIANGLEFAN) {
        if (srcIndices.size() < primitiveCount + 2) return;
        uint32_t root = srcIndices[0];
        for (UINT i = 0; i < primitiveCount; ++i) {
            uint32_t b = srcIndices[i + 1];
            uint32_t c = srcIndices[i + 2];
            if (root == b || b == c || root == c) continue;
            outTriIndices.push_back(root);
            outTriIndices.push_back(b);
            outTriIndices.push_back(c);
        }
    }
}

void D3D9Capture::OnDrawPrimitive(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount) {
    if (!m_IsCapturing) return;
    m_DrawCallCounter++;

    if (primitiveType != D3DPT_TRIANGLELIST && primitiveType != D3DPT_TRIANGLESTRIP && primitiveType != D3DPT_TRIANGLEFAN) return;

    std::vector<D3DVERTEXELEMENT9> elements;
    UINT stride = 0;
    if (m_CurrentDecl) {
        D3DVERTEXELEMENT9 declElems[MAX_FVF_DECL_SIZE];
        UINT numElems = 0;
        if (SUCCEEDED(m_CurrentDecl->GetDeclaration(declElems, &numElems))) {
            elements.assign(declElems, declElems + numElems);
        }
    } else if (m_CurrentFVF) {
        DecodeFVF(m_CurrentFVF, elements, stride);
    }
    if (elements.empty()) return;

    // Generate sequential indices
    UINT vertexCount = 0;
    if (primitiveType == D3DPT_TRIANGLELIST) vertexCount = primitiveCount * 3;
    else if (primitiveType == D3DPT_TRIANGLESTRIP || primitiveType == D3DPT_TRIANGLEFAN) vertexCount = primitiveCount + 2;

    std::vector<uint32_t> rawIndices(vertexCount);
    for (UINT i = 0; i < vertexCount; ++i) rawIndices[i] = startVertex + i;

    std::vector<uint32_t> triIndices;
    Triangulate(primitiveType, primitiveCount, rawIndices, triIndices);

    ExtractedMesh mesh;
    if (ExtractMeshFromStreams(elements, triIndices, nullptr, stride, mesh)) {
        if (ModelFilter::ShouldKeepMesh(mesh)) {
            std::string path = m_CurrentFrameDir + "\\mesh_" + PadNumber(m_SavedMeshCounter++) + ".obj";
            ObjExporter::ExportToFile(mesh, path);
        }
    }
}

void D3D9Capture::OnDrawIndexedPrimitive(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE primitiveType, INT baseVertexIndex,
                                         UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primitiveCount) {
    if (!m_IsCapturing || !m_CurrentIB) return;
    m_DrawCallCounter++;

    if (primitiveType != D3DPT_TRIANGLELIST && primitiveType != D3DPT_TRIANGLESTRIP && primitiveType != D3DPT_TRIANGLEFAN) return;

    std::vector<D3DVERTEXELEMENT9> elements;
    UINT stride = 0;
    if (m_CurrentDecl) {
        D3DVERTEXELEMENT9 declElems[MAX_FVF_DECL_SIZE];
        UINT numElems = 0;
        if (SUCCEEDED(m_CurrentDecl->GetDeclaration(declElems, &numElems))) {
            elements.assign(declElems, declElems + numElems);
        }
    } else if (m_CurrentFVF) {
        DecodeFVF(m_CurrentFVF, elements, stride);
    }
    if (elements.empty()) return;

    D3DINDEXBUFFER_DESC ibDesc;
    m_CurrentIB->GetDesc(&ibDesc);

    void* pIdxData = nullptr;
    if (FAILED(m_CurrentIB->Lock(0, 0, &pIdxData, D3DLOCK_READONLY)) || !pIdxData) return;

    UINT indexCount = 0;
    if (primitiveType == D3DPT_TRIANGLELIST) indexCount = primitiveCount * 3;
    else if (primitiveType == D3DPT_TRIANGLESTRIP || primitiveType == D3DPT_TRIANGLEFAN) indexCount = primitiveCount + 2;

    std::vector<uint32_t> rawIndices(indexCount);
    if (ibDesc.Format == D3DFMT_INDEX16) {
        const uint16_t* p16 = (const uint16_t*)pIdxData + startIndex;
        for (UINT i = 0; i < indexCount; ++i) {
            uint16_t val = p16[i];
            rawIndices[i] = (val == 0xFFFF) ? 0xFFFFFFFF : (uint32_t)((int32_t)val + baseVertexIndex);
        }
    } else if (ibDesc.Format == D3DFMT_INDEX32) {
        const uint32_t* p32 = (const uint32_t*)pIdxData + startIndex;
        for (UINT i = 0; i < indexCount; ++i) {
            uint32_t val = p32[i];
            rawIndices[i] = (val == 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)((int32_t)val + baseVertexIndex);
        }
    }
    m_CurrentIB->Unlock();

    std::vector<uint32_t> triIndices;
    Triangulate(primitiveType, primitiveCount, rawIndices, triIndices);

    ExtractedMesh mesh;
    if (ExtractMeshFromStreams(elements, triIndices, nullptr, stride, mesh)) {
        if (ModelFilter::ShouldKeepMesh(mesh)) {
            std::string path = m_CurrentFrameDir + "\\mesh_" + PadNumber(m_SavedMeshCounter++) + ".obj";
            ObjExporter::ExportToFile(mesh, path);
        }
    }
}

void D3D9Capture::OnDrawPrimitiveUP(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
                                    const void* pVertexStreamZeroData, UINT vertexStreamZeroStride) {
    if (!m_IsCapturing || !pVertexStreamZeroData) return;
    m_DrawCallCounter++;

    if (primitiveType != D3DPT_TRIANGLELIST && primitiveType != D3DPT_TRIANGLESTRIP && primitiveType != D3DPT_TRIANGLEFAN) return;

    std::vector<D3DVERTEXELEMENT9> elements;
    UINT stride = vertexStreamZeroStride;
    if (m_CurrentDecl) {
        D3DVERTEXELEMENT9 declElems[MAX_FVF_DECL_SIZE];
        UINT numElems = 0;
        if (SUCCEEDED(m_CurrentDecl->GetDeclaration(declElems, &numElems))) {
            elements.assign(declElems, declElems + numElems);
        }
    } else if (m_CurrentFVF) {
        DecodeFVF(m_CurrentFVF, elements, stride);
    }
    if (elements.empty()) return;

    UINT vertexCount = 0;
    if (primitiveType == D3DPT_TRIANGLELIST) vertexCount = primitiveCount * 3;
    else if (primitiveType == D3DPT_TRIANGLESTRIP || primitiveType == D3DPT_TRIANGLEFAN) vertexCount = primitiveCount + 2;

    std::vector<uint32_t> rawIndices(vertexCount);
    for (UINT i = 0; i < vertexCount; ++i) rawIndices[i] = i;

    std::vector<uint32_t> triIndices;
    Triangulate(primitiveType, primitiveCount, rawIndices, triIndices);

    ExtractedMesh mesh;
    if (ExtractMeshFromStreams(elements, triIndices, pVertexStreamZeroData, vertexStreamZeroStride, mesh)) {
        if (ModelFilter::ShouldKeepMesh(mesh)) {
            std::string path = m_CurrentFrameDir + "\\mesh_" + PadNumber(m_SavedMeshCounter++) + ".obj";
            ObjExporter::ExportToFile(mesh, path);
        }
    }
}

void D3D9Capture::OnDrawIndexedPrimitiveUP(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE primitiveType, UINT minVertexIndex,
                                           UINT numVertices, UINT primitiveCount, const void* pIndexData,
                                           D3DFORMAT indexDataFormat, const void* pVertexStreamZeroData,
                                           UINT vertexStreamZeroStride) {
    if (!m_IsCapturing || !pIndexData || !pVertexStreamZeroData) return;
    m_DrawCallCounter++;

    if (primitiveType != D3DPT_TRIANGLELIST && primitiveType != D3DPT_TRIANGLESTRIP && primitiveType != D3DPT_TRIANGLEFAN) return;

    std::vector<D3DVERTEXELEMENT9> elements;
    UINT stride = vertexStreamZeroStride;
    if (m_CurrentDecl) {
        D3DVERTEXELEMENT9 declElems[MAX_FVF_DECL_SIZE];
        UINT numElems = 0;
        if (SUCCEEDED(m_CurrentDecl->GetDeclaration(declElems, &numElems))) {
            elements.assign(declElems, declElems + numElems);
        }
    } else if (m_CurrentFVF) {
        DecodeFVF(m_CurrentFVF, elements, stride);
    }
    if (elements.empty()) return;

    UINT indexCount = 0;
    if (primitiveType == D3DPT_TRIANGLELIST) indexCount = primitiveCount * 3;
    else if (primitiveType == D3DPT_TRIANGLESTRIP || primitiveType == D3DPT_TRIANGLEFAN) indexCount = primitiveCount + 2;

    std::vector<uint32_t> rawIndices(indexCount);
    if (indexDataFormat == D3DFMT_INDEX16) {
        const uint16_t* p16 = (const uint16_t*)pIndexData;
        for (UINT i = 0; i < indexCount; ++i) rawIndices[i] = p16[i];
    } else if (indexDataFormat == D3DFMT_INDEX32) {
        const uint32_t* p32 = (const uint32_t*)pIndexData;
        for (UINT i = 0; i < indexCount; ++i) rawIndices[i] = p32[i];
    }

    std::vector<uint32_t> triIndices;
    Triangulate(primitiveType, primitiveCount, rawIndices, triIndices);

    ExtractedMesh mesh;
    if (ExtractMeshFromStreams(elements, triIndices, pVertexStreamZeroData, vertexStreamZeroStride, mesh)) {
        if (ModelFilter::ShouldKeepMesh(mesh)) {
            std::string path = m_CurrentFrameDir + "\\mesh_" + PadNumber(m_SavedMeshCounter++) + ".obj";
            ObjExporter::ExportToFile(mesh, path);
        }
    }
}

} // namespace D3D9
} // namespace OpenRipper
