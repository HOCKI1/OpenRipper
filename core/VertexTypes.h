#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace OpenRipper {

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vector2 {
    float u = 0.0f;
    float v = 0.0f;
};

struct Vector4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

enum class AttributeSemantic {
    Position,
    TexCoord,
    Normal,
    Color,
    Tangent,
    Unknown
};

enum class AttributeFormat {
    Float1,
    Float2,
    Float3,
    Float4,
    Short2_Norm,
    Short4_Norm,
    Byte4_Norm,
    UByte4_Norm,
    Unknown
};

struct VertexAttribute {
    AttributeSemantic semantic = AttributeSemantic::Unknown;
    AttributeFormat format = AttributeFormat::Unknown;
    uint32_t offset = 0;
    uint32_t binding = 0;
    uint32_t location = 0;
};

struct VertexLayout {
    uint32_t stride = 0;
    std::vector<VertexAttribute> attributes;

    const VertexAttribute* FindSemantic(AttributeSemantic sem) const {
        for (const auto& attr : attributes) {
            if (attr.semantic == sem) return &attr;
        }
        return nullptr;
    }
};

struct ExtractedMesh {
    std::string name;
    uint32_t drawCallId = 0;
    uint32_t pipelineId = 0;

    std::vector<Vector3> positions;
    std::vector<Vector2> uvs;
    std::vector<Vector3> normals;
    std::vector<Vector4> colors;
    std::vector<uint32_t> indices; // Always normalized to 32-bit indices

    bool HasPositions() const { return !positions.empty(); }
    bool HasUVs() const { return !uvs.empty() && uvs.size() == positions.size(); }
    bool HasNormals() const { return !normals.empty() && normals.size() == positions.size(); }
    bool HasColors() const { return !colors.empty() && colors.size() == positions.size(); }
};

} // namespace OpenRipper
