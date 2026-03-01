#pragma once
#include <glm/glm.hpp>

namespace tsu {

class EditorCamera;

// ================================================================
// EditorGizmo — axis selection and translation drag in the editor
// Normal always points B→A (same convention as PhysicsSystem)
// ================================================================

class EditorGizmo
{
public:
    enum class Axis { None, X, Y, Z, XY, XZ, YZ };

    // Arrow length in "local units" (scaled by distance)
    static constexpr float k_ArrowLen    = 1.2f;
    // Hit radius in screen pixels
    static constexpr float k_HitRadiusPx = 14.0f;
    // Plane handle offset (fraction of arrow length)
    static constexpr float k_PlaneOff    = 0.35f;
    static constexpr float k_PlaneSz     = 0.15f;

    // Detect which axis was clicked (or None)
    // mode: 0=Move/Scale (arrows+planes), 1=Rotate (rings)
    Axis OnMouseDown(const glm::vec3& objPos,
                     const EditorCamera& cam,
                     float mouseX, float mouseY,
                     int winW, int winH,
                     int mode = 0);

    // Called each frame while dragging — returns delta in world-space (translation)
    glm::vec3 OnMouseDrag(const EditorCamera& cam,
                          float dMouseX, float dMouseY,
                          int winW, int winH);

    // Rotation drag: returns delta rotation in degrees around the active axis
    float OnMouseDragRotation(const EditorCamera& cam,
                              float dMouseX, float dMouseY,
                              int winW, int winH);

    // Scale drag: returns scale delta (uniform along the active axis)
    glm::vec3 OnMouseDragScale(const EditorCamera& cam,
                               float dMouseX, float dMouseY,
                               int winW, int winH);

    // Called on mouse-up
    void OnMouseUp();

    bool IsDragging()  const { return m_DragAxis != Axis::None; }
    Axis GetDragAxis() const { return m_DragAxis; }

    // Uniform scale to keep the gizmo at constant screen size
    float GetScreenScale(const glm::vec3& pos, const EditorCamera& cam) const;

private:
    Axis      m_DragAxis = Axis::None;
    glm::vec3 m_ObjPos{};

    // Projects 3D point to screen coordinates [0..winW, 0..winH]
    static glm::vec2 ToScreen(const glm::vec3& p,
                               const glm::mat4& vp,
                               int winW, int winH);

    // Distance from point to 2D segment
    static float DistToSeg2D(glm::vec2 pt, glm::vec2 a, glm::vec2 b);
};

} // namespace tsu
