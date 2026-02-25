#pragma once
#include <glm/glm.hpp>

namespace tsu {

class EditorCamera;

// ================================================================
// EditorGizmo — seleção de eixo e drag de translação no editor
// A normal aponta sempre B→A (mesmo padrão do PhysicsSystem)
// ================================================================

class EditorGizmo
{
public:
    enum class Axis { None, X, Y, Z };

    // Comprimento da seta em "unidades locais" (escalonado pela distância)
    static constexpr float k_ArrowLen    = 1.2f;
    // Raio de hit em pixels de tela
    static constexpr float k_HitRadiusPx = 14.0f;

    // Chama no mouse-down — retorna qual eixo foi clicado (ou None)
    Axis OnMouseDown(const glm::vec3& objPos,
                     const EditorCamera& cam,
                     float mouseX, float mouseY,
                     int winW, int winH);

    // Chama a cada frame enquanto arrastar — retorna delta world-space
    glm::vec3 OnMouseDrag(const EditorCamera& cam,
                          float dMouseX, float dMouseY,
                          int winW, int winH);

    // Chama no mouse-up
    void OnMouseUp();

    bool IsDragging()  const { return m_DragAxis != Axis::None; }
    Axis GetDragAxis() const { return m_DragAxis; }

    // Escala uniforme para manter o gizmo com tamanho constante em tela
    float GetScreenScale(const glm::vec3& pos, const EditorCamera& cam) const;

private:
    Axis      m_DragAxis = Axis::None;
    glm::vec3 m_ObjPos{};

    // Projeta ponto 3D para coordenadas de tela [0..winW, 0..winH]
    static glm::vec2 ToScreen(const glm::vec3& p,
                               const glm::mat4& vp,
                               int winW, int winH);

    // Distância de ponto a segmento 2D
    static float DistToSeg2D(glm::vec2 pt, glm::vec2 a, glm::vec2 b);
};

} // namespace tsu
