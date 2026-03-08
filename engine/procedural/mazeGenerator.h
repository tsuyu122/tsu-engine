#pragma once
#include "scene/scene.h"
#include <random>
#include <unordered_set>
#include <unordered_map>

namespace tsu {

// ================================================================
// MazeGenerator  --  runtime procedural labyrinth system
//
//  Finds the active generator entity + active PlayerController,
//  spawns rooms within GenerateRadius, despawns beyond DespawnRadius.
//  100% random (no seed, no history).  Door-aligned, no overlap.
// ================================================================

class MazeGenerator
{
public:
    // Call once per frame during Game mode.
    // Finds all generator entities, spawns/despawns rooms around player.
    static void Update(Scene& scene, float dt);

private:
    // Pack grid coords into a single 64-bit key for the occupancy map
    static int64_t PackCell(int gx, int gz)
    {
        return ((int64_t)(uint32_t)gx << 32) | (int64_t)(uint32_t)gz;
    }

    // Get the player world position (first active PlayerController entity)
    static bool GetPlayerPosition(const Scene& scene, glm::vec3& outPos);

    // Attempt to spawn a room anchored at (gridX,gridZ) using a template from the set.
    // Returns true if placed successfully (no overlap, doors can align).
    // Uses indices instead of references because CreateEntity can reallocate vectors.
    static bool TrySpawnRoom(Scene& scene,
                             int genIdx, int roomSetIdx,
                             int gridX, int gridZ,
                             float blockSize,
                             const glm::vec3& generatorWorldPos);

    // Pick a random room template from the set weighted by Rarity
    static int PickWeightedRandom(const RoomSet& set, std::mt19937& rng);

    // Check if all cells a room would occupy are free
    static bool CellsFree(const MazeGeneratorComponent& gen,
                          const RoomTemplate& room,
                          int anchorX, int anchorZ, int rotation);

    // Rotate a block's local (x,z) by 0/90/180/270 degrees
    static void RotateXZ(int localX, int localZ, int rotation, int& outX, int& outZ);

    // Spawn mesh entities for each block of a room template
    static void SpawnRoomEntities(Scene& scene,
                                  const RoomTemplate& room,
                                  int anchorX, int anchorZ,
                                  int rotation,
                                  float blockSize,
                                  const glm::vec3& generatorWorldPos,
                                  MazeGeneratorComponent::LiveRoom& liveRoom);

    // Thread-local RNG
    static std::mt19937& GetRNG();

    // ---- Door-connectivity helpers ----

    // Convert a DoorFace to the local (dx, dz) direction it exits toward
    static void DoorFaceToLocalDelta(DoorFace f, int& dx, int& dz);

    // Return the opposite DoorFace (the face a connecting room must have)
    static DoorFace OppositeDoorFace(DoorFace f);

    // Check if a template, when placed with the given rotation, has at least
    // one door that faces in the given world DoorFace direction.
    // worldFace is already in world-space (not local).
    static bool TemplateHasDoorOnWorldFace(const RoomTemplate& tmpl,
                                            DoorFace worldFace, int rotation);

    // Pick a room that has at least one door.  Falls back to any random room.
    static int PickWeightedRandomWithDoors(const RoomSet& set, std::mt19937& rng);

    // After spawning a room: compute its door exits → add to generator's PendingDoorSlots
    // Also stores the exit cells in liveRoom.DoorExitCells for cleanup on despawn.
    static void RegisterDoorExits(Scene& scene, int genIdx,
                                  const RoomTemplate& tmpl,
                                  int anchorX, int anchorZ, int rotation,
                                  MazeGeneratorComponent::LiveRoom& liveRoom);
};

} // namespace tsu
