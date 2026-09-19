#ifndef GFX_CORE_UNIFORMBUFFER_H
#define GFX_CORE_UNIFORMBUFFER_H

/**
 * @file UniformBuffer.h
 * @brief RAII, move-only OpenGL uniform buffer (UBO) bound to an index.
 *
 * Uniform buffers back the plan's batched GPU data blocks (lights, per-frame
 * camera/shadow matrices). Like every GL object here, creation/resizing/
 * binding assert render-thread affinity (plan section 1.1) and copies are
 * deleted so the GL name cannot be double-deleted.
 */

#include <cstddef>
#include <span>
#include <type_traits>

#include <glad/gl.h>

namespace gfx {

class UniformBuffer {
public:
    UniformBuffer() = default;
    ~UniformBuffer();

    UniformBuffer(const UniformBuffer&)            = delete;
    UniformBuffer& operator=(const UniformBuffer&) = delete;
    UniformBuffer(UniformBuffer&& other) noexcept;
    UniformBuffer& operator=(UniformBuffer&& other) noexcept;

    // Allocate (or resize) the store to `bytes`. Render-thread only.
    void Create(GLsizeiptr bytes, GLenum usage = GL_DYNAMIC_DRAW);

    // Allocate from a POD value/span and remember its size. Render-thread only.
    template <class T>
    requires std::is_trivially_copyable_v<T>
    void Create(const T& value, GLenum usage = GL_DYNAMIC_DRAW) {
        Create(static_cast<GLsizeiptr>(sizeof(T)), usage);
        Replace(std::as_bytes(std::span<const T>(&value, 1)));
    }

    // Re-substitute the whole store. Render-thread only.
    void Replace(std::span<const std::byte> data);
    template <class T>
    requires std::is_trivially_copyable_v<T>
    void Replace(const T& value) { Replace(std::as_bytes(std::span<const T>(&value, 1))); }

    // Bind to a uniform buffer binding point (glBindBufferRange).
    void BindBase(GLuint index) const;
    static void UnbindBase(GLuint index);

    [[nodiscard]] GLuint      id()    const noexcept { return id_; }
    [[nodiscard]] GLsizeiptr  size()  const noexcept { return size_; }
    [[nodiscard]] bool        valid() const noexcept { return id_ != 0; }

private:
    GLuint     id_   = 0;
    GLsizeiptr size_ = 0;
};

} // namespace gfx

#endif // GFX_CORE_UNIFORMBUFFER_H
