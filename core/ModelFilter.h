#pragma once
#include "VertexTypes.h"
#include <cmath>
#include <algorithm>

namespace OpenRipper {

struct FilterSettings {
    uint32_t minVertexCount = 8;
    uint32_t minIndexCount = 12;
    bool filterFlat2DQuads = true;
    bool filterScreenSpaceUI = true;
    float minDepthSpan = 0.0001f; // If maxZ - minZ < minDepthSpan, likely a 2D HUD / UI quad
};

class ModelFilter {
public:
    static bool ShouldKeepMesh(const ExtractedMesh& mesh, const FilterSettings& settings = {}, std::string* rejectReason = nullptr) {
        // 1. Basic vertex and index threshold
        if (mesh.positions.size() < settings.minVertexCount) {
            if (rejectReason) *rejectReason = "vertexCount < " + std::to_string(settings.minVertexCount);
            return false;
        }

        if (!mesh.indices.empty() && mesh.indices.size() < settings.minIndexCount) {
            if (rejectReason) *rejectReason = "indexCount < " + std::to_string(settings.minIndexCount);
            return false;
        }

        // 2. Check for invalid floating point coordinates (inf, nan, or corrupt memory overflow)
        size_t badCount = 0;
        for (const auto& pos : mesh.positions) {
            if (std::isnan(pos.x) || std::isnan(pos.y) || std::isnan(pos.z) ||
                std::isinf(pos.x) || std::isinf(pos.y) || std::isinf(pos.z) ||
                std::abs(pos.x) > 1e7f || std::abs(pos.y) > 1e7f || std::abs(pos.z) > 1e7f) {
                badCount++;
            }
        }
        if (badCount > mesh.positions.size() / 4) {
            if (rejectReason) *rejectReason = "Too many corrupt coordinates (" + std::to_string(badCount) + "/" + std::to_string(mesh.positions.size()) + ")";
            return false;
        }

        // 3. Filter obvious 2D quads (e.g. 4 vertices, 6 indices)
        if (settings.filterFlat2DQuads) {
            if (mesh.positions.size() == 4 && (mesh.indices.size() == 6 || mesh.indices.empty())) {
                if (rejectReason) *rejectReason = "Single 4-vert 2D quad";
                return false;
            }
        }

        // 4. Filter strictly flat 2D HUD/UI quads (only if small vertex count <= 8)
        // Large meshes with constant Z are legitimate horizontal surfaces (floors, roofs, water in Z-up engines)
        if (settings.filterScreenSpaceUI && mesh.positions.size() >= 3 && mesh.positions.size() <= 8) {
            float minZ = mesh.positions[0].z, maxZ = mesh.positions[0].z;
            float minX = mesh.positions[0].x, maxX = mesh.positions[0].x;
            float minY = mesh.positions[0].y, maxY = mesh.positions[0].y;

            for (const auto& pos : mesh.positions) {
                minZ = std::min(minZ, pos.z); maxZ = std::max(maxZ, pos.z);
                minX = std::min(minX, pos.x); maxX = std::max(maxX, pos.x);
                minY = std::min(minY, pos.y); maxY = std::max(maxY, pos.y);
            }

            if (std::abs(maxZ - minZ) < settings.minDepthSpan &&
                (minX >= -1.0f && maxX <= 1920.0f) && (minY >= -1.0f && maxY <= 1080.0f)) {
                if (rejectReason) *rejectReason = "Screen-space 2D HUD quad (verts <= 8, flat Z)";
                return false;
            }
        }

        return true;
    }
};

} // namespace OpenRipper
