#include "mesh.h"

#include <cstddef>   // offsetof

Mesh uploadMesh(const MeshData& data) {
    Mesh mesh;
    mesh.indexCount = (GLsizei)data.indices.size();
    mesh.boundsMin  = data.boundsMin;
    mesh.boundsMax  = data.boundsMax;

    if (data.vertices.empty() || data.indices.empty())
        return mesh;

    glGenVertexArrays(1, &mesh.VAO);
    glBindVertexArray(mesh.VAO);

    glGenBuffers(1, &mesh.VBO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(Vertex), data.vertices.data(), GL_STATIC_DRAW);

    // The EBO binding is stored in the VAO, so bind it while the VAO is bound.
    glGenBuffers(1, &mesh.EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(unsigned int), data.indices.data(), GL_STATIC_DRAW);

    // Stride = one whole Vertex; offsets come from the struct layout instead of hand-counted floats.
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);

    // Unbind the VAO first. Unbinding the EBO while the VAO is still bound
    // would erase the EBO from the VAO's state.
    glBindVertexArray(0);

    return mesh;
}

void destroyMesh(Mesh& mesh) {
    if (mesh.EBO) glDeleteBuffers(1, &mesh.EBO);
    if (mesh.VBO) glDeleteBuffers(1, &mesh.VBO);
    if (mesh.VAO) glDeleteVertexArrays(1, &mesh.VAO);
    mesh = Mesh{};
}
