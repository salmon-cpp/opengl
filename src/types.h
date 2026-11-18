#pragma once
#include <glm/glm.hpp>

#include <string>

struct Light {
    glm::vec3 position = glm::vec3(3);
    glm::vec3 color    = glm::vec3(1.0f);
    bool visible       = true;
};

struct Scene {
    float backgroundColor[3] = {0.05f, 0.05f, 0.08f};
    Light light;
    int polygonMode = 0;
};

struct Transform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);   // euler
    glm::vec3 scale    = glm::vec3(1.0f);
};

struct Material {
    glm::vec3 color = glm::vec3(0.8);
    bool useTexture = false;
    unsigned int textureID = 0;
};

struct Mesh;

struct Object {
    std::string name = "Object";
    const Mesh* mesh = nullptr;
    Transform transform;
    Material material;
    bool visible = true;
    glm::vec3 rotSpeed = glm::vec3(30.0f);
};
