# The single build definition for Windows (MSYS2 UCRT64) and Linux/NixOS.
# VS Code: Ctrl+Shift+B.  Terminal: cmake -S . -B build -G Ninja && cmake --build build
cmake_minimum_required(VERSION 3.16)
project(opengl_renderer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_executable(main
    src/main.cpp
    src/shader.cpp
    src/window.cpp
    src/overlay.cpp
    src/camera.cpp
    src/mesh.cpp
    src/obj_loader.cpp
    src/glad.c

    include/imgui/imgui.cpp
    include/imgui/imgui_draw.cpp
    include/imgui/imgui_tables.cpp
    include/imgui/imgui_widgets.cpp
    include/imgui/backends/imgui_impl_glfw.cpp
    include/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(main PRIVATE
    include
    include/imgui
    include/imgui/backends
)

set_target_properties(main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)

if(WIN32)
    target_link_libraries(main PRIVATE "${CMAKE_SOURCE_DIR}/lib/libglfw3dll.a")
    add_custom_command(TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/lib/glfw3.dll" "$<TARGET_FILE_DIR:main>")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
    if(GLFW_VERSION VERSION_LESS 3.4)
        message(FATAL_ERROR
            "GLFW ${GLFW_VERSION} found, but 3.4+ is required: the vendored "
            "headers in include/GLFW are 3.4 and ImGui's GLFW backend uses 3.4 APIs.")
    endif()
    target_include_directories(main BEFORE PRIVATE ${GLFW_INCLUDE_DIRS})
    target_link_libraries(main PRIVATE PkgConfig::GLFW ${CMAKE_DL_LIBS})
endif()
