#pragma once
#include <cstdint>

namespace tsu {

class Scene;

class Entity
{
public:
    Entity(uint32_t id, Scene* scene);

    uint32_t GetID() const;

    template<typename T>
    T& GetComponent();

    template<typename T>
    T& AddComponent();

private:
    uint32_t m_ID;
    Scene* m_Scene;
};

}