#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>

// location 0 = position, location 1 = normal, location 2 = uv
// struct can be unpacked with std::vector<Vertex>
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// CPU-side mesh: plain data
struct MeshData {
    std::vector<Vertex>       vertices;
    std::vector<unsigned int> indices;

    glm::vec3 boundsMin = glm::vec3(0.0f);  // axis-aligned bounding box in model space
    glm::vec3 boundsMax = glm::vec3(0.0f);
};

struct Mesh {
    GLuint  VAO = 0;
    GLuint  VBO = 0;
    GLuint  EBO = 0;
    GLsizei indexCount = 0;

    glm::vec3 boundsMin = glm::vec3(0.0f);
    glm::vec3 boundsMax = glm::vec3(0.0f);
};

Mesh uploadMesh(const MeshData& data);
void destroyMesh(Mesh& mesh);
