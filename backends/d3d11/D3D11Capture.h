#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "VertexTypes.h"
#include "ModelFilter.h"
#include "ObjExporter.h"

namespace OpenRipper {
namespace D3D11 {

struct VertexBufferBinding {
    ID3D11Buffer* pBuffer = nullptr;
    UINT stride = 0;
    UINT offset = 0;
};

struct LayoutElement {
    std::string semanticName;
    UINT semanticIndex = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT inputSlot = 0;
    UINT alignedByteOffset = 0;
};

class D3D11Capture {
public:
    static D3D11Capture& Get();

    void Init(const std::string& outputDir);
    void StartFrameCapture(uint32_t frameNumber);
    void EndFrameCapture();
    bool IsCapturing() const { return m_IsCapturing; }

    // State tracking
    void RegisterInputLayout(ID3D11InputLayout* pLayout, const D3D11_INPUT_ELEMENT_DESC* pDescs, UINT numElements);
    void OnSetInputLayout(ID3D11InputLayout* pLayout);
    void OnSetVertexBuffers(UINT startSlot, UINT numBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets);
    void OnSetIndexBuffer(ID3D11Buffer* pIndexBuffer, DXGI_FORMAT format, UINT offset);
    void OnSetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology);

    // Draw call captures
    void OnDraw(ID3D11DeviceContext* pContext, UINT vertexCount, UINT startVertexLocation);
    void OnDrawIndexed(ID3D11DeviceContext* pContext, UINT indexCount, UINT startIndexLocation, INT baseVertexLocation);
    void OnDrawInstanced(ID3D11DeviceContext* pContext, UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation);
    void OnDrawIndexedInstanced(ID3D11DeviceContext* pContext, UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation);

private:
    D3D11Capture() = default;

    ID3D11Buffer* GetOrCreateStagingBuffer(ID3D11Device* pDevice, UINT sizeInBytes);
    bool ReadBufferData(ID3D11DeviceContext* pContext, ID3D11Buffer* pGpuBuffer, UINT byteOffset, UINT byteLength, std::vector<uint8_t>& outData);

    bool ExtractMesh(ID3D11DeviceContext* pContext,
                     const std::vector<uint32_t>& indices,
                     INT baseVertexLocation,
                     ExtractedMesh& outMesh);

    bool m_IsCapturing = false;
    std::string m_OutputDir = "C:\\OpenRipperDumps";
    std::string m_CurrentFrameDir;
    uint32_t m_FrameNumber = 0;
    uint32_t m_DrawCallCounter = 0;
    uint32_t m_SavedMeshCounter = 0;

    // Current Pipeline State
    ID3D11InputLayout* m_CurrentLayout = nullptr;
    VertexBufferBinding m_VertexBuffers[16] = {};
    ID3D11Buffer* m_CurrentIB = nullptr;
    DXGI_FORMAT m_IndexFormat = DXGI_FORMAT_UNKNOWN;
    UINT m_IndexOffset = 0;
    D3D11_PRIMITIVE_TOPOLOGY m_Topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

    // Layout descriptor cache
    std::unordered_map<ID3D11InputLayout*, std::vector<LayoutElement>> m_LayoutMap;

    // Staging buffers cache: size -> buffer
    std::vector<std::pair<UINT, ID3D11Buffer*>> m_StagingBuffers;
};

} // namespace D3D11
} // namespace OpenRipper
