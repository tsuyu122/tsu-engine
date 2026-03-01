#include "core/application.h"
#include "scene/scene.h"
#include "serialization/sceneSerializer.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace tsu;

static int RunEngine()
{
    Application app;
    SceneSerializer::Load(app.GetScene(), "assets/scenes/default.tscene");
    app.Run();
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return RunEngine();
}
#else
int main()
{
    return RunEngine();
}
#endif