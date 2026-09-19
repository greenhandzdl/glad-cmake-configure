#ifndef GFX_GEOMETRY_GEOMETRYFACTORY_H
#define GFX_GEOMETRY_GEOMETRYFACTORY_H

/**
 * @file GeometryFactory.h
 * @brief Procedural mesh generation (CPU-only, Stage A).
 *
 * Returns MeshData value types with no GL dependency, so these can be produced
 * on any thread. The caller uploads on the render thread via Mesh::Upload.
 */

#include "gfx/geometry/Mesh.h"

namespace gfx {

class GeometryFactory {
public:
    // Axis-aligned cube of side length `size`, centered at the origin.
    static MeshData Cube(float size = 1.0f);

    // Unit quad in the XY plane (2 triangles, indexed).
    static MeshData Quad(float width = 1.0f, float height = 1.0f);

    // UV sphere of given radius and tessellation.
    static MeshData Sphere(float radius = 1.0f, int segments = 32, int rings = 16);

    // Ground grid in the XZ plane (line-free, triangle strip not needed; indexed quads).
    static MeshData Plane(float halfExtent = 5.0f);
};

} // namespace gfx

#endif // GFX_GEOMETRY_GEOMETRYFACTORY_H
