cmake_minimum_required(VERSION 3.16)
project(TsuEngine)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(external/glfw)

add_library(glad external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)

# ---- ImGui via FetchContent ----
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.6
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    external/glfw/include
    external/glad/include
)
target_link_libraries(imgui glfw glad)

# ---- Main engine ----
include_directories(
    external/glfw/include
    external/glad/include
    external/glm
    external/stb
    .
    Engine
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

add_executable(TsuEngine
    Engine/core/application.cpp
    Engine/core/window.cpp
    Engine/editor/editorCamera.cpp
    Engine/editor/editorGizmo.cpp
    Engine/input/inputManager.cpp
    Engine/physics/physicsSystem.cpp
    Engine/renderer/mesh.cpp
    Engine/renderer/renderer.cpp
    Engine/renderer/textureLoader.cpp
    Engine/scene/entity.cpp
    Engine/scene/scene.cpp
    Engine/serialization/sceneSerializer.cpp
    Engine/ui/hierarchyPanel.cpp
    Engine/ui/inspectorPanel.cpp
    Engine/ui/uiManager.cpp
    Sandbox/main.cpp
)

target_link_libraries(TsuEngine glfw glad opengl32 imgui)

add_custom_command(TARGET TsuEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:TsuEngine>/assets"
    COMMENT "Copiando assets...")