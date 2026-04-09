cmake_minimum_required(VERSION 3.20)

# ─── vcpkg toolchain ──────────────────────────────────────────────────────────
# Set VCPKG_ROOT before project() or pass via -DCMAKE_TOOLCHAIN_FILE=...
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain")
endif()

project(MinecraftClone VERSION 0.1.0 LANGUAGES CXX)

# ─── C++20 ────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Build type defaults ──────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ─── Compiler flags ───────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX- /MP /arch:AVX2
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/DNDEBUG>
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Zi>
        $<$<CONFIG:Debug>:/RTC1>)
    add_link_options($<$<CONFIG:Release>:/LTCG>)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -march=native
        $<$<CONFIG:Release>:-O3 -DNDEBUG -flto>
        $<$<CONFIG:Debug>:-O0 -g>)
endif()

# ─── Dependencies via vcpkg ───────────────────────────────────────────────────
find_package(glfw3  CONFIG REQUIRED)
find_package(GLEW   CONFIG REQUIRED)
find_package(glm    CONFIG REQUIRED)

# ─── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/Application.cpp
    src/Window.cpp
    src/Camera.cpp
    src/World.cpp
    src/Chunk.cpp
    src/ChunkMesher.cpp
    src/ChunkManager.cpp
    src/Lighting.cpp
    src/TerrainGenerator.cpp
    src/Renderer.cpp
    src/SkyRenderer.cpp
    src/Shader.cpp
    src/Texture.cpp
    src/TextureAtlas.cpp
    src/GuiRenderer.cpp
    src/Physics.cpp
    src/Player.cpp
    src/BlockRegistry.cpp
    src/ThreadPool.cpp
    src/InputHandler.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    GLEW::GLEW
    glm::glm
    opengl32   # Windows
)

# ─── Copy shaders to build dir ────────────────────────────────────────────────
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMENT "Copying shaders..."
)

# ─── Copy textures to build dir ───────────────────────────────────────────────
if(EXISTS ${CMAKE_SOURCE_DIR}/textures)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/textures
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/textures
        COMMENT "Copying textures..."
    )
endif()
