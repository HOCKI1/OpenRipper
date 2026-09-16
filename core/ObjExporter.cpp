#include "ObjExporter.h"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace OpenRipper {

bool ObjExporter::ExportToFile(const ExtractedMesh& mesh, const std::string& filepath, const ObjExportOptions& options) {
    if (mesh.positions.empty()) {
        return false;
    }

    std::ofstream file(filepath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << "# OpenRipper Geometry Dump\n";
    file << "# Model: " << mesh.name << "\n";
    file << "# DrawCall ID: " << mesh.drawCallId << "\n";
    file << "# Pipeline ID: " << mesh.pipelineId << "\n";
    file << "# Vertices: " << mesh.positions.size() << "\n";
    file << "# Indices: " << mesh.indices.size() << "\n\n";

    file << "o " << mesh.name << "\n\n";

    // Set floating point precision
    file << std::fixed << std::setprecision(6);

    // 1. Write positions (v)
    for (const auto& pos : mesh.positions) {
        file << "v " << pos.x << " " << pos.y << " " << pos.z << "\n";
    }
    file << "\n";

    // 2. Write UVs (vt)
    bool hasUVs = options.writeUVs && mesh.HasUVs();
    if (hasUVs) {
        for (const auto& uv : mesh.uvs) {
            float v = options.flipV ? (1.0f - uv.v) : uv.v;
            file << "vt " << uv.u << " " << v << "\n";
        }
        file << "\n";
    }

    // 3. Write Normals (vn)
    bool hasNormals = options.writeNormals && mesh.HasNormals();
    if (hasNormals) {
        for (const auto& n : mesh.normals) {
            file << "vn " << n.x << " " << n.y << " " << n.z << "\n";
        }
        file << "\n";
    }

    // 4. Write Faces (f)
    file << "s 1\n";

    auto writeVertexIndex = [&](uint32_t idx) {
        // OBJ indices are 1-based
        uint32_t oneBased = idx + 1;
        file << oneBased;
        if (hasUVs || hasNormals) {
            file << "/";
            if (hasUVs) file << oneBased;
            if (hasNormals) {
                file << "/" << oneBased;
            }
        }
    };

    if (!mesh.indices.empty()) {
        // Indexed triangles
        size_t triCount = mesh.indices.size() / 3;
        for (size_t i = 0; i < triCount; ++i) {
            file << "f ";
            writeVertexIndex(mesh.indices[i * 3 + 0]);
            file << " ";
            writeVertexIndex(mesh.indices[i * 3 + 1]);
            file << " ";
            writeVertexIndex(mesh.indices[i * 3 + 2]);
            file << "\n";
        }
    } else {
        // Non-indexed triangle list
        size_t triCount = mesh.positions.size() / 3;
        for (size_t i = 0; i < triCount; ++i) {
            file << "f ";
            writeVertexIndex(static_cast<uint32_t>(i * 3 + 0));
            file << " ";
            writeVertexIndex(static_cast<uint32_t>(i * 3 + 1));
            file << " ";
            writeVertexIndex(static_cast<uint32_t>(i * 3 + 2));
            file << "\n";
        }
    }

    file.close();
    return true;
}

} // namespace OpenRipper
