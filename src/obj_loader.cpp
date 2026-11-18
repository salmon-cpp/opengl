#include "obj_loader.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

// One face corner, as 0-based indices into the v / vt / vn lists. -1 = not given.
struct Corner {
    int v  = -1;
    int vt = -1;
    int vn = -1;
};

// OBJ indices are 1-based. Negative indices count back from the end of the
// list *as read so far* (-1 = the most recently declared element).
int resolveIndex(int idx, size_t count) {
    if (idx > 0) return idx - 1;
    if (idx < 0) return (int)count + idx;
    return -1;   // 0 is not a valid OBJ index; treat as "missing"
}

// Parses "7", "7/3", "7//2" or "7/3/2" into a Corner. Returns false on malformed input.
bool parseCorner(const std::string& token, size_t nv, size_t nvt, size_t nvn, Corner& out) {
    int raw[3] = { 0, 0, 0 };   // 0 = field absent

    size_t start = 0;
    for (int field = 0; field < 3; field++) {
        size_t slash = token.find('/', start);
        std::string part = token.substr(start, slash == std::string::npos ? std::string::npos : slash - start);

        if (!part.empty()) {
            try {
                raw[field] = std::stoi(part);
            } catch (...) {
                return false;
            }
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }

    out.v  = resolveIndex(raw[0], nv);
    out.vt = resolveIndex(raw[1], nvt);
    out.vn = resolveIndex(raw[2], nvn);

    // A position is mandatory; uv / normal are optional but must be in range if present.
    if (out.v < 0 || out.v >= (int)nv) return false;
    if (out.vt >= (int)nvt) return false;
    if (out.vn >= (int)nvn) return false;
    return true;
}

// Packs a (v, vt, vn) triplet into one 64-bit key for the de-duplication map.
// 21 bits per field (+1 so that -1 becomes 0) -> supports ~2 million of each.
uint64_t cornerKey(const Corner& c) {
    return ((uint64_t)(c.v  + 1) << 42) |
           ((uint64_t)(c.vt + 1) << 21) |
            (uint64_t)(c.vn + 1);
}

} // namespace

bool loadOBJ(const std::string& path, MeshData& out) {
    out = MeshData{};

    std::ifstream file(path);
    if (!file) {
        std::cerr << "[OBJ] Could not open '" << path << "' (check the working directory)\n";
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;

    // (v, vt, vn) triplet -> index of the GL vertex already created for it.
    std::unordered_map<uint64_t, unsigned int> vertexLookup;

    std::vector<Corner> face;   // reused for every 'f' line
    size_t faceCount = 0;
    size_t skippedFaces = 0;

    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        lineNumber++;

        std::istringstream ss(line);   // >> skips whitespace, including Windows '\r'
        std::string prefix;
        if (!(ss >> prefix) || prefix[0] == '#') continue;

        if (prefix == "v") {
            glm::vec3 p(0.0f);
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (prefix == "vt") {
            glm::vec2 t(0.0f);
            ss >> t.x >> t.y;
            uvs.push_back(t);
        }
        else if (prefix == "vn") {
            glm::vec3 n(0.0f);
            ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (prefix == "f") {
            face.clear();
            bool ok = true;
            std::string token;

            while (ss >> token) {
                Corner c;
                if (!parseCorner(token, positions.size(), uvs.size(), normals.size(), c)) {
                    ok = false;
                    break;
                }
                face.push_back(c);
            }

            if (!ok || face.size() < 3) {
                std::cerr << "[OBJ] " << path << ":" << lineNumber << " skipping malformed face\n";
                skippedFaces++;
                continue;
            }

            // Flat normal for faces that don't supply one (from the first triangle).
            bool faceHasNormals = true;
            for (const Corner& c : face)
                if (c.vn < 0) faceHasNormals = false;

            glm::vec3 flatNormal(0.0f, 1.0f, 0.0f);
            if (!faceHasNormals) {
                glm::vec3 a = positions[face[0].v];
                glm::vec3 b = positions[face[1].v];
                glm::vec3 c = positions[face[2].v];
                glm::vec3 n = glm::cross(b - a, c - a);
                if (glm::length(n) > 1e-12f) flatNormal = glm::normalize(n);
            }

            // Turn each corner into a GL vertex index.
            std::vector<unsigned int> faceIndices;
            faceIndices.reserve(face.size());

            for (const Corner& c : face) {
                // Corners with their own normal can be shared between faces.
                // Corners using a computed flat normal can't: the same (v, vt) pair
                // needs a different normal on each face, so they always get a new vertex.
                if (c.vn >= 0) {
                    auto it = vertexLookup.find(cornerKey(c));
                    if (it != vertexLookup.end()) {
                        faceIndices.push_back(it->second);
                        continue;
                    }
                }

                Vertex vert;
                vert.position = positions[c.v];
                vert.uv       = (c.vt >= 0) ? uvs[c.vt] : glm::vec2(0.0f);
                vert.normal   = (c.vn >= 0) ? glm::normalize(normals[c.vn]) : flatNormal;

                unsigned int newIndex = (unsigned int)out.vertices.size();
                out.vertices.push_back(vert);
                if (c.vn >= 0) vertexLookup[cornerKey(c)] = newIndex;

                faceIndices.push_back(newIndex);
            }

            // Fan triangulation: (0,1,2) (0,2,3) (0,3,4) ... keeps the original winding.
            for (size_t i = 1; i + 1 < faceIndices.size(); i++) {
                out.indices.push_back(faceIndices[0]);
                out.indices.push_back(faceIndices[i]);
                out.indices.push_back(faceIndices[i + 1]);
            }
            faceCount++;
        }
        // Everything else (o, g, s, mtllib, usemtl, l, ...) is ignored.
    }

    if (out.indices.empty()) {
        std::cerr << "[OBJ] '" << path << "' contains no usable faces\n";
        out = MeshData{};
        return false;
    }

    // Bounding box over the vertices actually used.
    out.boundsMin = out.boundsMax = out.vertices[0].position;
    for (const Vertex& v : out.vertices) {
        out.boundsMin = glm::min(out.boundsMin, v.position);
        out.boundsMax = glm::max(out.boundsMax, v.position);
    }

    std::cout << "[OBJ] Loaded '" << path << "': "
              << positions.size() << " v, " << uvs.size() << " vt, " << normals.size() << " vn, "
              << faceCount << " faces -> "
              << out.vertices.size() << " GL vertices, " << out.indices.size() << " indices";
    if (skippedFaces) std::cout << " (" << skippedFaces << " faces skipped)";
    std::cout << "\n";

    return true;
}
