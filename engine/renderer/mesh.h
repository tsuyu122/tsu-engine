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
    Mesh();
    void Draw();

    static Mesh CreateCube    (const std::string& hexColor = "#FFFFFF");
    static Mesh CreatePyramid (const std::string& hexColor = "#FFFFFF");
    static Mesh CreateCylinder(const std::string& hexColor = "#FFFFFF", int segments = 32);
    static Mesh CreateSphere  (const std::string& hexColor = "#FFFFFF", int stacks = 16, int slices = 32);
    static Mesh CreateCapsule (const std::string& hexColor = "#FFFFFF", float radius = 0.5f, float height = 1.0f, int segments = 32);
    static Mesh CreatePlane   (const std::string& hexColor = "#FFFFFF");

    // Gizmos internos do editor (sem textura, cor fixa)
    static Mesh CreateGizmoSphere(float radius = 0.08f); // bolinha da game camera
    static Mesh CreateGizmoLine  (float length = 0.6f);  // linha de direção
    // Seta do gizmo de translação (cilindro + cone, aponta em +Y)
    static Mesh CreateGizmoArrow (float length, float r, float g, float b);

private:
    unsigned int VAO, VBO;
    int m_VertexCount = 0;

    static Mesh BuildFromVertices(const std::vector<float>& verts);
};

} // namespace tsu