#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace tsu {

glm::vec3 HexToRGB(const std::string& hex);

class Mesh
{
public:
    struct CpuVertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec3 color;
        glm::vec2 uv0;
        glm::vec2 uv2;
    };

    Mesh();
    void Draw();

    const std::vector<CpuVertex>& GetCpuVertices() const { return m_Vertices; }
    bool HasValidUV2() const;
    void GenerateAutoUV2(float padding = 0.08f);

    static Mesh CreateCube    (const std::string& hexColor = "#FFFFFF");
    static Mesh CreatePyramid (const std::string& hexColor = "#FFFFFF");
    static Mesh CreateCylinder(const std::string& hexColor = "#FFFFFF", int segments = 32);
    static Mesh CreateSphere  (const std::string& hexColor = "#FFFFFF", int stacks = 16, int slices = 32);
    static Mesh CreateCapsule (const std::string& hexColor = "#FFFFFF", float radius = 0.5f, float height = 1.0f, int segments = 32);
    static Mesh CreatePlane   (const std::string& hexColor = "#FFFFFF");

    // Load an OBJ file from disk; returns an empty mesh on failure
    static Mesh LoadOBJ(const std::string& path);

    // Gizmos internos do editor (sem textura, cor fixa)
    static Mesh CreateGizmoSphere(float radius = 0.08f);
    static Mesh CreateGizmoLine  (float length = 0.6f);
    static Mesh CreateGizmoArrow (float length, float r, float g, float b);

    // Axis-aligned bounding box (object space)
    glm::vec3 BoundsMin = glm::vec3(-0.5f);
    glm::vec3 BoundsMax = glm::vec3( 0.5f);

private:
    unsigned int VAO, VBO;
    int m_VertexCount = 0;
    std::vector<CpuVertex> m_Vertices;

    static Mesh BuildFromVertices(const std::vector<float>& verts);
    static Mesh BuildFromCpuVertices(const std::vector<CpuVertex>& verts);
    void UploadGpuBuffer();
};

} // namespace tsu