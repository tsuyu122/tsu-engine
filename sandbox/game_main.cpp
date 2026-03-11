// GameRuntime entry point — no editor, no ImGui, just the game.
#include "core/gameApp.h"
#include <vector>
#include <string>
#include <sstream>

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

static std::vector<std::string> ParseArgsString(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}

static int RunGame(const std::vector<std::string>& args)
{
    GameApplication app(args);
    app.Run();
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    std::string cmd = GetCommandLineA();
    return RunGame(ParseArgsString(cmd));
}
#else
int main(int argc, char** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i] ? argv[i] : "");
    return RunGame(args);
}
#endif
