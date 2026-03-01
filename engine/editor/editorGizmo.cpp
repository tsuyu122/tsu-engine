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
    return dist * 0.15f; // 15% of distance → constant screen size
}

glm::vec2 EditorGizmo::ToScreen(const glm::vec3& p,
                                  const glm::mat4& vp,
                                  int winW, int winH)
{
    glm::vec4 clip = vp * glm::vec4(p, 1.0f);
    if (clip.w <= 0.0001f) return { -99999.0f, -99999.0f };
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    // OpenGL NDC → pixel (top-left origin same as GLFW)
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
// OnMouseDown — detects which axis was clicked (including plane handles)
// ----------------------------------------------------------------

// Helper: minimum distance from a 2D point to a ring (circle) made of segments
static float DistToRing2D(glm::vec2 pt, glm::vec2 center, float radius)
{
    float d = glm::length(pt - center);
    return fabsf(d - radius);
}

EditorGizmo::Axis EditorGizmo::OnMouseDown(const glm::vec3& objPos,
                                            const EditorCamera& cam,
                                            float mouseX, float mouseY,
                                            int winW, int winH,
                                            int mode)
{
    m_ObjPos   = objPos;
    m_DragAxis = Axis::None;

    float aspect = (float)winW / (float)winH;
    glm::mat4 vp = cam.GetProjection(aspect) * cam.GetViewMatrix();

    float scale = GetScreenScale(objPos, cam);
    float len   = scale * k_ArrowLen;

    glm::vec2 mouse = { mouseX, mouseY };
    glm::vec2 origin = ToScreen(objPos, vp, winW, winH);

    // ---- Rotation mode: test proximity to ring circles ----
    if (mode == 1)
    {
        constexpr int kSegs = 64;
        constexpr float kPi = 3.14159265358979f;
        const float ringHitPx = 18.0f; // generous hit radius in pixels

        // Min distance to each ring in screen space (sample ring points)
        auto ringDist = [&](glm::vec3 ax1, glm::vec3 ax2) -> float {
            float best = 1e9f;
            for (int i = 0; i <= kSegs; ++i) {
                float a = 2.0f * kPi * (float)i / (float)kSegs;
                glm::vec3 p = objPos + (ax1 * cosf(a) + ax2 * sinf(a)) * scale * k_ArrowLen;
                glm::vec2 sp = ToScreen(p, vp, winW, winH);
                float d = glm::length(mouse - sp);
                if (d < best) best = d;
            }
            return best;
        };

        // X ring: Y-Z plane
        float dRX = ringDist(glm::vec3(0,1,0), glm::vec3(0,0,1));
        // Y ring: X-Z plane
        float dRY = ringDist(glm::vec3(1,0,0), glm::vec3(0,0,1));
        // Z ring: X-Y plane
        float dRZ = ringDist(glm::vec3(1,0,0), glm::vec3(0,1,0));

        float best = ringHitPx;
        if (dRX < best) { best = dRX; m_DragAxis = Axis::X; }
        if (dRY < best) { best = dRY; m_DragAxis = Axis::Y; }
        if (dRZ < best) { best = dRZ; m_DragAxis = Axis::Z; }

        return m_DragAxis;
    }

    // ---- Move / Scale mode: arrows + plane handles ----
    glm::vec3 tipX = objPos + glm::vec3(len, 0.0f, 0.0f);
    glm::vec3 tipY = objPos + glm::vec3(0.0f, len, 0.0f);
    glm::vec3 tipZ = objPos + glm::vec3(0.0f, 0.0f, len);

    glm::vec2 scrX = ToScreen(tipX, vp, winW, winH);
    glm::vec2 scrY = ToScreen(tipY, vp, winW, winH);
    glm::vec2 scrZ = ToScreen(tipZ, vp, winW, winH);

    // Check plane handles first (small squares at axis pair intersections)
    float pOff = k_PlaneOff * len;
    float pSz  = k_PlaneSz * len;

    // XY plane handle
    {
        glm::vec2 c0 = ToScreen(objPos + glm::vec3(pOff-pSz, pOff-pSz, 0), vp, winW, winH);
        glm::vec2 c1 = ToScreen(objPos + glm::vec3(pOff+pSz, pOff+pSz, 0), vp, winW, winH);
        glm::vec2 mn = glm::min(c0, c1);
        glm::vec2 mx = glm::max(c0, c1);
        if (mouse.x >= mn.x && mouse.x <= mx.x && mouse.y >= mn.y && mouse.y <= mx.y)
        { m_DragAxis = Axis::XY; return m_DragAxis; }
    }
    // XZ plane handle
    {
        glm::vec2 c0 = ToScreen(objPos + glm::vec3(pOff-pSz, 0, pOff-pSz), vp, winW, winH);
        glm::vec2 c1 = ToScreen(objPos + glm::vec3(pOff+pSz, 0, pOff+pSz), vp, winW, winH);
        glm::vec2 mn = glm::min(c0, c1);
        glm::vec2 mx = glm::max(c0, c1);
        if (mouse.x >= mn.x && mouse.x <= mx.x && mouse.y >= mn.y && mouse.y <= mx.y)
        { m_DragAxis = Axis::XZ; return m_DragAxis; }
    }
    // YZ plane handle
    {
        glm::vec2 c0 = ToScreen(objPos + glm::vec3(0, pOff-pSz, pOff-pSz), vp, winW, winH);
        glm::vec2 c1 = ToScreen(objPos + glm::vec3(0, pOff+pSz, pOff+pSz), vp, winW, winH);
        glm::vec2 mn = glm::min(c0, c1);
        glm::vec2 mx = glm::max(c0, c1);
        if (mouse.x >= mn.x && mouse.x <= mx.x && mouse.y >= mn.y && mouse.y <= mx.y)
        { m_DragAxis = Axis::YZ; return m_DragAxis; }
    }

    // Single-axis hit testing (arrows)
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
// OnMouseDrag — converts pixel delta to world delta (translation)
// Supports single-axis (X/Y/Z) and dual-axis (XY/XZ/YZ)
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

    // For dual-axis: project mouse delta onto both axes
    auto projectAxis = [&](const glm::vec3& axisWorld) -> float {
        glm::vec2 screenOrg = ToScreen(m_ObjPos, vp, winW, winH);
        glm::vec2 screenTip = ToScreen(m_ObjPos + axisWorld * len, vp, winW, winH);
        glm::vec2 screenAxis = screenTip - screenOrg;
        float screenLen = glm::length(screenAxis);
        if (screenLen < 1.0f) return 0.0f;
        glm::vec2 mouseDir = glm::normalize(screenAxis);
        float pixelDist = glm::dot(glm::vec2{dMouseX, -dMouseY}, mouseDir);
        return pixelDist * len / screenLen;
    };

    glm::vec3 delta(0.0f);
    switch (m_DragAxis) {
        case Axis::X:  delta = glm::vec3(1,0,0) * projectAxis(glm::vec3(1,0,0)); break;
        case Axis::Y:  delta = glm::vec3(0,1,0) * projectAxis(glm::vec3(0,1,0)); break;
        case Axis::Z:  delta = glm::vec3(0,0,1) * projectAxis(glm::vec3(0,0,1)); break;
        case Axis::XY:
            delta = glm::vec3(1,0,0) * projectAxis(glm::vec3(1,0,0))
                  + glm::vec3(0,1,0) * projectAxis(glm::vec3(0,1,0));
            break;
        case Axis::XZ:
            delta = glm::vec3(1,0,0) * projectAxis(glm::vec3(1,0,0))
                  + glm::vec3(0,0,1) * projectAxis(glm::vec3(0,0,1));
            break;
        case Axis::YZ:
            delta = glm::vec3(0,1,0) * projectAxis(glm::vec3(0,1,0))
                  + glm::vec3(0,0,1) * projectAxis(glm::vec3(0,0,1));
            break;
        default: break;
    }

    m_ObjPos += delta;
    return delta;
}

// ----------------------------------------------------------------
// OnMouseDragRotation — returns delta rotation in degrees
// ----------------------------------------------------------------

float EditorGizmo::OnMouseDragRotation(const EditorCamera& cam,
                                        float dMouseX, float dMouseY,
                                        int winW, int winH)
{
    if (m_DragAxis == Axis::None) return 0.0f;

    // Simple screen-space rotation: horizontal mouse movement = rotation
    // Sensitivity: 1 pixel = 0.5 degrees
    float pixelDist = dMouseX;

    // For Y axis rotation, horizontal mouse is natural
    // For X and Z, use the dominant screen direction
    if (m_DragAxis == Axis::Y || m_DragAxis == Axis::XZ)
        return pixelDist * 0.5f;
    if (m_DragAxis == Axis::X || m_DragAxis == Axis::YZ)
        return -dMouseY * 0.5f;
    if (m_DragAxis == Axis::Z || m_DragAxis == Axis::XY)
        return pixelDist * 0.5f;

    return 0.0f;
}

// ----------------------------------------------------------------
// OnMouseDragScale — returns scale delta along active axes
// ----------------------------------------------------------------

glm::vec3 EditorGizmo::OnMouseDragScale(const EditorCamera& cam,
                                         float dMouseX, float dMouseY,
                                         int winW, int winH)
{
    if (m_DragAxis == Axis::None) return glm::vec3(0.0f);

    float aspect = (float)winW / (float)winH;
    glm::mat4 vp = cam.GetProjection(aspect) * cam.GetViewMatrix();

    float scale = GetScreenScale(m_ObjPos, cam);
    float len   = scale * k_ArrowLen;

    // Project world axis to screen, then dot with mouse delta
    auto projectScale = [&](const glm::vec3& axisWorld) -> float {
        glm::vec2 screenOrg = ToScreen(m_ObjPos, vp, winW, winH);
        glm::vec2 screenTip = ToScreen(m_ObjPos + axisWorld * len, vp, winW, winH);
        glm::vec2 screenAxis = screenTip - screenOrg;
        float screenLen = glm::length(screenAxis);
        if (screenLen < 1.0f) return 0.0f;
        glm::vec2 mouseDir = glm::normalize(screenAxis);
        float pixelDist = glm::dot(glm::vec2{dMouseX, -dMouseY}, mouseDir);
        return pixelDist * 0.01f;
    };

    switch (m_DragAxis) {
        case Axis::X:  return glm::vec3(projectScale(glm::vec3(1,0,0)), 0, 0);
        case Axis::Y:  return glm::vec3(0, projectScale(glm::vec3(0,1,0)), 0);
        case Axis::Z:  return glm::vec3(0, 0, projectScale(glm::vec3(0,0,1)));
        case Axis::XY: { float dx=projectScale(glm::vec3(1,0,0)); float dy=projectScale(glm::vec3(0,1,0)); return glm::vec3(dx,dy,0); }
        case Axis::XZ: { float dx=projectScale(glm::vec3(1,0,0)); float dz=projectScale(glm::vec3(0,0,1)); return glm::vec3(dx,0,dz); }
        case Axis::YZ: { float dy=projectScale(glm::vec3(0,1,0)); float dz=projectScale(glm::vec3(0,0,1)); return glm::vec3(0,dy,dz); }
        default:       return glm::vec3(0.0f);
    }
}

// ----------------------------------------------------------------
// OnMouseUp
// ----------------------------------------------------------------

void EditorGizmo::OnMouseUp()
{
    m_DragAxis = Axis::None;
}

} // namespace tsu
