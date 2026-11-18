#pragma once
#include <GLFW/glfw3.h>
#include "types.h"
#include "camera.h"

#include <string>
#include <vector>

class Overlay {
public:
    // iniPath: where ImGui saves its window layout (must outlive the overlay).
    Overlay(GLFWwindow* window, std::string iniPath);
    ~Overlay();

    void loopStart();
    void drawUI(Scene& scene, std::vector<Object>& objects, Camera& camera);
    void loopEnd();

private:
    int selected = 0;
    std::string iniPath;
};
