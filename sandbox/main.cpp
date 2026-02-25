#include "core/application.h"
#include "scene/scene.h"
#include "serialization/sceneSerializer.h"

using namespace tsu;

int main()
{
    Application app;
    SceneSerializer::Load(app.GetScene(), "assets/scenes/default.tscene");
    app.Run();
    return 0;
}