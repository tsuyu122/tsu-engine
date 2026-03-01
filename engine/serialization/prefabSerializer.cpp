#include "serialization/prefabSerializer.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace tsu {

static std::string ptrim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static glm::vec3 pParseVec3(const std::string& s)
{
    std::istringstream ss(s);
    glm::vec3 v{0,0,0};
    ss >> v.x >> v.y >> v.z;
    return v;
}

// ----------------------------------------------------------------
// Save  – writes one [node] section per entry in p.Nodes
// ----------------------------------------------------------------
void PrefabSerializer::Save(const PrefabAsset& p, const std::string& filepath)
{
    std::ofstream f(filepath);
    if (!f.is_open())
    {
        std::cerr << "[PrefabSerializer] Cannot open for write: " << filepath << "\n";
        return;
    }

    f << "# tsuEngine Prefab File\n";
    f << "name = " << p.Name << "\n\n";

    for (int ni = 0; ni < (int)p.Nodes.size(); ++ni)
    {
        const PrefabEntityData& n = p.Nodes[ni];
        f << "[node]\n";
        f << "node_idx    = " << ni           << "\n";
        f << "node_name   = " << n.Name       << "\n";
        f << "node_parent = " << n.ParentIdx  << "\n";

        if (!n.MeshType.empty())     f << "node_mesh     = " << n.MeshType     << "\n";
        if (!n.MaterialName.empty()) f << "node_material = " << n.MaterialName << "\n";

        const auto& t = n.Transform;
        f << "position = " << t.Position.x << " " << t.Position.y << " " << t.Position.z << "\n";
        f << "rotation = " << t.Rotation.x << " " << t.Rotation.y << " " << t.Rotation.z << "\n";
        f << "scale    = " << t.Scale.x    << " " << t.Scale.y    << " " << t.Scale.z    << "\n";

        if (n.HasLight)
        {
            static const char* ltNames[] = {"directional","point","spot","area"};
            const LightComponent& lc = n.Light;
            f << "has_light       = true\n";
            f << "light_type      = " << ltNames[(int)lc.Type] << "\n";
            f << "light_color     = " << lc.Color.r << " " << lc.Color.g << " " << lc.Color.b << "\n";
            f << "light_temp      = " << lc.Temperature  << "\n";
            f << "light_intensity = " << lc.Intensity    << "\n";
            f << "light_range     = " << lc.Range        << "\n";
            f << "light_inner     = " << lc.InnerAngle   << "\n";
            f << "light_outer     = " << lc.OuterAngle   << "\n";
            f << "light_width     = " << lc.Width        << "\n";
            f << "light_height    = " << lc.Height       << "\n";
        }

        if (n.HasCamera)
        {
            const GameCameraComponent& gc = n.Camera;
            f << "has_camera = true\n";
            f << "cam_fov    = " << gc.FOV   << "\n";
            f << "cam_near   = " << gc.Near  << "\n";
            f << "cam_far    = " << gc.Far   << "\n";
            f << "cam_yaw    = " << gc.Yaw   << "\n";
            f << "cam_pitch  = " << gc.Pitch << "\n";
        }
        f << "\n";
    }
}

// ----------------------------------------------------------------
// Load  – reads [node] sections and fills p.Nodes
// ----------------------------------------------------------------
bool PrefabSerializer::Load(PrefabAsset& p, const std::string& filepath)
{
    std::ifstream f(filepath);
    if (!f.is_open())
    {
        std::cerr << "[PrefabSerializer] Cannot open: " << filepath << "\n";
        return false;
    }

    p.FilePath = filepath;
    PrefabEntityData cur;
    bool inNode = false;

    auto flushNode = [&]()
    {
        if (inNode) p.Nodes.push_back(cur);
        cur = PrefabEntityData{};
        inNode = false;
    };

    std::string line;
    while (std::getline(f, line))
    {
        line = ptrim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line == "[node]") { flushNode(); inNode = true; continue; }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = ptrim(line.substr(0, eq));
        std::string v = ptrim(line.substr(eq + 1));

        if (!inNode)
        {
            if (k == "name") p.Name = v;
            continue;
        }

        // Node-level keys
        if      (k == "node_name")     cur.Name         = v;
        else if (k == "node_parent")   cur.ParentIdx    = std::stoi(v);
        else if (k == "node_mesh")     cur.MeshType     = v;
        else if (k == "node_material") cur.MaterialName = v;
        else if (k == "position")      cur.Transform.Position = pParseVec3(v);
        else if (k == "rotation")      cur.Transform.Rotation = pParseVec3(v);
        else if (k == "scale")         cur.Transform.Scale    = pParseVec3(v);
        else if (k == "has_light")     cur.HasLight           = (v == "true");
        else if (k == "light_type")
        {
            if      (v == "point") cur.Light.Type = LightType::Point;
            else if (v == "spot")  cur.Light.Type = LightType::Spot;
            else if (v == "area")  cur.Light.Type = LightType::Area;
            else                   cur.Light.Type = LightType::Directional;
        }
        else if (k == "light_color")     cur.Light.Color       = pParseVec3(v);
        else if (k == "light_temp")      cur.Light.Temperature = std::stof(v);
        else if (k == "light_intensity") cur.Light.Intensity   = std::stof(v);
        else if (k == "light_range")     cur.Light.Range       = std::stof(v);
        else if (k == "light_inner")     cur.Light.InnerAngle  = std::stof(v);
        else if (k == "light_outer")     cur.Light.OuterAngle  = std::stof(v);
        else if (k == "light_width")     cur.Light.Width       = std::stof(v);
        else if (k == "light_height")    cur.Light.Height      = std::stof(v);
        else if (k == "has_camera")      cur.HasCamera         = (v == "true");
        else if (k == "cam_fov")         cur.Camera.FOV        = std::stof(v);
        else if (k == "cam_near")        cur.Camera.Near       = std::stof(v);
        else if (k == "cam_far")         cur.Camera.Far        = std::stof(v);
        else if (k == "cam_yaw")         cur.Camera.Yaw        = std::stof(v);
        else if (k == "cam_pitch")       cur.Camera.Pitch      = std::stof(v);
    }
    flushNode(); // flush the last node
    return !p.Nodes.empty();
}

} // namespace tsu
