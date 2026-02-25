#include "entity.h"
#include "scene.h"

namespace tsu {

Entity::Entity(uint32_t id, Scene* scene)
    : m_ID(id), m_Scene(scene)
{
}

uint32_t Entity::GetID() const
{
    return m_ID;
}

}