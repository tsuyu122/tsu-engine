#pragma once
#include <glm/glm.hpp>

namespace tsu {

class EditorCamera
{
public:
    EditorCamera();
    void OnUpdate(float dt);

    glm::mat4 GetViewMatrix()          const;
    glm::mat4 GetProjection(float aspect) const;
    glm::vec3 GetPosition() const { return m_Position; }
    glm::vec3 GetFront()    const { return m_Front; }
    float     GetScrollSpeedDisplayTimer() const { return m_ScrollSpeedDisplayTimer; }
    float     GetScrollSpeed()             const { return ScrollSpeed; }

    float FOV             = 60.0f;
    float Near            = 0.1f;
    float Far             = 1000.0f;
    float MoveSpeed       = 5.0f;
    float MouseSensitivity= 0.1f;
    float SpeedMultiplier = 3.0f;
    float ScrollSpeed     = 5.0f;  // [0.01 .. 10] units per scroll notch

private:
    void UpdateVectors();

    glm::vec3 m_Position = { 0.0f, 1.0f, 6.0f };
    glm::vec3 m_Front    = { 0.0f, 0.0f,-1.0f };
    glm::vec3 m_Up       = { 0.0f, 1.0f, 0.0f };
    glm::vec3 m_Right    = { 1.0f, 0.0f, 0.0f };
    glm::vec3 m_WorldUp  = { 0.0f, 1.0f, 0.0f };
    float m_Yaw                    = -90.0f;
    float m_Pitch                  =   0.0f;
    float m_ScrollSpeedDisplayTimer = 0.0f;
};

} // namespace tsu