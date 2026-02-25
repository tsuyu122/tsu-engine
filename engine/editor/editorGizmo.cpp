#include "editor/editorGizmo.h"
#include "editor/editorCamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace tsu {

// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------

float EditorGizmo::GetScreenScale(const glm::vec3& pos,
                                   const EditorCamera& cam) const
{
    float dist = glm::length(pos - cam.GetPosition());
    return dist * 0.15f; // 15% da distância → tamanho constante em tela
}

glm::vec2 EditorGizmo::ToScreen(const glm::vec3& p,
                                  const glm::mat4& vp,
                                  int winW, int winH)
{
    glm::vec4 clip = vp * glm::vec4(p, 1.0f);
    if (clip.w <= 0.0001f) return { -99999.0f, -99999.0f };
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    // OpenGL NDC → pixel (origem topo-esquerda igual ao GLFW)
    return {
        (ndc.x *  0.5f + 0.5f) * (float)winW,
        (ndc.y * -0.5f + 0.5f) * (float)winH
    };
}

float EditorGizmo::DistToSeg2D(glm::vec2 pt, glm::vec2 a, glm::vec2 b)
{
    glm::vec2 ab  = b - a;
    float     len2 = glm::dot(ab, ab);
    if (len2 < 1e-6f) return glm::length(pt - a);
    float t = glm::clamp(glm::dot(pt - a, ab) / len2, 0.0f, 1.0f);
    return glm::length(pt - (a + t * ab));
}

// ----------------------------------------------------------------
// OnMouseDown — detecta qual eixo foi clicado
// ----------------------------------------------------------------

EditorGizmo::Axis EditorGizmo::OnMouseDown(const glm::vec3& objPos,
                                            const EditorCamera& cam,
                                            float mouseX, float mouseY,
                                            int winW, int winH)
{
    m_ObjPos   = objPos;
    m_DragAxis = Axis::None;

    float aspect = (float)winW / (float)winH;
    glm::mat4 vp = cam.GetProjection(aspect) * cam.GetViewMatrix();

    float scale = GetScreenScale(objPos, cam);
    float len   = scale * k_ArrowLen;

    glm::vec3 tipX = objPos + glm::vec3(len, 0.0f, 0.0f);
    glm::vec3 tipY = objPos + glm::vec3(0.0f, len, 0.0f);
    glm::vec3 tipZ = objPos + glm::vec3(0.0f, 0.0f, len);

    glm::vec2 mouse  = { mouseX, mouseY };
    glm::vec2 origin = ToScreen(objPos, vp, winW, winH);
    glm::vec2 scrX   = ToScreen(tipX,   vp, winW, winH);
    glm::vec2 scrY   = ToScreen(tipY,   vp, winW, winH);
    glm::vec2 scrZ   = ToScreen(tipZ,   vp, winW, winH);

    float dX = DistToSeg2D(mouse, origin, scrX);
    float dY = DistToSeg2D(mouse, origin, scrY);
    float dZ = DistToSeg2D(mouse, origin, scrZ);

    float best = k_HitRadiusPx;
    if (dX < best) { best = dX; m_DragAxis = Axis::X; }
    if (dY < best) { best = dY; m_DragAxis = Axis::Y; }
    if (dZ < best) { best = dZ; m_DragAxis = Axis::Z; }

    return m_DragAxis;
}

// ----------------------------------------------------------------
// OnMouseDrag — converte delta de pixel em delta de mundo
// ----------------------------------------------------------------

glm::vec3 EditorGizmo::OnMouseDrag(const EditorCamera& cam,
                                    float dMouseX, float dMouseY,
                                    int winW, int winH)
{
    if (m_DragAxis == Axis::None) return glm::vec3(0.0f);

    float aspect = (float)winW / (float)winH;
    glm::mat4 vp = cam.GetProjection(aspect) * cam.GetViewMatrix();

    float scale = GetScreenScale(m_ObjPos, cam);
    float len   = scale * k_ArrowLen;

    glm::vec3 axisWorld = (m_DragAxis == Axis::X) ? glm::vec3(1, 0, 0) :
                          (m_DragAxis == Axis::Y) ? glm::vec3(0, 1, 0) :
                                                    glm::vec3(0, 0, 1);

    // Projeta a seta na tela para saber sua direção em pixels
    glm::vec2 screenOrg = ToScreen(m_ObjPos,               vp, winW, winH);
    glm::vec2 screenTip = ToScreen(m_ObjPos + axisWorld * len, vp, winW, winH);

    glm::vec2 screenAxis = screenTip - screenOrg;
    float screenLen = glm::length(screenAxis);
    if (screenLen < 1.0f) return glm::vec3(0.0f);

    // GLFW y-down → invert dMouseY to match NDC y-up
    glm::vec2 mouseDir = glm::normalize(screenAxis);
    float     pixelDist = glm::dot(glm::vec2{ dMouseX, -dMouseY }, mouseDir);

    // pixels → world units (proporção entre comprimento da seta em px e em world)
    float worldDist = pixelDist * len / screenLen;

    // Atualiza posição interna para próximo drag frame
    m_ObjPos += axisWorld * worldDist;

    return axisWorld * worldDist;
}

// ----------------------------------------------------------------
// OnMouseUp
// ----------------------------------------------------------------

void EditorGizmo::OnMouseUp()
{
    m_DragAxis = Axis::None;
}

} // namespace tsu
