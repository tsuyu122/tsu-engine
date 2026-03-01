#include "editor/editorCamera.h"
#include "input/inputManager.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace tsu {

EditorCamera::EditorCamera()
{
    UpdateVectors();
}

void EditorCamera::OnUpdate(float dt)
{
    // Mouse rotation — always allowed, regardless of UI focus
    if (InputManager::IsMousePressed(Mouse::Right))
    {
        MouseDelta delta = InputManager::GetMouseDelta();
        m_Yaw   += delta.x * MouseSensitivity;
        m_Pitch += delta.y * MouseSensitivity;
        if (m_Pitch >  89.0f) m_Pitch =  89.0f;
        if (m_Pitch < -89.0f) m_Pitch = -89.0f;
        UpdateVectors();
    }

    // Middle mouse pan (hold MMB + drag)
    if (InputManager::IsMousePressed(Mouse::Middle))
    {
        MouseDelta delta = InputManager::GetMouseDelta();
        float panSpeed = MoveSpeed * 0.012f;
        m_Position += m_Right * (delta.x * panSpeed);
        m_Position += m_Up    * (delta.y * panSpeed);
    }

    // Tick the scroll-speed display timer
    if (m_ScrollSpeedDisplayTimer > 0.0f)
        m_ScrollSpeedDisplayTimer -= dt;

    // Scroll: only when ImGui is not capturing the mouse (viewport scroll)
    float scroll = InputManager::GetScrollDelta();
    if (scroll != 0.0f && !ImGui::GetIO().WantCaptureMouse)
    {
        if (InputManager::IsKeyPressed(Key::LeftShift))
        {
            ScrollSpeed = std::clamp(ScrollSpeed + scroll * 0.5f, 0.01f, 10.0f);
            m_ScrollSpeedDisplayTimer = 2.0f; // show HUD for 2 seconds
        }
        else
        {
            m_Position += m_Front * (scroll * ScrollSpeed);
        }
    }

    // Keyboard movement — blocked when an ImGui widget has keyboard focus,
    // but NOT when RMB is held (we're in camera-rotation mode; WASD must work)
    bool inCameraRotate = InputManager::IsMousePressed(Mouse::Right);
    if (!inCameraRotate && ImGui::GetIO().WantCaptureKeyboard) return;

    float speed = MoveSpeed * dt;
    if (InputManager::IsKeyPressed(Key::LeftShift))
        speed *= SpeedMultiplier;

    if (InputManager::IsKeyPressed(Key::W))           m_Position += m_Front   * speed;
    if (InputManager::IsKeyPressed(Key::S))           m_Position -= m_Front   * speed;
    if (InputManager::IsKeyPressed(Key::A))           m_Position -= m_Right   * speed;
    if (InputManager::IsKeyPressed(Key::D))           m_Position += m_Right   * speed;
    if (InputManager::IsKeyPressed(Key::Space))       m_Position += m_WorldUp * speed;
    if (InputManager::IsKeyPressed(Key::E))           m_Position += m_WorldUp * speed;
    if (InputManager::IsKeyPressed(Key::LeftControl)) m_Position -= m_WorldUp * speed;
    if (InputManager::IsKeyPressed(Key::Q))           m_Position -= m_WorldUp * speed;
}

glm::mat4 EditorCamera::GetViewMatrix() const
{
    return glm::lookAt(m_Position, m_Position + m_Front, m_Up);
}

glm::mat4 EditorCamera::GetProjection(float aspect) const
{
    return glm::perspective(glm::radians(FOV), aspect, Near, Far);
}

void EditorCamera::UpdateVectors()
{
    glm::vec3 front;
    front.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
    front.y = sin(glm::radians(m_Pitch));
    front.z = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
    m_Front = glm::normalize(front);
    m_Right = glm::normalize(glm::cross(m_Front, m_WorldUp));
    m_Up    = glm::normalize(glm::cross(m_Right, m_Front));
}

} // namespace tsu