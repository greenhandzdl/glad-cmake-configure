#ifndef GLDX_GEOMETRY_GEOMETRYFACTORY_H
#define GLDX_GEOMETRY_GEOMETRYFACTORY_H

/**
 * @file GeometryFactory.h
 * @brief Procedural mesh generation (CPU-only, Stage A).
 *
 * Returns MeshData value types with no GL dependency, so these can be produced
 * on any thread. The caller uploads on the render thread via Mesh::Upload.
 *
 * Winding convention: every factory here winds its triangles counter-clockwise
 * as seen from the side their vertex normals point towards, which is what
 * Renderer::Init()'s glFrontFace(GL_CCW) + glCullFace(GL_BACK) expects. An
 * inverted mesh is not loud: back faces vanish (holes) or the far side of a
 * closed shape is drawn instead, while every attribute still looks right.
 */

#include "gldx/geometry/Mesh.h"

namespace gldx {

class GeometryFactory {
public:
    // Axis-aligned cube of side length `size`, centered at the origin.
    static MeshData Cube(float size = 1.0f);

    // Unit quad in the XY plane (2 triangles, indexed).
    static MeshData Quad(float width = 1.0f, float height = 1.0f);

    // UV sphere of given radius and tessellation: `segments` meridians, `rings`
    // parallels, (segments+1)*(rings+1) vertices and segments*rings*6 indices.
    // The index stream is three indices per triangle, so the same buffer can also
    // be handed to the tessellation stages as one 3-vertex patch per triangle
    // (Mesh::DrawAs(GL_PATCHES) with glPatchParameteri(GL_PATCH_VERTICES, 3)).
    static MeshData Sphere(float radius = 1.0f, int segments = 32, int rings = 16);

    // Ground grid in the XZ plane (line-free, triangle strip not needed; indexed quads).
    static MeshData Plane(float halfExtent = 5.0f);
};

} // namespace gldx

#endif // GLDX_GEOMETRY_GEOMETRYFACTORY_H
