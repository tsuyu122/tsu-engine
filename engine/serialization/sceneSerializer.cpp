#include "serialization/sceneSerializer.h"
#include "scene/scene.h"    // contém ColliderType, RigidBodyComponent, GameCameraComponent
#include "scene/entity.h"
#include "renderer/mesh.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace tsu {

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static glm::vec3 parseVec3(const std::string& s)
{
    std::istringstream ss(s);
    glm::vec3 v{0,0,0};
    ss >> v.x >> v.y >> v.z;
    return v;
}

void SceneSerializer::Load(Scene& scene, const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "[SceneSerializer] Nao encontrado: " << filepath << "\n";
        return;
    }

    struct EntityData {
        std::string type     = "";
        std::string name     = "Entity";
        std::string color    = "#FFFFFF";
        std::string collider = "";
        glm::vec3   position = {0,0,0};
        glm::vec3   rotation = {0,0,0};
        glm::vec3   scale    = {1,1,1};
        bool        valid        = false;
        bool        hasRigidBody = false;
        bool        kinematic    = false;
        bool        useGravity   = true;
        float       mass         = 1.0f;
        bool        gravModule   = false;
        bool        colModule    = false;
        float       camFOV       = 75.0f;
        float       camYaw       = -90.0f;
        float       camPitch     = 0.0f;
    };

    auto flush = [&](EntityData& d)
    {
        if (!d.valid) return;

        Entity e    = scene.CreateEntity(d.name);
        uint32_t id = e.GetID();

        scene.Transforms[id].Position = d.position;
        scene.Transforms[id].Rotation = d.rotation;
        scene.Transforms[id].Scale    = d.scale;

        if (d.type == "game_camera")
        {
            GameCameraComponent& gc = scene.GameCameras[id];
            gc.Active = true;
            gc.FOV    = d.camFOV;
            gc.Yaw    = d.camYaw;
            gc.Pitch  = d.camPitch;
            gc.UpdateVectors();
        }
        else
        {
            Mesh* mesh = nullptr;
            if      (d.type == "cube")     mesh = new Mesh(Mesh::CreateCube(d.color));
            else if (d.type == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid(d.color));
            else if (d.type == "cylinder") mesh = new Mesh(Mesh::CreateCylinder(d.color));
            else if (d.type == "sphere")   mesh = new Mesh(Mesh::CreateSphere(d.color));
            else if (d.type == "capsule")  mesh = new Mesh(Mesh::CreateCapsule(d.color));
            else
                std::cerr << "[SceneSerializer] Tipo desconhecido: " << d.type << "\n";

            scene.MeshRenderers[id].MeshPtr = mesh;

            if (d.hasRigidBody)
            {
                RigidBodyComponent& rb = scene.RigidBodies[id];
                rb.IsKinematic     = d.kinematic;
                rb.UseGravity      = d.useGravity;
                rb.Mass            = d.mass;
                // If explicit module flags were saved, use them; otherwise both on (legacy files)
                rb.HasGravityModule  = d.gravModule || (!d.gravModule && !d.colModule && d.hasRigidBody);
                rb.HasColliderModule = d.colModule  || (!d.gravModule && !d.colModule && d.hasRigidBody);

                if      (d.collider == "box")     rb.Collider = ColliderType::Box;
                else if (d.collider == "sphere")  rb.Collider = ColliderType::Sphere;
                else if (d.collider == "capsule") rb.Collider = ColliderType::Capsule;
                else if (d.collider == "pyramid") rb.Collider = ColliderType::Pyramid;
                else if (d.collider == "none")    rb.Collider = ColliderType::None;
                else
                {
                    // Auto-detecta pelo tipo de mesh
                    if      (d.type == "sphere")  rb.Collider = ColliderType::Sphere;
                    else if (d.type == "capsule") rb.Collider = ColliderType::Capsule;
                    else if (d.type == "pyramid") rb.Collider = ColliderType::Pyramid;
                    else                          rb.Collider = ColliderType::Box;
                }
            }
        }

        d = EntityData{};
    };

    EntityData cur;
    std::string line;

    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line == "[entity]")
        {
            flush(cur);
            cur.valid = true;
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k   = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if      (k == "type")      cur.type        = val;
        else if (k == "name")      cur.name        = val;
        else if (k == "color")     cur.color       = val;
        else if (k == "position")  cur.position    = parseVec3(val);
        else if (k == "rotation")  cur.rotation    = parseVec3(val);
        else if (k == "scale")     cur.scale       = parseVec3(val);
        else if (k == "rigidbody") cur.hasRigidBody= (val == "true");
        else if (k == "kinematic") cur.kinematic   = (val == "true");
        else if (k == "gravity")   cur.useGravity  = (val == "true");
        else if (k == "mass")      cur.mass        = std::stof(val);
        else if (k == "collider")  cur.collider    = val;
        else if (k == "fov")         cur.camFOV      = std::stof(val);
        else if (k == "yaw")         cur.camYaw      = std::stof(val);
        else if (k == "pitch")       cur.camPitch    = std::stof(val);
        else if (k == "grav_module") cur.gravModule  = (val == "true");
        else if (k == "col_module")  cur.colModule   = (val == "true");
    }

    flush(cur);
}

void SceneSerializer::Save(const Scene& scene, const std::string& filepath)
{
    std::ofstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "[SceneSerializer] Erro ao salvar: " << filepath << "\n";
        return;
    }

    file << "# tsuEngine Scene File\n\n";

    for (size_t i = 0; i < scene.Transforms.size(); i++)
    {
        const TransformComponent&    t  = scene.Transforms[i];
        const MeshRendererComponent& mr = scene.MeshRenderers[i];
        const RigidBodyComponent&    rb = scene.RigidBodies[i];
        const GameCameraComponent&   gc = scene.GameCameras[i];

        file << "[entity]\n";

        // Name (always saved so it round-trips
        std::string entityName = (i < scene.EntityNames.size()) ? scene.EntityNames[i] : "Entity";
        file << "name  = " << entityName << "\n";

        if (gc.Active)
        {
            file << "type  = game_camera\n"
                 << "fov   = " << gc.FOV   << "\n"
                 << "yaw   = " << gc.Yaw   << "\n"
                 << "pitch = " << gc.Pitch << "\n";
        }
        else if (mr.MeshPtr)
        {
            file << "type  = cube\ncolor = #FFFFFF\n";
            if (rb.HasGravityModule || rb.HasColliderModule)
            {
                file << "rigidbody = true\n"
                     << "kinematic = " << (rb.IsKinematic ? "true" : "false") << "\n"
                     << "gravity   = " << (rb.UseGravity  ? "true" : "false") << "\n"
                     << "mass      = " << rb.Mass << "\n"
                     << "grav_module = " << (rb.HasGravityModule  ? "true" : "false") << "\n"
                     << "col_module  = " << (rb.HasColliderModule ? "true" : "false") << "\n";
            }
        }

        file << "position = " << t.Position.x << " " << t.Position.y << " " << t.Position.z << "\n"
             << "rotation = " << t.Rotation.x << " " << t.Rotation.y << " " << t.Rotation.z << "\n"
             << "scale    = " << t.Scale.x    << " " << t.Scale.y    << " " << t.Scale.z    << "\n\n";
    }
}

} // namespace tsu