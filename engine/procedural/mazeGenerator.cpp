#include "procedural/mazeGenerator.h"
#include "scene/entity.h"
#include "renderer/mesh.h"
#include "renderer/lightmapManager.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <chrono>

namespace tsu {

// ----------------------------------------------------------------
// Thread-local random engine (no seed — truly random each run)
// ----------------------------------------------------------------
std::mt19937& MazeGenerator::GetRNG()
{
    static std::mt19937 rng(
        (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return rng;
}

// ----------------------------------------------------------------
// Get player world position (first active PlayerController)
// ----------------------------------------------------------------
bool MazeGenerator::GetPlayerPosition(const Scene& scene, glm::vec3& outPos)
{
    for (int i = 0; i < (int)scene.PlayerControllers.size(); ++i)
    {
        if (scene.PlayerControllers[i].Active && scene.PlayerControllers[i].Enabled)
        {
            outPos = scene.GetEntityWorldPos(i);
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------
// Rotate local (x,z) by 0/90/180/270 degrees (CW in top-down XZ)
// ----------------------------------------------------------------
void MazeGenerator::RotateXZ(int lx, int lz, int rot, int& ox, int& oz)
{
    switch (rot)
    {
        default:
        case 0:   ox =  lx; oz =  lz; break;
        case 90:  ox =  lz; oz = -lx; break;
        case 180: ox = -lx; oz = -lz; break;
        case 270: ox = -lz; oz =  lx; break;
    }
}

// ----------------------------------------------------------------
// Check if all cells of a room template (rotated) are free
// ----------------------------------------------------------------
bool MazeGenerator::CellsFree(const MazeGeneratorComponent& gen,
                               const RoomTemplate& room,
                               int ax, int az, int rotation)
{
    for (const auto& b : room.Blocks)
    {
        int rx, rz;
        RotateXZ(b.X, b.Z, rotation, rx, rz);
        int wx = ax + rx;
        int wz = az + rz;
        for (const auto& occ : gen.OccupiedCells)
            if (occ.first == wx && occ.second == wz) return false;
    }
    return true;
}

// ----------------------------------------------------------------
// Door-connectivity helpers
// ----------------------------------------------------------------
void MazeGenerator::DoorFaceToLocalDelta(DoorFace f, int& dx, int& dz)
{
    switch (f) {
        case DoorFace::PosX:  dx =  1; dz =  0; break;
        case DoorFace::NegX:  dx = -1; dz =  0; break;
        case DoorFace::PosZ:  dx =  0; dz =  1; break;
        case DoorFace::NegZ:  dx =  0; dz = -1; break;
        default:              dx =  1; dz =  0; break;
    }
}

DoorFace MazeGenerator::OppositeDoorFace(DoorFace f)
{
    switch (f) {
        case DoorFace::PosX: return DoorFace::NegX;
        case DoorFace::NegX: return DoorFace::PosX;
        case DoorFace::PosZ: return DoorFace::NegZ;
        case DoorFace::NegZ: return DoorFace::PosZ;
        default: return DoorFace::NegX;
    }
}

static DoorFace DeltaToDoorFace(int dx, int dz)
{
    if (dx > 0) return DoorFace::PosX;
    if (dx < 0) return DoorFace::NegX;
    if (dz > 0) return DoorFace::PosZ;
    return DoorFace::NegZ;
}

bool MazeGenerator::TemplateHasDoorOnWorldFace(const RoomTemplate& tmpl,
                                                 DoorFace worldFace, int rotation)
{
    int wdx, wdz;
    DoorFaceToLocalDelta(worldFace, wdx, wdz);
    for (const auto& door : tmpl.Doors)
    {
        int ldx, ldz;
        DoorFaceToLocalDelta(door.Face, ldx, ldz);
        // Rotate the local direction by the room rotation
        int rdx, rdz;
        RotateXZ(ldx, ldz, rotation, rdx, rdz);
        if (rdx == wdx && rdz == wdz) return true;
    }
    return false;
}

int MazeGenerator::PickWeightedRandomWithDoors(const RoomSet& set, std::mt19937& rng)
{
    // Build a filtered list of rooms that have at least one door
    std::vector<int> doorIndices;
    for (int i = 0; i < (int)set.Rooms.size(); ++i)
        if (!set.Rooms[i].Doors.empty()) doorIndices.push_back(i);

    if (doorIndices.empty())
        return PickWeightedRandom(set, rng);  // fallback: no door rooms exist

    // Weighted pick from the door rooms
    float total = 0.0f;
    for (int i : doorIndices) total += std::max(set.Rooms[i].Rarity, 0.001f);
    std::uniform_real_distribution<float> dist(0.0f, total);
    float roll = dist(rng);
    float accum = 0.0f;
    for (int i : doorIndices)
    {
        accum += std::max(set.Rooms[i].Rarity, 0.001f);
        if (roll <= accum) return i;
    }
    return doorIndices.back();
}

void MazeGenerator::RegisterDoorExits(Scene& scene, int genIdx,
                                       const RoomTemplate& tmpl,
                                       int ax, int az, int rotation,
                                       MazeGeneratorComponent::LiveRoom& liveRoom)
{
    auto& gen = scene.MazeGenerators[genIdx];

    for (const auto& door : tmpl.Doors)
    {
        // Rotate block position
        int rx, rz;
        RotateXZ(door.BlockX, door.BlockZ, rotation, rx, rz);
        int blockWorldX = ax + rx;
        int blockWorldZ = az + rz;

        // Rotate door face direction
        int ldx, ldz;
        DoorFaceToLocalDelta(door.Face, ldx, ldz);
        int rdx, rdz;
        RotateXZ(ldx, ldz, rotation, rdx, rdz);

        // Exit cell is one step beyond the door's block face
        int exitX = blockWorldX + rdx;
        int exitZ = blockWorldZ + rdz;

        // Skip if exit cell is already occupied
        bool occupied = false;
        for (const auto& occ : gen.OccupiedCells)
            if (occ.first == exitX && occ.second == exitZ) { occupied = true; break; }
        if (occupied) continue;

        // Skip if already in PendingDoorSlots
        bool existing = false;
        for (const auto& slot : gen.PendingDoorSlots)
            if (slot.gridX == exitX && slot.gridZ == exitZ) { existing = true; break; }
        if (existing) continue;

        // The connecting room must have a door facing BACK toward us = opposite face
        DoorFace required = OppositeDoorFace(DeltaToDoorFace(rdx, rdz));

        MazeGeneratorComponent::DoorSlot slot;
        slot.gridX        = exitX;
        slot.gridZ        = exitZ;
        slot.requiredFace = required;
        gen.PendingDoorSlots.push_back(slot);

        // Track in the live room so we can remove this slot on despawn
        liveRoom.DoorExitCells.push_back({ exitX, exitZ });
    }
}

// ----------------------------------------------------------------
// Pick weighted random (rarity-based)
// ----------------------------------------------------------------
int MazeGenerator::PickWeightedRandom(const RoomSet& set, std::mt19937& rng)
{
    if (set.Rooms.empty()) return -1;
    float total = 0.0f;
    for (const auto& r : set.Rooms) total += std::max(r.Rarity, 0.001f);
    std::uniform_real_distribution<float> dist(0.0f, total);
    float roll = dist(rng);
    float accum = 0.0f;
    for (int i = 0; i < (int)set.Rooms.size(); ++i)
    {
        accum += std::max(set.Rooms[i].Rarity, 0.001f);
        if (roll <= accum) return i;
    }
    return (int)set.Rooms.size() - 1;
}

// ----------------------------------------------------------------
// Spawn mesh entities for each block + interior decoration
// ----------------------------------------------------------------
void MazeGenerator::SpawnRoomEntities(Scene& scene,
                                       const RoomTemplate& room,
                                       int ax, int az, int rotation,
                                       float blockSize,
                                       const glm::vec3& genPos,
                                       MazeGeneratorComponent::LiveRoom& live)
{
    // For each block, create a cube entity
    for (const auto& b : room.Blocks)
    {
        int rx, rz;
        RotateXZ(b.X, b.Z, rotation, rx, rz);
        float wx = genPos.x + (ax + rx) * blockSize;
        float wy = genPos.y + b.Y * blockSize;
        float wz = genPos.z + (az + rz) * blockSize;

        Entity e = scene.CreateEntity("MazeBlock");
        int eid = (int)scene.Transforms.size() - 1;

        scene.Transforms[eid].Position = glm::vec3(wx, wy, wz);
        scene.Transforms[eid].Scale    = glm::vec3(blockSize, blockSize, blockSize);

        // Assign a cube mesh
        Mesh* mesh = new Mesh(Mesh::CreateCube("#AAAAAA"));
        scene.MeshRenderers[eid].MeshPtr  = mesh;
        scene.MeshRenderers[eid].MeshType = "cube";

        // Remove from RootOrder display (maze entities shouldn't clutter hierarchy)
        // They're still in the scene arrays but hidden from the user hierarchy.

        live.EntityIds.push_back(eid);
    }

    // Spawn interior decoration (prefab-style nodes inside the room)
    for (const auto& node : room.Interior)
    {
        Entity e = scene.CreateEntity(node.Name.empty() ? "MazeProp" : node.Name);
        int eid = (int)scene.Transforms.size() - 1;

        // Room-local transform → rotate + offset to world
        float rx = node.Transform.Position.x;
        float rz = node.Transform.Position.z;
        float cosA = 1.0f, sinA = 0.0f;
        if (rotation == 90)       { cosA = 0; sinA = 1; }
        else if (rotation == 180) { cosA = -1; sinA = 0; }
        else if (rotation == 270) { cosA = 0; sinA = -1; }

        float wrx = rx * cosA - rz * sinA;
        float wrz = rx * sinA + rz * cosA;

        scene.Transforms[eid].Position = glm::vec3(
            genPos.x + ax * blockSize + wrx * blockSize,
            genPos.y + node.Transform.Position.y * blockSize,
            genPos.z + az * blockSize + wrz * blockSize
        );
        scene.Transforms[eid].Rotation = node.Transform.Rotation;
        scene.Transforms[eid].Scale    = node.Transform.Scale;

        if (!node.MeshType.empty())
        {
            Mesh* mesh = nullptr;
            const std::string& mt = node.MeshType;
            if      (mt == "cube")     mesh = new Mesh(Mesh::CreateCube("#DDDDDD"));
            else if (mt == "sphere")   mesh = new Mesh(Mesh::CreateSphere("#DDDDDD"));
            else if (mt == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid("#DDDDDD"));
            else if (mt == "cylinder") mesh = new Mesh(Mesh::CreateCylinder("#DDDDDD"));
            else if (mt == "capsule")  mesh = new Mesh(Mesh::CreateCapsule("#DDDDDD"));
            else if (mt == "plane")    mesh = new Mesh(Mesh::CreatePlane("#DDDDDD"));
            else if (mt.size() > 4 && mt.substr(0, 4) == "obj:")
                mesh = new Mesh(Mesh::LoadOBJ(mt.substr(4)));
            if (mesh)
            {
                scene.MeshRenderers[eid].MeshPtr  = mesh;
                scene.MeshRenderers[eid].MeshType = mt;
            }
        }

        live.EntityIds.push_back(eid);
    }

    // ---- Apply lightmap to every spawned entity ----
    if (!room.LightmapPath.empty())
    {
        unsigned int lmID = LightmapManager::Instance().Load(room.LightmapPath);
        if (lmID != 0)
        {
            live.LightmapPath = room.LightmapPath;  // store for release on despawn
            for (int eid : live.EntityIds)
            {
                scene.MeshRenderers[eid].LightmapID = lmID;
                scene.MeshRenderers[eid].LightmapST = room.LightmapST;
            }
        }
    }
}

// ----------------------------------------------------------------
// Try to place a room at (gridX, gridZ)
// Uses index-based access to avoid dangling references after CreateEntity.
// ----------------------------------------------------------------
bool MazeGenerator::TrySpawnRoom(Scene& scene,
                                  int genIdx, int roomSetIdx,
                                  int gridX, int gridZ,
                                  float blockSize,
                                  const glm::vec3& genPos)
{
    const RoomSet& set = scene.RoomSets[roomSetIdx];
    if (set.Rooms.empty()) return false;

    auto& rng = GetRNG();

    // ---- Check if this cell is a pending door slot ----
    bool isDoorSlot   = false;
    DoorFace reqFace  = DoorFace::PosX;
    for (const auto& slot : scene.MazeGenerators[genIdx].PendingDoorSlots)
    {
        if (slot.gridX == gridX && slot.gridZ == gridZ)
        {
            isDoorSlot = true;
            reqFace     = slot.requiredFace;
            break;
        }
    }

    // ---- Build candidate template list ----
    // If this is a door slot, prefer templates that have a matching door.
    // Try first with door-matching filter, then fall back to any template.
    std::vector<int> preferred, fallback;
    for (int i = 0; i < (int)set.Rooms.size(); ++i)
    {
        if (set.Rooms[i].Blocks.empty()) continue;
        static const int rots[] = {0, 90, 180, 270};
        bool hasMatchRot = false;
        for (int rot : rots)
        {
            if (isDoorSlot && TemplateHasDoorOnWorldFace(set.Rooms[i], reqFace, rot))
            { hasMatchRot = true; break; }
        }
        if (isDoorSlot && hasMatchRot) preferred.push_back(i);
        else fallback.push_back(i);
    }
    if (preferred.empty()) preferred = fallback; // nothing matched → allow anything

    // ---- Attempt placement with preferred templates first ----
    // Shuffle preferred list for fairness
    std::shuffle(preferred.begin(), preferred.end(), rng);

    for (int tIdx : preferred)
    {
        if (tIdx < 0 || tIdx >= (int)set.Rooms.size()) continue;
        const RoomTemplate& tmpl = scene.RoomSets[roomSetIdx].Rooms[tIdx];
        if (tmpl.Blocks.empty()) continue;

        // Build rotation list: if door slot, try door-matching rotations first
        std::vector<int> rotOrder = {0, 90, 180, 270};
        if (isDoorSlot)
        {
            std::vector<int> matchingRots, otherRots;
            for (int r : rotOrder)
            {
                if (TemplateHasDoorOnWorldFace(tmpl, reqFace, r))
                    matchingRots.push_back(r);
                else
                    otherRots.push_back(r);
            }
            std::shuffle(matchingRots.begin(), matchingRots.end(), rng);
            std::shuffle(otherRots.begin(), otherRots.end(), rng);
            rotOrder.clear();
            for (int r : matchingRots) rotOrder.push_back(r);
            for (int r : otherRots)    rotOrder.push_back(r);
        }
        else
        {
            std::shuffle(rotOrder.begin(), rotOrder.end(), rng);
        }

        for (int rot : rotOrder)
        {
            if (!CellsFree(scene.MazeGenerators[genIdx], tmpl, gridX, gridZ, rot)) continue;

            MazeGeneratorComponent::LiveRoom liveRoom;
            liveRoom.TemplateIdx = tIdx;
            liveRoom.GridX       = gridX;
            liveRoom.GridZ       = gridZ;
            liveRoom.Rotation    = rot;

            // Mark occupied cells first
            for (const auto& b : tmpl.Blocks)
            {
                int rx, rz;
                RotateXZ(b.X, b.Z, rot, rx, rz);
                scene.MazeGenerators[genIdx].OccupiedCells.push_back({ gridX + rx, gridZ + rz });
            }

            // Spawn entities
            SpawnRoomEntities(scene, tmpl, gridX, gridZ, rot, blockSize, genPos, liveRoom);

            // Register door exits (after spawn so gen is up to date)
            RegisterDoorExits(scene, genIdx, tmpl, gridX, gridZ, rot, liveRoom);

            // If this cell was a pending door slot, remove it
            if (isDoorSlot)
            {
                auto& slots = scene.MazeGenerators[genIdx].PendingDoorSlots;
                slots.erase(std::remove_if(slots.begin(), slots.end(),
                    [gridX, gridZ](const MazeGeneratorComponent::DoorSlot& s) {
                        return s.gridX == gridX && s.gridZ == gridZ;
                    }), slots.end());
            }

            scene.MazeGenerators[genIdx].SpawnedRooms.push_back(std::move(liveRoom));
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------
// Main update: spawn/despawn rooms around player
// ----------------------------------------------------------------
void MazeGenerator::Update(Scene& scene, float dt)
{
    (void)dt;

    glm::vec3 playerPos;
    if (!GetPlayerPosition(scene, playerPos)) return;

    // Snapshot count — new entities created by spawning append to the end
    // and should NOT be iterated as generators.
    const int genCount = (int)scene.MazeGenerators.size();

    for (int gi = 0; gi < genCount; ++gi)
    {
        // Use index-based access throughout to avoid dangling references
        // after CreateEntity / DeleteEntity reallocate the parallel arrays.
        if (!scene.MazeGenerators[gi].Active || !scene.MazeGenerators[gi].Enabled) continue;
        int roomSetIdx = scene.MazeGenerators[gi].RoomSetIndex;
        if (roomSetIdx < 0 || roomSetIdx >= (int)scene.RoomSets.size()) continue;
        if (scene.RoomSets[roomSetIdx].Rooms.empty()) continue;

        glm::vec3 genPos = scene.GetEntityWorldPos(gi);
        float blockSize  = std::max(scene.MazeGenerators[gi].BlockSize, 0.5f);
        float genRad     = scene.MazeGenerators[gi].GenerateRadius;
        float desRad     = scene.MazeGenerators[gi].DespawnRadius;

        // 1) Despawn rooms that are too far from the player
        for (int ri = (int)scene.MazeGenerators[gi].SpawnedRooms.size() - 1; ri >= 0; --ri)
        {
            auto& room = scene.MazeGenerators[gi].SpawnedRooms[ri];
            float cx = genPos.x + room.GridX * blockSize;
            float cz = genPos.z + room.GridZ * blockSize;
            float dist = std::sqrt((playerPos.x - cx) * (playerPos.x - cx) +
                                    (playerPos.z - cz) * (playerPos.z - cz));
            if (dist > desRad)
            {
                // Remove occupied cells for this room
                const auto& rset = scene.RoomSets[roomSetIdx];
                if (room.TemplateIdx >= 0 && room.TemplateIdx < (int)rset.Rooms.size())
                {
                    const auto& tmpl = rset.Rooms[room.TemplateIdx];
                    for (const auto& b : tmpl.Blocks)
                    {
                        int rx, rz;
                        RotateXZ(b.X, b.Z, room.Rotation, rx, rz);
                        int wx = room.GridX + rx;
                        int wz = room.GridZ + rz;
                        auto& occ = scene.MazeGenerators[gi].OccupiedCells;
                        auto it = std::find(occ.begin(), occ.end(), std::make_pair(wx, wz));
                        if (it != occ.end()) occ.erase(it);
                    }
                }

                // Remove pending door exits registered by this room
                auto& slots = scene.MazeGenerators[gi].PendingDoorSlots;
                for (const auto& exitCell : room.DoorExitCells)
                {
                    slots.erase(std::remove_if(slots.begin(), slots.end(),
                        [&exitCell](const MazeGeneratorComponent::DoorSlot& s) {
                            return s.gridX == exitCell.first && s.gridZ == exitCell.second;
                        }), slots.end());
                }

                // Release lightmap (if any) before erasing entities
                if (!room.LightmapPath.empty())
                    LightmapManager::Instance().Release(room.LightmapPath);

                // Copy entity IDs before deletion (room ref is about to be erased)
                std::vector<int> deadIds = room.EntityIds;
                std::sort(deadIds.begin(), deadIds.end(), std::greater<int>());

                for (int eid : deadIds)
                {
                    if (eid >= 0 && eid < (int)scene.Transforms.size())
                        scene.DeleteEntity(eid);
                }

                // Fix entity IDs in other live rooms after deletions
                for (int rj = 0; rj < (int)scene.MazeGenerators[gi].SpawnedRooms.size(); ++rj)
                {
                    if (rj == ri) continue;
                    for (auto& eid : scene.MazeGenerators[gi].SpawnedRooms[rj].EntityIds)
                        for (int deletedId : deadIds)
                            if (eid > deletedId) --eid;
                }

                scene.MazeGenerators[gi].SpawnedRooms.erase(
                    scene.MazeGenerators[gi].SpawnedRooms.begin() + ri);
            }
        }

        // 2a) Process pending door slots: try to fill open door exits first
        //     (Iterate a copy since TrySpawnRoom may modify PendingDoorSlots)
        {
            std::vector<MazeGeneratorComponent::DoorSlot> slotsCopy =
                scene.MazeGenerators[gi].PendingDoorSlots;

            for (const auto& slot : slotsCopy)
            {
                // Skip if gone (was filled by a previous iteration in this frame)
                bool stillPending = false;
                for (const auto& s : scene.MazeGenerators[gi].PendingDoorSlots)
                    if (s.gridX == slot.gridX && s.gridZ == slot.gridZ) { stillPending = true; break; }
                if (!stillPending) continue;

                // Only actually fill what's within spawn radius
                float wx = genPos.x + slot.gridX * blockSize;
                float wz = genPos.z + slot.gridZ * blockSize;
                float dist2 = std::sqrt((playerPos.x - wx) * (playerPos.x - wx) +
                                         (playerPos.z - wz) * (playerPos.z - wz));
                if (dist2 > genRad) continue;

                // Check not already occupied
                bool occupied = false;
                for (const auto& occ : scene.MazeGenerators[gi].OccupiedCells)
                    if (occ.first == slot.gridX && occ.second == slot.gridZ) { occupied = true; break; }
                if (occupied)
                {
                    // Cell is now occupied; remove this slot
                    auto& pendSlots = scene.MazeGenerators[gi].PendingDoorSlots;
                    pendSlots.erase(std::remove_if(pendSlots.begin(), pendSlots.end(),
                        [&slot](const MazeGeneratorComponent::DoorSlot& s) {
                            return s.gridX == slot.gridX && s.gridZ == slot.gridZ;
                        }), pendSlots.end());
                    continue;
                }

                TrySpawnRoom(scene, gi, roomSetIdx, slot.gridX, slot.gridZ, blockSize, genPos);
            }
        }

        // 2b) Spawn rooms in empty grid cells around the player within generate radius
        int gridRadius = (int)std::ceil(genRad / blockSize);
        int pgx = (int)std::round((playerPos.x - genPos.x) / blockSize);
        int pgz = (int)std::round((playerPos.z - genPos.z) / blockSize);

        for (int r = 0; r <= gridRadius; ++r)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                for (int dz = -r; dz <= r; ++dz)
                {
                    if (std::abs(dx) != r && std::abs(dz) != r) continue;

                    int gx = pgx + dx;
                    int gz = pgz + dz;

                    float wx = genPos.x + gx * blockSize;
                    float wz = genPos.z + gz * blockSize;
                    float dist2Player = std::sqrt((playerPos.x - wx) * (playerPos.x - wx) +
                                                   (playerPos.z - wz) * (playerPos.z - wz));
                    if (dist2Player > genRad) continue;

                    // Re-access occupied cells via index (safe after prior spawns)
                    bool occupied = false;
                    for (const auto& occ : scene.MazeGenerators[gi].OccupiedCells)
                    {
                        if (occ.first == gx && occ.second == gz) { occupied = true; break; }
                    }
                    if (occupied) continue;

                    TrySpawnRoom(scene, gi, roomSetIdx, gx, gz, blockSize, genPos);
                }
            }
        }
    }
}

} // namespace tsu
