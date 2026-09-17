#pragma once
#include <windows.h>
#include <d3d9.h>
#include <string>
#include <vector>
#include <cstdint>
#include "VertexTypes.h"
#include "ModelFilter.h"
#include "ObjExporter.h"

#ifndef MAX_FVF_DECL_SIZE
#define MAX_FVF_DECL_SIZE (MAXD3DDECLLENGTH + 1)
#endif

namespace OpenRipper {
namespace D3D9 {

struct StreamBinding {
    IDirect3DVertexBuffer9* pVB = nullptr;
    UINT offset = 0;
    UINT stride = 0;
};

class D3D9Capture {
public:
    static D3D9Capture& Get();

    void Init(const std::string& outputDir);
    void StartFrameCapture(uint32_t frameNumber);
    void EndFrameCapture();
    bool IsCapturing() const { return m_IsCapturing; }

    // State trackers
    void OnSetStreamSource(UINT streamNumber, IDirect3DVertexBuffer9* pStreamData, UINT offsetInBytes, UINT stride);
    void OnSetIndices(IDirect3DIndexBuffer9* pIndexData);
    void OnSetFVF(DWORD fvf);
    void OnSetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl);

    // Draw call captures
    void OnDrawPrimitive(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount);
    void OnDrawIndexedPrimitive(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE primitiveType, INT baseVertexIndex, UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primitiveCount);
    void OnDrawPrimitiveUP(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE primitiveType, UINT primitiveCount, const void* pVertexStreamZeroData, UINT vertexStreamZeroStride);
    void OnDrawIndexedPrimitiveUP(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE primitiveType, UINT minVertexIndex, UINT numVertices, UINT primitiveCount, const void* pIndexData, D3DFORMAT indexDataFormat, const void* pVertexStreamZeroData, UINT vertexStreamZeroStride);

private:
    D3D9Capture() = default;

    bool DecodeFVF(DWORD fvf, std::vector<D3DVERTEXELEMENT9>& elements, UINT& outStride);
    bool ExtractMeshFromStreams(const std::vector<D3DVERTEXELEMENT9>& elements,
                                const std::vector<uint32_t>& rawIndices,
                                const void* upVertexData, UINT upStride,
                                ExtractedMesh& outMesh);

    bool m_IsCapturing = false;
    std::string m_OutputDir = "C:\\OpenRipperDumps";
    std::string m_CurrentFrameDir;
    uint32_t m_FrameNumber = 0;
    uint32_t m_DrawCallCounter = 0;
    uint32_t m_SavedMeshCounter = 0;

    StreamBinding m_Streams[16] = {};
    IDirect3DIndexBuffer9* m_CurrentIB = nullptr;
    IDirect3DVertexDeclaration9* m_CurrentDecl = nullptr;
    DWORD m_CurrentFVF = 0;
};

} // namespace D3D9
} // namespace OpenRipper
