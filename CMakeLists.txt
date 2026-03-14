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

# ---- tinyobjloader (header-only) ----
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG        v1.0.6
)
FetchContent_GetProperties(tinyobjloader)
if(NOT tinyobjloader_POPULATED)
    FetchContent_Populate(tinyobjloader)
endif()

# ---- Lua 5.4 ----
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.7
)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
endif()
# Build Lua as a static library (exclude standalone interpreter + compiler)
file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/lua.c")
list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/luac.c")
add_library(lua54 STATIC ${LUA_SOURCES})
target_include_directories(lua54 PUBLIC ${lua_SOURCE_DIR})
# Suppress Lua warnings on MSVC
if(MSVC)
    target_compile_options(lua54 PRIVATE /W0)
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
    engine
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${tinyobjloader_SOURCE_DIR}
    ${lua_SOURCE_DIR}
)

if(WIN32)
    set(TSU_WIN32_FLAG WIN32)
else()
    set(TSU_WIN32_FLAG "")
endif()

add_executable(TsuEngine ${TSU_WIN32_FLAG}
    engine/core/application.cpp
    engine/core/window.cpp
    engine/editor/editorCamera.cpp
    engine/editor/editorGizmo.cpp
    engine/input/inputManager.cpp
    engine/physics/physicsSystem.cpp
    engine/procedural/mazeGenerator.cpp
    engine/renderer/lightBaker.cpp
    engine/renderer/lightmapManager.cpp
    engine/renderer/mesh.cpp
    engine/renderer/renderer.cpp
    engine/renderer/textureLoader.cpp
    engine/scene/entity.cpp
    engine/scene/scene.cpp
    engine/serialization/sceneSerializer.cpp
    engine/serialization/prefabSerializer.cpp
    engine/network/multiplayerSystem.cpp
    engine/ui/hierarchyPanel.cpp
    engine/ui/inspectorPanel.cpp
    engine/ui/uiManager.cpp
    engine/lua/luaSystem.cpp
    sandbox/main.cpp
)

if(WIN32)
    target_link_libraries(TsuEngine glfw glad opengl32 imgui lua54 ws2_32)
else()
    target_link_libraries(TsuEngine glfw glad GL imgui lua54 pthread dl)
endif()
target_compile_definitions(TsuEngine PRIVATE TSU_EDITOR=1)

add_custom_command(TARGET TsuEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:TsuEngine>/assets"
    COMMENT "Copying assets...")

# ---- GameRuntime — lean standalone player (no editor / no ImGui) ----
add_executable(GameRuntime ${TSU_WIN32_FLAG}
    engine/core/gameApp.cpp
    engine/core/window.cpp
    engine/editor/editorCamera.cpp
    engine/input/inputManager.cpp
    engine/physics/physicsSystem.cpp
    engine/procedural/mazeGenerator.cpp
    engine/renderer/lightBaker.cpp
    engine/renderer/lightmapManager.cpp
    engine/renderer/mesh.cpp
    engine/renderer/renderer.cpp
    engine/renderer/textureLoader.cpp
    engine/scene/entity.cpp
    engine/scene/scene.cpp
    engine/serialization/sceneSerializer.cpp
    engine/serialization/prefabSerializer.cpp
    engine/network/multiplayerSystem.cpp
    engine/lua/luaSystem.cpp
    sandbox/game_main.cpp
)

target_include_directories(GameRuntime PRIVATE
    external/glfw/include
    external/glad/include
    external/glm
    external/stb
    .
    engine
    ${tinyobjloader_SOURCE_DIR}
    ${lua_SOURCE_DIR}
)

# No imgui — runtime is lean
if(WIN32)
    target_link_libraries(GameRuntime glfw glad opengl32 lua54 ws2_32)
else()
    target_link_libraries(GameRuntime glfw glad GL lua54 pthread dl)
endif()

# Place GameRuntime in a hidden _engine/ subdirectory so it does not appear
# alongside TsuEngine.exe in the Release folder.
set_target_properties(GameRuntime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_BINARY_DIR}/Debug/_engine"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_BINARY_DIR}/Release/_engine"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo/_engine"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_BINARY_DIR}/MinSizeRel/_engine")

add_custom_command(TARGET GameRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:GameRuntime>/assets"
    COMMENT "Copying assets to GameRuntime output...")
