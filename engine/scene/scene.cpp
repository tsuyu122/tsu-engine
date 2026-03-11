#include "scene/scene.h"
#include "scene/entity.h"
#include "renderer/mesh.h"
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
    EntityMaterial.push_back(-1);
    EntityColors.push_back(glm::vec3(1.0f, 1.0f, 1.0f));
    EntityStatic.push_back(true);
    EntityReceiveLightmap.push_back(true);
    PlayerControllers.emplace_back();
    MouseLooks.emplace_back();
    Lights.emplace_back();
    Triggers.emplace_back();
    Animators.emplace_back();
    AnimationControllers.emplace_back();
    MultiplayerManagers.emplace_back();
    MultiplayerControllers.emplace_back();
    LuaScripts.emplace_back();
    AudioSources.emplace_back();
    AudioBarriers.emplace_back();
    SkinnedMeshes.emplace_back();
    EntityParents.push_back(-1);
    EntityChildren.emplace_back();       // empty children list
    EntityIsPrefabInstance.push_back(false);
    EntityPrefabSource.push_back(-1);
    EntityPrefabNode.push_back(-1);
    EntityTags.emplace_back("");
    MazeGenerators.emplace_back();
    RootOrder.push_back((int)id);        // new entity starts at root
    return Entity(id, this);
}

int Scene::SpawnFromPrefab(int prefabIdx, const glm::vec3& position)
{
    if (prefabIdx < 0 || prefabIdx >= (int)Prefabs.size()) return -1;
    const PrefabAsset& pref = Prefabs[prefabIdx];
    if (pref.Nodes.empty()) return -1;

    std::vector<int> nodeId(pref.Nodes.size(), -1);
    for (int ni = 0; ni < (int)pref.Nodes.size(); ++ni)
    {
        const PrefabEntityData& node = pref.Nodes[ni];
        Entity e = CreateEntity(node.Name);
        int id = (int)e.GetID();
        nodeId[ni] = id;
        Transforms[id] = node.Transform;
        // Root node gets the spawn position
        if (ni == 0) {
            Transforms[id].Position = position;
        }
        if (!node.MeshType.empty()) {
            Mesh* mesh = nullptr;
            const std::string& mt = node.MeshType;
            if      (mt == "cube")     mesh = new Mesh(Mesh::CreateCube("#DDDDDD"));
            else if (mt == "sphere")   mesh = new Mesh(Mesh::CreateSphere("#DDDDDD"));
            else if (mt == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid("#DDDDDD"));
            else if (mt == "cylinder") mesh = new Mesh(Mesh::CreateCylinder("#DDDDDD"));
            else if (mt == "capsule")  mesh = new Mesh(Mesh::CreateCapsule("#DDDDDD"));
            else if (mt == "plane")    mesh = new Mesh(Mesh::CreatePlane("#DDDDDD"));
            else if (mt.size() > 4 && mt.substr(0,4) == "obj:") mesh = new Mesh(Mesh::LoadOBJ(mt.substr(4)));
            MeshRenderers[id].MeshPtr = mesh;
            MeshRenderers[id].MeshType = mt;
        }
        if (!node.MaterialName.empty()) {
            for (int mi2 = 0; mi2 < (int)Materials.size(); ++mi2)
                if (Materials[mi2].Name == node.MaterialName) { EntityMaterial[id] = mi2; break; }
        }
        if (node.HasLight)  Lights[id] = node.Light;
        if (node.HasCamera) GameCameras[id] = node.Camera;
        EntityIsPrefabInstance[id] = true;
        EntityPrefabSource[id] = prefabIdx;
    }
    // Set up parent-child relationships
    for (int ni = 0; ni < (int)pref.Nodes.size(); ++ni) {
        int pidx = pref.Nodes[ni].ParentIdx;
        if (pidx >= 0 && pidx < (int)nodeId.size()) SetEntityParent(nodeId[ni], nodeId[pidx]);
    }
    return nodeId[0];
}

void Scene::OnUpdate(float dt)
{
    // Future scripts and behaviours go here
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

void Scene::DeleteEntity(int idx)
{
    if (idx < 0 || idx >= (int)Transforms.size()) return;

    // Detach from parent
    int par = (idx < (int)EntityParents.size()) ? EntityParents[idx] : -1;
    if (par >= 0)
    {
        auto& ch = EntityChildren[par];
        ch.erase(std::remove(ch.begin(), ch.end(), idx), ch.end());
    }

    // Detach children — they go to root
    for (int child : EntityChildren[idx])
    {
        EntityParents[child] = -1;
        RootOrder.push_back(child);
    }

    // Remove from group
    int grp = EntityGroups[idx];
    if (grp >= 0 && grp < (int)Groups.size())
    {
        auto& ce = Groups[grp].ChildEntities;
        ce.erase(std::remove(ce.begin(), ce.end(), idx), ce.end());
    }

    // Remove from root order
    RootOrder.erase(std::remove(RootOrder.begin(), RootOrder.end(), idx), RootOrder.end());

    // Helper: fix any index > idx (shift down by 1) in a list
    auto fixIdx = [&](int& v) { if (v > idx) --v; else if (v == idx) v = -1; };
    auto fixList = [&](std::vector<int>& list)
    {
        for (auto& v : list) if (v > idx) --v;
    };

    // Erase from all parallel arrays
    auto erase1 = [&](auto& vec) { vec.erase(vec.begin() + idx); };
    erase1(Transforms);
    erase1(Cameras);
    erase1(MeshRenderers);
    erase1(RigidBodies);
    erase1(GameCameras);
    erase1(EntityNames);
    erase1(EntityGroups);
    erase1(EntityParents);
    erase1(EntityChildren);
    erase1(EntityMaterial);
    erase1(EntityColors);
    if (idx < (int)EntityStatic.size()) EntityStatic.erase(EntityStatic.begin() + idx);
    if (idx < (int)EntityReceiveLightmap.size()) EntityReceiveLightmap.erase(EntityReceiveLightmap.begin() + idx);
    erase1(PlayerControllers);
    erase1(MouseLooks);
    erase1(Lights);
    if (idx < (int)Triggers.size()) Triggers.erase(Triggers.begin() + idx);
    if (idx < (int)Animators.size()) Animators.erase(Animators.begin() + idx);
    if (idx < (int)AnimationControllers.size()) AnimationControllers.erase(AnimationControllers.begin() + idx);
    if (idx < (int)MultiplayerManagers.size()) MultiplayerManagers.erase(MultiplayerManagers.begin() + idx);
    if (idx < (int)MultiplayerControllers.size()) MultiplayerControllers.erase(MultiplayerControllers.begin() + idx);
    if (idx < (int)LuaScripts.size()) LuaScripts.erase(LuaScripts.begin() + idx);
    if (idx < (int)AudioSources.size()) AudioSources.erase(AudioSources.begin() + idx);
    if (idx < (int)AudioBarriers.size()) AudioBarriers.erase(AudioBarriers.begin() + idx);
    if (idx < (int)SkinnedMeshes.size()) SkinnedMeshes.erase(SkinnedMeshes.begin() + idx);
    if (idx < (int)EntityIsPrefabInstance.size())
        EntityIsPrefabInstance.erase(EntityIsPrefabInstance.begin() + idx);
    if (idx < (int)EntityPrefabSource.size())
        EntityPrefabSource.erase(EntityPrefabSource.begin() + idx);
    if (idx < (int)EntityPrefabNode.size())
        EntityPrefabNode.erase(EntityPrefabNode.begin() + idx);
    if (idx < (int)EntityTags.size())
        EntityTags.erase(EntityTags.begin() + idx);
    if (idx < (int)MazeGenerators.size())
        MazeGenerators.erase(MazeGenerators.begin() + idx);
    // RootOrder was already cleaned manually — just fix remaining indices:
    fixList(RootOrder);

    // Fix EntityParents and EntityChildren references
    for (auto& p : EntityParents) if (p > idx) --p;
    for (auto& childList : EntityChildren) fixList(childList);

    // Fix group ChildEntities
    for (auto& g : Groups) fixList(g.ChildEntities);

    // Fix MouseLook entity references
    for (auto& ml : MouseLooks) { fixIdx(ml.YawTargetEntity); fixIdx(ml.PitchTargetEntity); }
}

void Scene::EnsureChannelCount(int count)
{
    if (count <= 0) return;
    while ((int)Channels.size() < count)
    {
        ChannelVariable ch;
        ch.Name = "Channel " + std::to_string((int)Channels.size() + 1);
        Channels.push_back(ch);
    }
}

bool Scene::GetChannelBool(int idx, bool fallback) const
{
    if (idx < 0 || idx >= (int)Channels.size()) return fallback;
    const auto& ch = Channels[idx];
    if (ch.Type == ChannelVariableType::Boolean) return ch.BoolValue;
    if (ch.Type == ChannelVariableType::Float)   return ch.FloatValue > 0.5f;
    return !ch.StringValue.empty();
}

float Scene::GetChannelFloat(int idx, float fallback) const
{
    if (idx < 0 || idx >= (int)Channels.size()) return fallback;
    const auto& ch = Channels[idx];
    if (ch.Type == ChannelVariableType::Float)   return ch.FloatValue;
    if (ch.Type == ChannelVariableType::Boolean) return ch.BoolValue ? 1.0f : 0.0f;
    return fallback;
}

std::string Scene::GetChannelString(int idx, const std::string& fallback) const
{
    if (idx < 0 || idx >= (int)Channels.size()) return fallback;
    const auto& ch = Channels[idx];
    if (ch.Type == ChannelVariableType::String) return ch.StringValue;
    if (ch.Type == ChannelVariableType::Boolean) return ch.BoolValue ? "true" : "false";
    return std::to_string(ch.FloatValue);
}

void Scene::SetChannelBool(int idx, bool value)
{
    if (idx < 0) return;
    EnsureChannelCount(idx + 1);
    auto& ch = Channels[idx];
    ch.Type = ChannelVariableType::Boolean;
    ch.BoolValue = value;
}

void Scene::SetChannelFloat(int idx, float value)
{
    if (idx < 0) return;
    EnsureChannelCount(idx + 1);
    auto& ch = Channels[idx];
    ch.Type = ChannelVariableType::Float;
    ch.FloatValue = value;
}

void Scene::SetChannelString(int idx, const std::string& value)
{
    if (idx < 0) return;
    EnsureChannelCount(idx + 1);
    auto& ch = Channels[idx];
    ch.Type = ChannelVariableType::String;
    ch.StringValue = value;
}

} // namespace tsu
