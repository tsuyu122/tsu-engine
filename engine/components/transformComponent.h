#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace tsu {

struct TransformComponent
{
    glm::vec3 Position = {0.0f, 0.0f, 0.0f};
    glm::vec3 Rotation = {0.0f, 0.0f, 0.0f}; // in degrees
    glm::vec3 Scale    = {1.0f, 1.0f, 1.0f};

    // 1️⃣ World-axis movement (ignores rotation)
    void MoveWorld(const glm::vec3& dir, float speed)
    {
        Position += dir * speed;
    }

    // 2️⃣ Movement based on object rotation
    void MoveLocal(const glm::vec3& dir, float speed)
    {
        glm::mat4 rotationMatrix = glm::mat4(1.0f);

        rotationMatrix = glm::rotate(rotationMatrix, glm::radians(Rotation.x), glm::vec3(1,0,0));
        rotationMatrix = glm::rotate(rotationMatrix, glm::radians(Rotation.y), glm::vec3(0,1,0));
        rotationMatrix = glm::rotate(rotationMatrix, glm::radians(Rotation.z), glm::vec3(0,0,1));

        glm::vec3 finalDir = glm::vec3(rotationMatrix * glm::vec4(dir, 0.0f));
        Position += finalDir * speed;
    }

    // 3️⃣ Character-style movement (uses deltaTime)
    void MoveCharacter(const glm::vec3& dir, float speed, float dt)
    {
        Position += dir * speed * dt;
    }

    // 4️⃣ Physics movement (placeholder)
    void MovePhysics(const glm::vec3& velocity, float dt)
    {
        Position += velocity * dt;
    }

    glm::mat4 GetMatrix() const
    {
        glm::mat4 model = glm::mat4(1.0f);

        model = glm::translate(model, Position);

        model = glm::rotate(model, glm::radians(Rotation.x), glm::vec3(1,0,0));
        model = glm::rotate(model, glm::radians(Rotation.y), glm::vec3(0,1,0));
        model = glm::rotate(model, glm::radians(Rotation.z), glm::vec3(0,0,1));

        model = glm::scale(model, Scale);

        return model;
    }
};

}