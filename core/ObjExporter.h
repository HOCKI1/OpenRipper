#pragma once
#include "VertexTypes.h"
#include <string>

namespace OpenRipper {

struct ObjExportOptions {
    bool flipV = true;          // Invert V coordinate (1.0 - v) for DirectX/Vulkan -> Blender convention
    bool writeNormals = true;
    bool writeUVs = true;
};

class ObjExporter {
public:
    static bool ExportToFile(const ExtractedMesh& mesh, const std::string& filepath, const ObjExportOptions& options = {});
};

} // namespace OpenRipper
