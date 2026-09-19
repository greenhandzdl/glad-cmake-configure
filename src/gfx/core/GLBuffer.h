#ifndef GFX_CORE_GLBUFFER_H
#define GFX_CORE_GLBUFFER_H

/**
 * @file GLBuffer.h
 * @brief RAII, move-only OpenGL buffer wrapper.
 *
 * A thin owner of one GLuint buffer object. Every mutating call asserts the
 * render thread (plan section 1.1) because glGenBuffers/glBufferData touch GL.
 * Copies are deleted to prevent double-delete of the GL name.
 */

#include <cstddef>
#include <span>
#include <type_traits>

#include <glad/gl.h>

namespace gfx {

class GLBuffer {
public:
    GLBuffer() = default;
    ~GLBuffer();

    GLBuffer(const GLBuffer&)            = delete;
    GLBuffer& operator=(const GLBuffer&) = delete;
    GLBuffer(GLBuffer&& other) noexcept;
    GLBuffer& operator=(GLBuffer&& other) noexcept;

    // Allocate / resize the backing GL buffer and upload raw bytes (render thread).
    void Create(GLenum target, std::span<const std::byte> data, GLenum usage = GL_STATIC_DRAW);

    // Upload from a POD typed span (render thread).
    template <class T>
    requires std::is_trivially_copyable_v<T>
    void Create(GLenum target, std::span<const T> data, GLenum usage = GL_STATIC_DRAW) {
        Create(target, std::as_bytes(data), usage);
    }

    // Reserve `byteCount` bytes of (uninitialised) storage. Used to size a
    // dynamic buffer once, then feed it via SubData. Render thread.
    void Reserve(GLenum target, std::size_t byteCount, GLenum usage = GL_DYNAMIC_DRAW);

    // Partial upload at a byte offset (render thread). Buffer must exist.
    void SubData(std::span<const std::byte> data, std::size_t offsetBytes = 0);
    template <class T>
    requires std::is_trivially_copyable_v<T>
    void SubData(std::span<const T> data, std::size_t offsetBytes = 0) {
        SubData(std::as_bytes(data), offsetBytes);
    }

    // Re-substitute the whole store (render thread). Must already be created.
    void Replace(std::span<const std::byte> data);
    template <class T>
    requires std::is_trivially_copyable_v<T>
    void Replace(std::span<const T> data) { Replace(std::as_bytes(data)); }

    void Bind(GLenum target) const;
    void Unbind(GLenum target) const;

    [[nodiscard]] GLuint  id()       const noexcept { return id_; }
    [[nodiscard]] GLenum  target()   const noexcept { return target_; }
    [[nodiscard]] GLsizei sizeBytes() const noexcept { return sizeBytes_; }
    [[nodiscard]] bool    valid()    const noexcept { return id_ != 0; }

private:
    GLuint  id_        = 0;
    GLenum  target_    = GL_ARRAY_BUFFER;
    GLsizei sizeBytes_ = 0;
};

} // namespace gfx

#endif // GFX_CORE_GLBUFFER_H
