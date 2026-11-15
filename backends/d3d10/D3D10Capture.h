#pragma once
#include <windows.h>
#include <d3d10.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "VertexTypes.h"
#include "ModelFilter.h"
#include "ObjExporter.h"

namespace OpenRipper {
namespace D3D10 {

struct VertexBufferBinding {
    ID3D10Buffer* pBuffer = nullptr;
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

class D3D10Capture {
public:
    static D3D10Capture& Get();

    void Init(const std::string& outputDir);
    void StartFrameCapture(uint32_t frameNumber);
    void EndFrameCapture();
    bool IsCapturing() const { return m_IsCapturing; }

    // State tracking
    void RegisterInputLayout(ID3D10InputLayout* pLayout, const D3D10_INPUT_ELEMENT_DESC* pDescs, UINT numElements);
    void OnSetInputLayout(ID3D10InputLayout* pLayout);
    void OnSetVertexBuffers(UINT startSlot, UINT numBuffers, ID3D10Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets);
    void OnSetIndexBuffer(ID3D10Buffer* pIndexBuffer, DXGI_FORMAT format, UINT offset);
    void OnSetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY topology);

    // Draw call captures
    void OnDraw(ID3D10Device* pDevice, UINT vertexCount, UINT startVertexLocation);
    void OnDrawIndexed(ID3D10Device* pDevice, UINT indexCount, UINT startIndexLocation, INT baseVertexLocation);
    void OnDrawInstanced(ID3D10Device* pDevice, UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation);
    void OnDrawIndexedInstanced(ID3D10Device* pDevice, UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation);

private:
    D3D10Capture() = default;

    ID3D10Buffer* GetOrCreateStagingBuffer(ID3D10Device* pDevice, UINT sizeInBytes);
    bool ReadBufferData(ID3D10Device* pDevice, ID3D10Buffer* pGpuBuffer, UINT byteOffset, UINT byteLength, std::vector<uint8_t>& outData);

    bool ExtractMesh(ID3D10Device* pDevice,
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
    ID3D10InputLayout* m_CurrentLayout = nullptr;
    VertexBufferBinding m_VertexBuffers[16] = {};
    ID3D10Buffer* m_CurrentIB = nullptr;
    DXGI_FORMAT m_IndexFormat = DXGI_FORMAT_UNKNOWN;
    UINT m_IndexOffset = 0;
    D3D10_PRIMITIVE_TOPOLOGY m_Topology = D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED;

    // Layout descriptor cache
    std::unordered_map<ID3D10InputLayout*, std::vector<LayoutElement>> m_LayoutMap;

    // Staging buffers cache: size -> buffer
    std::vector<std::pair<UINT, ID3D10Buffer*>> m_StagingBuffers;
};

} // namespace D3D10
} // namespace OpenRipper
