#include "OpenGLCapture.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <direct.h>
#include <cmath>

namespace OpenRipper {
namespace OpenGL {

static void LogGL(const std::string& msg) {
    static std::ofstream logFile("C:\\OpenRipperDumps\\opengl_debug.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << "[OpenRipper GL] " << msg << std::endl;
        logFile.flush();
    }
}

// Runtime OpenGL procs
static PFNGLGETBUFFERSUBDATAPROC fp_glGetBufferSubData = nullptr;
static PFNGLGETBUFFERPARAMETERIVPROC fp_glGetBufferParameteriv = nullptr;
static PFNGLGETVERTEXATTRIBIVPROC fp_glGetVertexAttribiv = nullptr;
static PFNGLGETVERTEXATTRIBPOINTERVPROC fp_glGetVertexAttribPointerv = nullptr;
static PFNGLBINDBUFFERPROC fp_glBindBuffer = nullptr;
static PFNGLBINDVERTEXARRAYPROC fp_glBindVertexArray = nullptr;

void OpenGLCapture::EnsureGLProcsLoaded() {
    if (!fp_glGetBufferSubData) {
        fp_glGetBufferSubData = (PFNGLGETBUFFERSUBDATAPROC)wglGetProcAddress("glGetBufferSubData");
    }
    if (!fp_glGetBufferParameteriv) {
        fp_glGetBufferParameteriv = (PFNGLGETBUFFERPARAMETERIVPROC)wglGetProcAddress("glGetBufferParameteriv");
    }
    if (!fp_glGetVertexAttribiv) {
        fp_glGetVertexAttribiv = (PFNGLGETVERTEXATTRIBIVPROC)wglGetProcAddress("glGetVertexAttribiv");
    }
    if (!fp_glGetVertexAttribPointerv) {
        fp_glGetVertexAttribPointerv = (PFNGLGETVERTEXATTRIBPOINTERVPROC)wglGetProcAddress("glGetVertexAttribPointerv");
    }
    if (!fp_glBindBuffer) {
        fp_glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    }
    if (!fp_glBindVertexArray) {
        fp_glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
    }
}

OpenGLCapture& OpenGLCapture::Get() {
    static OpenGLCapture instance;
    return instance;
}

void OpenGLCapture::Init(const std::string& outputDir) {
    m_OutputDir = outputDir;
    m_DefaultAttribs.resize(16);
    LogGL("Initialized with OutputDir=" + outputDir);
}

void OpenGLCapture::StartFrameCapture(uint32_t frameNumber) {
    m_IsCapturing = true;
    m_CurrentFrame = frameNumber;
    m_DrawCallCounter = 0;
    EnsureGLProcsLoaded();
    LogGL("StartFrameCapture #" + std::to_string(frameNumber));
}

void OpenGLCapture::EndFrameCapture() {
    if (m_IsCapturing) {
        LogGL("EndFrameCapture: Total captured draw calls = " + std::to_string(m_DrawCallCounter));
    }
    m_IsCapturing = false;
}

void OpenGLCapture::OnBindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER) {
        m_CurrentVBO = buffer;
    } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
        m_CurrentIBO = buffer;
    }
}

void OpenGLCapture::OnBindVertexArray(GLuint array) {
    m_CurrentVAO = array;
}

std::vector<GLVertexAttrib>& OpenGLCapture::GetCurrentAttribs() {
    if (m_CurrentVAO == 0) {
        if (m_DefaultAttribs.size() < 16) m_DefaultAttribs.resize(16);
        return m_DefaultAttribs;
    }
    auto& attribs = m_VaoAttribs[m_CurrentVAO];
    if (attribs.size() < 16) {
        attribs.resize(16);
    }
    return attribs;
}

void OpenGLCapture::OnVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    if (index >= 32) return;
    auto& attribs = GetCurrentAttribs();
    if (index >= attribs.size()) attribs.resize(index + 1);

    GLVertexAttrib& a = attribs[index];
    a.size = size;
    a.type = type;
    a.normalized = normalized;
    a.stride = stride;
    a.pointer = pointer;
    a.bufferBinding = m_CurrentVBO;
}

void OpenGLCapture::OnEnableVertexAttribArray(GLuint index) {
    auto& attribs = GetCurrentAttribs();
    if (index < attribs.size()) {
        attribs[index].enabled = true;
    }
}

void OpenGLCapture::OnDisableVertexAttribArray(GLuint index) {
    auto& attribs = GetCurrentAttribs();
    if (index < attribs.size()) {
        attribs[index].enabled = false;
    }
}

bool OpenGLCapture::ReadBufferData(GLuint bufferId, GLintptr byteOffset, GLsizeiptr byteLength, std::vector<uint8_t>& outData) {
    if (bufferId == 0 || byteLength <= 0) return false;
    EnsureGLProcsLoaded();
    if (!fp_glGetBufferSubData || !fp_glBindBuffer || !fp_glGetBufferParameteriv) {
        LogGL("Error: glGetBufferSubData or related functions not available");
        return false;
    }

    // Save previous binding
    GLint prevBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuffer);

    fp_glBindBuffer(GL_ARRAY_BUFFER, bufferId);

    GLint bufSize = 0;
    fp_glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufSize);
    if (byteOffset + byteLength > bufSize) {
        if (byteOffset < bufSize) {
            byteLength = bufSize - byteOffset;
        } else {
            fp_glBindBuffer(GL_ARRAY_BUFFER, prevBuffer);
            return false;
        }
    }

    outData.resize(byteLength);
    fp_glGetBufferSubData(GL_ARRAY_BUFFER, byteOffset, byteLength, outData.data());

    fp_glBindBuffer(GL_ARRAY_BUFFER, prevBuffer);
    return true;
}

void OpenGLCapture::OnDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (!m_IsCapturing || count <= 0) return;
    std::vector<uint32_t> indices;
    indices.reserve(count);
    for (GLsizei i = 0; i < count; ++i) {
        indices.push_back((uint32_t)(first + i));
    }
    ExtractMesh(mode, indices, 0);
}

void OpenGLCapture::OnDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint baseVertex) {
    if (!m_IsCapturing || count <= 0) return;
    EnsureGLProcsLoaded();

    GLint boundIBO = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundIBO);

    size_t indexSize = (type == GL_UNSIGNED_INT) ? 4 : (type == GL_UNSIGNED_SHORT ? 2 : 1);
    size_t byteLength = count * indexSize;
    std::vector<uint8_t> rawIndices;

    if (boundIBO != 0) {
        uintptr_t offset = (uintptr_t)indices;
        if (!ReadBufferData(boundIBO, (GLintptr)offset, (GLsizeiptr)byteLength, rawIndices)) {
            LogGL("Failed to read IBO data (id=" + std::to_string(boundIBO) + ")");
            return;
        }
    } else {
        if (!indices) return;
        rawIndices.resize(byteLength);
        memcpy(rawIndices.data(), indices, byteLength);
    }

    std::vector<uint32_t> indexList;
    indexList.reserve(count);

    if (type == GL_UNSIGNED_INT) {
        const uint32_t* ptr = reinterpret_cast<const uint32_t*>(rawIndices.data());
        for (GLsizei i = 0; i < count; ++i) indexList.push_back(ptr[i]);
    } else if (type == GL_UNSIGNED_SHORT) {
        const uint16_t* ptr = reinterpret_cast<const uint16_t*>(rawIndices.data());
        for (GLsizei i = 0; i < count; ++i) indexList.push_back(ptr[i]);
    } else {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(rawIndices.data());
        for (GLsizei i = 0; i < count; ++i) indexList.push_back(ptr[i]);
    }

    ExtractMesh(mode, indexList, baseVertex);
}

bool OpenGLCapture::ExtractMesh(GLenum mode, const std::vector<uint32_t>& indices, GLint baseVertex) {
    if (indices.empty()) return false;
    EnsureGLProcsLoaded();

    auto& attribs = GetCurrentAttribs();

    // Query active state from driver if attribs table is empty or attribute 0 is not recorded
    int posIndex = 0;
    GLVertexAttrib posAttrib;
    if (posIndex < (int)attribs.size() && attribs[posIndex].enabled) {
        posAttrib = attribs[posIndex];
    } else if (fp_glGetVertexAttribiv && fp_glGetVertexAttribPointerv) {
        GLint enabled = 0;
        fp_glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        if (enabled) {
            posAttrib.enabled = true;
            fp_glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &posAttrib.size);
            fp_glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, (GLint*)&posAttrib.type);
            fp_glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &posAttrib.stride);
            fp_glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, (GLint*)&posAttrib.bufferBinding);
            fp_glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, (void**)&posAttrib.pointer);
        }
    }

    // Fallback: If no modern attribute 0, check fixed-function GL_VERTEX_ARRAY
    if (!posAttrib.enabled) {
        GLint vEnabled = 0;
        glGetIntegerv(GL_VERTEX_ARRAY, &vEnabled);
        if (vEnabled) {
            posAttrib.enabled = true;
            glGetIntegerv(GL_VERTEX_ARRAY_SIZE, &posAttrib.size);
            glGetIntegerv(GL_VERTEX_ARRAY_TYPE, (GLint*)&posAttrib.type);
            glGetIntegerv(GL_VERTEX_ARRAY_STRIDE, &posAttrib.stride);
            glGetPointerv(GL_VERTEX_ARRAY_POINTER, (void**)&posAttrib.pointer);
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, (GLint*)&posAttrib.bufferBinding);
        }
    }

    if (!posAttrib.enabled && posAttrib.bufferBinding == 0 && !posAttrib.pointer) {
        // Fallback: try active GL_ARRAY_BUFFER
        GLint boundVBO = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &boundVBO);
        if (boundVBO != 0) {
            posAttrib.enabled = true;
            posAttrib.bufferBinding = boundVBO;
            posAttrib.size = 3;
            posAttrib.type = GL_FLOAT;
            posAttrib.stride = 32;
            posAttrib.pointer = 0;
        } else {
            return false;
        }
    }

    if (posAttrib.size <= 0) posAttrib.size = 3;
    if (posAttrib.stride <= 0) posAttrib.stride = posAttrib.size * sizeof(float);

    uint32_t minIdx = indices[0];
    uint32_t maxIdx = indices[0];
    for (uint32_t idx : indices) {
        if (idx < minIdx) minIdx = idx;
        if (idx > maxIdx) maxIdx = idx;
    }

    uint32_t effectiveMax = maxIdx + (baseVertex > 0 ? baseVertex : 0);
    size_t totalBytesNeeded = (effectiveMax + 1) * posAttrib.stride;

    std::vector<uint8_t> vboData;
    const uint8_t* basePtr = nullptr;

    if (posAttrib.bufferBinding != 0) {
        if (!ReadBufferData(posAttrib.bufferBinding, 0, totalBytesNeeded, vboData)) {
            LogGL("Failed to read VBO data (id=" + std::to_string(posAttrib.bufferBinding) + ")");
            return false;
        }
        basePtr = vboData.data();
    } else {
        basePtr = (const uint8_t*)posAttrib.pointer;
    }

    if (!basePtr) return false;

    // Check optional normal / uv attribs
    bool hasNormal = (posAttrib.stride >= 24);
    size_t normalOffset = 12;
    bool hasUV = (posAttrib.stride >= 32);
    size_t uvOffset = 24;

    // Check if separate attribs were specified
    if (attribs.size() > 1 && attribs[1].enabled && attribs[1].bufferBinding == posAttrib.bufferBinding) {
        hasNormal = true;
        normalOffset = (uintptr_t)attribs[1].pointer;
    }
    if (attribs.size() > 2 && attribs[2].enabled && attribs[2].bufferBinding == posAttrib.bufferBinding) {
        hasUV = true;
        uvOffset = (uintptr_t)attribs[2].pointer;
    }

    ExtractedMesh mesh;
    mesh.drawCallId = m_DrawCallCounter++;
    mesh.pipelineId = (uint32_t)m_CurrentVAO;

    std::unordered_map<uint32_t, uint32_t> indexRemap;
    mesh.positions.reserve(maxIdx - minIdx + 1);

    auto ProcessVertex = [&](uint32_t rawIdx) -> uint32_t {
        int32_t actualIdx = (int32_t)rawIdx + baseVertex;
        if (actualIdx < 0) actualIdx = 0;

        auto it = indexRemap.find((uint32_t)actualIdx);
        if (it != indexRemap.end()) return it->second;

        size_t byteOffset = (uintptr_t)posAttrib.pointer + (size_t)actualIdx * posAttrib.stride;
        if (posAttrib.bufferBinding != 0 && byteOffset + sizeof(float)*3 > vboData.size()) {
            return 0;
        }

        const uint8_t* vPtr = basePtr + byteOffset;
        const float* fPos = reinterpret_cast<const float*>(vPtr);

        Vector3 pos{};
        pos.x = fPos[0];
        pos.y = (posAttrib.size > 1) ? fPos[1] : 0.0f;
        pos.z = (posAttrib.size > 2) ? fPos[2] : 0.0f;
        mesh.positions.push_back(pos);

        if (hasNormal) {
            size_t nByteOffset = normalOffset + (size_t)actualIdx * posAttrib.stride;
            if (posAttrib.bufferBinding == 0 || nByteOffset + sizeof(float)*3 <= vboData.size()) {
                const float* fNorm = reinterpret_cast<const float*>(basePtr + nByteOffset);
                mesh.normals.push_back({fNorm[0], fNorm[1], fNorm[2]});
            } else {
                mesh.normals.push_back({0.0f, 1.0f, 0.0f});
            }
        }

        if (hasUV) {
            size_t uvByteOffset = uvOffset + (size_t)actualIdx * posAttrib.stride;
            if (posAttrib.bufferBinding == 0 || uvByteOffset + sizeof(float)*2 <= vboData.size()) {
                const float* fUV = reinterpret_cast<const float*>(basePtr + uvByteOffset);
                mesh.uvs.push_back({fUV[0], 1.0f - fUV[1]});
            } else {
                mesh.uvs.push_back({0.0f, 0.0f});
            }
        }

        uint32_t newIdx = (uint32_t)mesh.positions.size() - 1;
        indexRemap[(uint32_t)actualIdx] = newIdx;
        return newIdx;
    };

    // Triangulation
    if (mode == GL_TRIANGLES) {
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            uint32_t i0 = ProcessVertex(indices[i]);
            uint32_t i1 = ProcessVertex(indices[i + 1]);
            uint32_t i2 = ProcessVertex(indices[i + 2]);
            if (i0 != i1 && i1 != i2 && i0 != i2) {
                mesh.indices.push_back(i0);
                mesh.indices.push_back(i1);
                mesh.indices.push_back(i2);
            }
        }
    } else if (mode == GL_TRIANGLE_STRIP) {
        for (size_t i = 0; i + 2 < indices.size(); ++i) {
            uint32_t raw0 = indices[i];
            uint32_t raw1 = indices[i + 1];
            uint32_t raw2 = indices[i + 2];
            if (raw0 == raw1 || raw1 == raw2 || raw0 == raw2) continue;

            uint32_t i0 = ProcessVertex(raw0);
            uint32_t i1 = ProcessVertex(raw1);
            uint32_t i2 = ProcessVertex(raw2);

            if (i % 2 == 0) {
                mesh.indices.push_back(i0);
                mesh.indices.push_back(i1);
                mesh.indices.push_back(i2);
            } else {
                mesh.indices.push_back(i0);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i1);
            }
        }
    } else if (mode == GL_TRIANGLE_FAN) {
        if (indices.size() >= 3) {
            uint32_t i0 = ProcessVertex(indices[0]);
            for (size_t i = 1; i + 1 < indices.size(); ++i) {
                uint32_t i1 = ProcessVertex(indices[i]);
                uint32_t i2 = ProcessVertex(indices[i + 1]);
                if (i0 != i1 && i1 != i2 && i0 != i2) {
                    mesh.indices.push_back(i0);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i2);
                }
            }
        }
    } else if (mode == GL_QUADS) {
        for (size_t i = 0; i + 3 < indices.size(); i += 4) {
            uint32_t i0 = ProcessVertex(indices[i]);
            uint32_t i1 = ProcessVertex(indices[i + 1]);
            uint32_t i2 = ProcessVertex(indices[i + 2]);
            uint32_t i3 = ProcessVertex(indices[i + 3]);
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
        }
    }

    if (!ModelFilter::ShouldKeepMesh(mesh)) {
        return false;
    }

    char frameFolder[64];
    sprintf_s(frameFolder, "Frame_%04u", m_CurrentFrame);
    std::string outDir = m_OutputDir + "\\" + frameFolder;
    _mkdir(m_OutputDir.c_str());
    _mkdir(outDir.c_str());

    char meshName[64];
    sprintf_s(meshName, "mesh_%04u.obj", mesh.drawCallId);
    std::string outPath = outDir + "\\" + meshName;

    bool res = ObjExporter::ExportToFile(mesh, outPath);
    LogGL("Dumped " + outPath + " (Vertices=" + std::to_string(mesh.positions.size()) + ", Indices=" + std::to_string(mesh.indices.size()) + ") result: " + std::to_string(res));
    return res;
}

} // namespace OpenGL
} // namespace OpenRipper
