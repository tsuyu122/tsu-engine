// GameRuntime entry point — no editor, no ImGui, just the game.
#include "core/gameApp.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

using namespace tsu;

static int RunGame()
{
    GameApplication app;
    app.Run();
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return RunGame();
}
#else
int main()
{
    return RunGame();
}
#endif
