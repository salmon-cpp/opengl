#pragma once

#include "mesh.h"

#include <string>

// Parses a Wavefront .obj file into CPU-side mesh data (no GL calls).
//
// Supported: v, vt, vn, and f with any of the corner forms
//   v    v/vt    v//vn    v/vt/vn
// including negative (relative) indices and polygons with any number of
// corners (triangulated as a fan, so faces must be convex).
// Faces without normals get a flat face normal.
// Ignored: materials (mtllib/usemtl), groups/objects (g/o), smoothing (s), lines, etc.
//
// Returns false (and leaves `out` empty) if the file can't be read or has no faces.
bool loadOBJ(const std::string& path, MeshData& out);
