#include "scene/scene.h"
#include "scene/entity.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace tsu {

Entity Scene::CreateEntity(const std::string& name)
{
    uint32_t id = (uint32_t)Transforms.size();
    Transforms.emplace_back();
    Cameras.emplace_back();
    MeshRenderers.emplace_back();
    RigidBodies.emplace_back();
    GameCameras.emplace_back();
    EntityNames.push_back(name);
    EntityGroups.push_back(-1);
    EntityParents.push_back(-1);
    EntityChildren.emplace_back();       // empty children list
    RootOrder.push_back((int)id);        // new entity starts at root
    return Entity(id, this);
}

void Scene::OnUpdate(float dt)
{
    // Scripts e behaviours futuros vão aqui
    (void)dt;
}

int Scene::GetActiveGameCamera() const
{
    for (size_t i = 0; i < GameCameras.size(); i++)
        if (GameCameras[i].Active) return (int)i;
    return -1;
}

// ----------------------------------------------------------------
// World matrix computation
// ----------------------------------------------------------------

glm::mat4 Scene::GetGroupWorldMatrix(int groupIdx) const
{
    const SceneGroup& g = Groups[groupIdx];
    glm::mat4 local     = g.Transform.GetMatrix();
    if (g.ParentGroup < 0)
        return local;
    return GetGroupWorldMatrix(g.ParentGroup) * local;
}

glm::mat4 Scene::GetEntityWorldMatrix(int entityIdx) const
{
    glm::mat4 local = Transforms[entityIdx].GetMatrix();

    // Entity parent takes priority over group parent
    if (entityIdx < (int)EntityParents.size() && EntityParents[entityIdx] >= 0)
        return GetEntityWorldMatrix(EntityParents[entityIdx]) * local;

    int group = EntityGroups[entityIdx];
    if (group < 0)
        return local;
    return GetGroupWorldMatrix(group) * local;
}

glm::vec3 Scene::GetEntityWorldPos(int entityIdx) const
{
    return glm::vec3(GetEntityWorldMatrix(entityIdx)[3]);
}

// ----------------------------------------------------------------
// Group management
// ----------------------------------------------------------------

int Scene::CreateGroup(const std::string& name, int parentGroup)
{
    int idx = (int)Groups.size();
    SceneGroup g;
    g.Name        = name;
    g.ParentGroup = parentGroup;
    Groups.push_back(g);
    if (parentGroup >= 0)
        Groups[parentGroup].ChildGroups.push_back(idx);
    return idx;
}

void Scene::SetEntityParent(int childIdx, int parentIdx)
{
    // Snapshot world position before any structural changes
    glm::vec3 worldPos   = GetEntityWorldPos(childIdx);
    int oldEntityParent  = (childIdx < (int)EntityParents.size()) ? EntityParents[childIdx] : -1;
    int oldGroup         = EntityGroups[childIdx];

    // Remove from old entity-parent's children list
    if (oldEntityParent >= 0 && oldEntityParent < (int)EntityChildren.size())
    {
        auto& ch = EntityChildren[oldEntityParent];
        ch.erase(std::remove(ch.begin(), ch.end(), childIdx), ch.end());
    }

    // Remove from group if in one
    if (oldGroup >= 0)
    {
        auto& ch = Groups[oldGroup].ChildEntities;
        ch.erase(std::remove(ch.begin(), ch.end(), childIdx), ch.end());
        EntityGroups[childIdx] = -1;
    }

    // Remove from root order if truly at root
    if (oldGroup < 0 && oldEntityParent < 0)
        RootOrder.erase(std::remove(RootOrder.begin(), RootOrder.end(), childIdx),
                        RootOrder.end());

    EntityParents[childIdx] = parentIdx;

    if (parentIdx >= 0)
    {
        EntityChildren[parentIdx].push_back(childIdx);
        // Convert to parent-local space
        glm::mat4 inv = glm::inverse(GetEntityWorldMatrix(parentIdx));
        Transforms[childIdx].Position = glm::vec3(inv * glm::vec4(worldPos, 1.0f));
    }
    else
    {
        // Back to world root
        Transforms[childIdx].Position = worldPos;
        RootOrder.push_back(childIdx);
    }
}

void Scene::SetEntityGroup(int entityIdx, int newGroup)
{
    // Snapshot world position before changes
    glm::vec3 worldPos      = GetEntityWorldPos(entityIdx);
    int oldGroup            = EntityGroups[entityIdx];
    int oldEntityParent     = (entityIdx < (int)EntityParents.size()) ? EntityParents[entityIdx] : -1;

    // Clear entity-parent relationship
    if (oldEntityParent >= 0 && oldEntityParent < (int)EntityChildren.size())
    {
        auto& ch = EntityChildren[oldEntityParent];
        ch.erase(std::remove(ch.begin(), ch.end(), entityIdx), ch.end());
    }
    EntityParents[entityIdx] = -1;

    // Remove from old group
    if (oldGroup >= 0)
    {
        auto& children = Groups[oldGroup].ChildEntities;
        children.erase(std::remove(children.begin(), children.end(), entityIdx), children.end());
    }

    // Remove from root order if was at true root
    if (oldGroup < 0 && oldEntityParent < 0)
        RootOrder.erase(std::remove(RootOrder.begin(), RootOrder.end(), entityIdx),
                        RootOrder.end());

    if (newGroup < 0)
    {
        EntityGroups[entityIdx]        = -1;
        Transforms[entityIdx].Position = worldPos;
        RootOrder.push_back(entityIdx);
        return;
    }

    glm::mat4 groupWorld             = GetGroupWorldMatrix(newGroup);
    glm::vec3 localPos               = glm::vec3(glm::inverse(groupWorld) * glm::vec4(worldPos, 1.0f));
    EntityGroups[entityIdx]          = newGroup;
    Transforms[entityIdx].Position   = localPos;
    Groups[newGroup].ChildEntities.push_back(entityIdx);
}

void Scene::UnparentEntity(int entityIdx)
{
    if (entityIdx < (int)EntityParents.size() && EntityParents[entityIdx] >= 0)
        SetEntityParent(entityIdx, -1);
    else
        SetEntityGroup(entityIdx, -1);
}

void Scene::DeleteGroup(int groupIdx)
{
    SceneGroup& g = Groups[groupIdx];
    for (int e : g.ChildEntities)
        UnparentEntity(e);
    std::vector<int> childGroupsCopy = g.ChildGroups;
    for (int cg : childGroupsCopy)
        DeleteGroup(cg);
    if (g.ParentGroup >= 0)
    {
        auto& siblings = Groups[g.ParentGroup].ChildGroups;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), groupIdx), siblings.end());
    }
    Groups[groupIdx].Name = "";
    Groups[groupIdx].ChildEntities.clear();
    Groups[groupIdx].ChildGroups.clear();
}

} // namespace tsu