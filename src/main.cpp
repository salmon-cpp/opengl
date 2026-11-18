#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "window.h"
#include "shader.h"
#include "overlay.h"

#include "types.h"
#include "camera.h"
#include "mesh.h"
#include "obj_loader.h"

#include <iostream>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// #define PI 3.14159265358979323846

/* OpenGL Error Catching | scrapped
#define GLCall(x) \
    x; \
    while (GLenum err = glGetError()) \
        std::cerr << "OpenGL error (" << #x << "): " << err << std::endl;
*/

constexpr int WIN_WIDTH = 1600;
constexpr int WIN_HEIGHT = 900;

constexpr GLenum polygonModes[] = { GL_FILL, GL_LINE, GL_POINT };

// modifiers:
Scene scene;
Camera camera;

// object vector:
std::vector<Object> objects;

// stays unordered for pointer validity:
std::unordered_map<std::string, Mesh> meshes;

const Mesh* loadModel(const std::string& name, const std::string& path) {
    MeshData data;
    if (!loadOBJ(path, data)) return nullptr;

    meshes[name] = uploadMesh(data);
    return &meshes[name];
}

void spawnObject(const std::string& name, const Mesh* mesh, glm::vec3 position, glm::vec3 scale, glm::vec3 color) {
    if (!mesh) {
        std::cerr << "Not spawning '" << name << "': mesh failed to load\n";
        return;
    }

    Object obj;
    obj.name = name;
    obj.mesh = mesh;
    obj.transform.position = position;
    obj.transform.scale = scale;
    obj.material.color = color / 255.0f;

    objects.push_back(obj);
}

unsigned int loadTexture(const char* path) {
    int width, height, nrChannels;
    unsigned int texture;

    stbi_set_flip_vertically_on_load(true);
    unsigned char *data = stbi_load(path, &width, &height, &nrChannels, 0);

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (data) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        std::cout << "Failed to load texture: " << path << std::endl;
    }
    stbi_image_free(data);

    return texture;
}

void render(Shader& lightingShader, Shader& lightCubeShader, const Mesh* lampMesh, Window& window) {
    glPolygonMode(GL_FRONT_AND_BACK, polygonModes[scene.polygonMode]);
    glClearColor(scene.backgroundColor[0], scene.backgroundColor[1], scene.backgroundColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 view = camera.getViewMatrix();
    const glm::mat4 projection = camera.getProjectionMatrix(window.getAspect());

    lightingShader.use();

    lightingShader.setMat4("view", view);
    lightingShader.setMat4("projection", projection);
    lightingShader.setInt("ourTexture", 0);
    lightingShader.setVec3("lightColor", scene.light.color);
    lightingShader.setVec3("lightPos", scene.light.position);

    for (Object& obj : objects) {
        if (!obj.visible || !obj.mesh || obj.mesh->indexCount == 0) continue;

        // object update:
        // obj.transform.rotation.x += obj.rotSpeed.x * window.deltaTime;
        // obj.transform.rotation.y += obj.rotSpeed.y * window.deltaTime;
        // obj.transform.rotation.z += obj.rotSpeed.z * window.deltaTime;
        // obj.transform.rotation = glm::mod(obj.transform.rotation, 360.0f);

        const Transform& t = obj.transform;
        
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, t.position);                                   // position XYZ
        model = glm::rotate(model, glm::radians(t.rotation.x), glm::vec3(1, 0, 0));  // pitch |
        model = glm::rotate(model, glm::radians(t.rotation.y), glm::vec3(0, 1, 0));  // yaw   | Rotation
        model = glm::rotate(model, glm::radians(t.rotation.z), glm::vec3(0, 0, 1));  // roll  |
        model = glm::scale(model, t.scale);                                          // size

        lightingShader.setMat4("model", model);
        lightingShader.setVec3("objectColor", obj.material.color);
        lightingShader.setBool("useTexture", obj.material.useTexture);
        lightingShader.setVec3("viewPos", camera.position);

        if (obj.material.useTexture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, obj.material.textureID);
        }

        glBindVertexArray(obj.mesh->VAO);
        glDrawElements(GL_TRIANGLES, obj.mesh->indexCount, GL_UNSIGNED_INT, 0);
    }

    if (!scene.light.visible || !lampMesh || lampMesh->indexCount == 0) return;

    // The lamp shader only reads attribute 0 (position); the normal/uv attributes
    // in the VAO are simply ignored, so the lamp can share the cube's VAO.
    lightCubeShader.use();
    glBindVertexArray(lampMesh->VAO);

    lightCubeShader.setMat4("view", view);
    lightCubeShader.setMat4("projection", projection);
    lightCubeShader.setVec3("lightColor", scene.light.color);

    glm::mat4 lampModel = glm::mat4(1.0f);
    lampModel = glm::translate(lampModel, scene.light.position);
    lampModel = glm::scale(lampModel, glm::vec3(0.1f));
    lightCubeShader.setMat4("model", lampModel);

    glDrawElements(GL_TRIANGLES, lampMesh->indexCount, GL_UNSIGNED_INT, 0);
}

namespace fs = std::filesystem;

// Directory containing the running executable (build/).
static fs::path executableDir(const char* argv0) {
    std::error_code ec;
#ifdef __linux__
    fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path();
#endif
    return fs::absolute(argv0, ec).parent_path();
}

// The binary lives in build/, but shaders/ and content/ are in the project root.
// Switch to whichever of cwd, the exe dir, or its parent holds shaders/, so the
// relative asset paths work no matter where the program is launched from.
static void enterProjectRoot(const fs::path& exeDir) {
    std::error_code ec;
    for (const fs::path& dir : {fs::current_path(ec), exeDir, exeDir.parent_path()}) {
        if (fs::is_directory(dir / "shaders", ec)) {
            fs::current_path(dir, ec);
            return;
        }
    }
    std::cerr << "Could not find the shaders/ folder; run from the project root\n";
}

int main(int argc, char** argv) {
    const fs::path exeDir = executableDir(argc > 0 ? argv[0] : "");
    enterProjectRoot(exeDir);

    Window window(WIN_WIDTH, WIN_HEIGHT, &camera);
    if (!window.initialize()) return -1;

    Shader lightingShader("shaders/cube.vs", "shaders/cube.fs");
    Shader lightCubeShader("shaders/lighting.vs", "shaders/lighting.fs");

    glEnable(GL_DEPTH_TEST);
    glPointSize(3.0f);

    // meshes:
    const Mesh* cube = loadModel("cube", "content/models/cube.obj");
    const Mesh* bean = loadModel("bean", "content/models/bean.obj");
    const Mesh* plane = loadModel("plane", "content/models/plane.obj");

    // unsigned int texture = loadTexture("content/textures/texture.jpg");

    spawnObject("Cube", cube, glm::vec3(-1.0f, 0.5f, 0.0f), glm::vec3(0.5f), glm::vec3(200, 150, 80));
    spawnObject("Bean", bean, glm::vec3( 1.0f, 0.5f, 0.0f), glm::vec3(0.5f), glm::vec3(80, 100, 200));
    spawnObject("Plane", plane, glm::vec3( 0.0f, 0.0f, 0.0f), glm::vec3(10.0f), glm::vec3(210));

    Overlay overlay(window.getGLFWwindow(), (exeDir / "imgui.ini").string());

    while (!glfwWindowShouldClose(window.getGLFWwindow())) {
        window.processInput(camera);

        render(lightingShader, lightCubeShader, cube, window);

        overlay.loopStart();
        overlay.drawUI(scene, objects, camera);
        overlay.loopEnd();

        window.refresh();
    }

    objects.clear();
    for (auto& [name, mesh] : meshes)
        destroyMesh(mesh);

    return 0;
}

