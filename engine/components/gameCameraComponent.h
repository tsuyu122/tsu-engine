#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace tsu {

// Câmera de jogo — existe na scene, ativa só no GameMode
// No EditorMode aparece como gizmo (bolinha vermelha + linha)
struct GameCameraComponent
{
    float FOV  = 75.0f;
    float Near = 0.1f;
    float Far  = 500.0f;

    bool Active = true; // câmera principal de jogo

    // Direção calculada a partir do transform da entidade
    glm::vec3 Front   = { 0.0f, 0.0f, -1.0f };
    glm::vec3 Up      = { 0.0f, 1.0f,  0.0f };
    glm::vec3 Right   = { 1.0f, 0.0f,  0.0f };
    glm::vec3 WorldUp = { 0.0f, 1.0f,  0.0f };

    float Yaw   = -90.0f;
    float Pitch =   0.0f;

    glm::mat4 GetProjection(float aspect) const
    {
        return glm::perspective(glm::radians(FOV), aspect, Near, Far);
    }

    void UpdateVectors()
    {
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up    = glm::normalize(glm::cross(Right, Front));
    }
};

} // namespace tsu