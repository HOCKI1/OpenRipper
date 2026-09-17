#pragma once
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "ModelFilter.h"
#include "ObjExporter.h"

namespace OpenRipper {
namespace OpenGL {

struct GLVertexAttrib {
    bool enabled = false;
    GLint size = 0;
    GLenum type = GL_FLOAT;
    GLboolean normalized = GL_FALSE;
    GLsizei stride = 0;
    const void* pointer = nullptr;
    GLuint bufferBinding = 0;
};

class OpenGLCapture {
public:
    static OpenGLCapture& Get();

    void Init(const std::string& outputDir);
    void StartFrameCapture(uint32_t frameNumber);
    void EndFrameCapture();
    bool IsCapturing() const { return m_IsCapturing; }

    // State tracking
    void OnBindBuffer(GLenum target, GLuint buffer);
    void OnBindVertexArray(GLuint array);
    void OnVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
    void OnEnableVertexAttribArray(GLuint index);
    void OnDisableVertexAttribArray(GLuint index);

    // Draw call captures
    void OnDrawArrays(GLenum mode, GLint first, GLsizei count);
    void OnDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint baseVertex = 0);

    // Helper to query runtime GL procs if not loaded
    void EnsureGLProcsLoaded();

private:
    OpenGLCapture() = default;

    bool ReadBufferData(GLuint bufferId, GLintptr byteOffset, GLsizeiptr byteLength, std::vector<uint8_t>& outData);
    bool ExtractMesh(GLenum mode, const std::vector<uint32_t>& indices, GLint baseVertex);

    std::vector<GLVertexAttrib>& GetCurrentAttribs();

    std::string m_OutputDir;
    bool m_IsCapturing = false;
    uint32_t m_CurrentFrame = 0;
    uint32_t m_DrawCallCounter = 0;

    GLuint m_CurrentVAO = 0;
    GLuint m_CurrentVBO = 0;
    GLuint m_CurrentIBO = 0;

    std::unordered_map<GLuint, std::vector<GLVertexAttrib>> m_VaoAttribs;
    std::vector<GLVertexAttrib> m_DefaultAttribs;
};

} // namespace OpenGL
} // namespace OpenRipper
