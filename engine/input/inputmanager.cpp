#include "input/inputManager.h"
#include <GLFW/glfw3.h>
#ifdef TSU_EDITOR
#  include <imgui_impl_glfw.h>
#endif
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace tsu {

GLFWwindow* InputManager::s_Window = nullptr;

bool InputManager::s_Keys[349]     = {};
bool InputManager::s_KeysPrev[349] = {};

bool InputManager::s_MouseButtons[8]     = {};
bool InputManager::s_MouseButtonsPrev[8] = {};

double InputManager::s_MouseX     = 0.0;
double InputManager::s_MouseY     = 0.0;
double InputManager::s_LastMouseX = 0.0;
double InputManager::s_LastMouseY = 0.0;
bool   InputManager::s_FirstMouse = true;
float  InputManager::s_ScrollDeltaThisFrame = 0.0f;

static float g_ScrollAccum = 0.0f;   // file-local accumulator fed by callback

static void ScrollCallback(GLFWwindow* w, double xoff, double yoff)
{
    g_ScrollAccum += (float)yoff;
#ifdef TSU_EDITOR
    // Forward to ImGui so inspector/panels can scroll with mouse wheel
    ImGui_ImplGlfw_ScrollCallback(w, xoff, yoff);
#endif
}

void InputManager::Update(GLFWwindow* window)
{
    if (s_Window != window)
    {
        s_Window = window;
        glfwSetScrollCallback(window, ScrollCallback);
    }
    memcpy(s_KeysPrev,         s_Keys,         sizeof(s_Keys));
    memcpy(s_MouseButtonsPrev, s_MouseButtons, sizeof(s_MouseButtons));

    s_ScrollDeltaThisFrame = g_ScrollAccum;
    g_ScrollAccum          = 0.0f;

    for (int i = 0; i < 349; i++)
        s_Keys[i] = (glfwGetKey(window, i) == GLFW_PRESS);

    for (int i = 0; i < 8; i++)
        s_MouseButtons[i] = (glfwGetMouseButton(window, i) == GLFW_PRESS);

    s_LastMouseX = s_MouseX;
    s_LastMouseY = s_MouseY;
    glfwGetCursorPos(window, &s_MouseX, &s_MouseY);

    if (s_FirstMouse)
    {
        s_LastMouseX = s_MouseX;
        s_LastMouseY = s_MouseY;
        s_FirstMouse = false;
    }
}

bool InputManager::IsKeyPressed(int key)  { if (key < 0 || key >= 349) return false; return s_Keys[key]; }
bool InputManager::IsKeyDown(int key)     { if (key < 0 || key >= 349) return false; return s_Keys[key] && !s_KeysPrev[key]; }
bool InputManager::IsKeyUp(int key)       { if (key < 0 || key >= 349) return false; return !s_Keys[key] && s_KeysPrev[key]; }

bool InputManager::IsMousePressed(int b)  { if (b < 0 || b >= 8) return false; return s_MouseButtons[b]; }
bool InputManager::IsMouseDown(int b)     { if (b < 0 || b >= 8) return false; return s_MouseButtons[b] && !s_MouseButtonsPrev[b]; }
bool InputManager::IsMouseUp(int b)       { if (b < 0 || b >= 8) return false; return !s_MouseButtons[b] && s_MouseButtonsPrev[b]; }

MouseDelta InputManager::GetMouseDelta()
{
    MouseDelta d;
    d.x = (float)(s_MouseX - s_LastMouseX);
    d.y = (float)(s_LastMouseY - s_MouseY);
    return d;
}

void InputManager::GetMousePosition(double& x, double& y)
{
    x = s_MouseX;
    y = s_MouseY;
}

float InputManager::GetScrollDelta()
{
    return s_ScrollDeltaThisFrame;
}

// ----------------------------------------------------------------
// Key name <-> GLFW code table
// ----------------------------------------------------------------

struct KeyEntry { int code; const char* name; };
static const KeyEntry kKeyTable[] = {
    // Letters
    {65,"A"},{66,"B"},{67,"C"},{68,"D"},{69,"E"},{70,"F"},{71,"G"},
    {72,"H"},{73,"I"},{74,"J"},{75,"K"},{76,"L"},{77,"M"},{78,"N"},
    {79,"O"},{80,"P"},{81,"Q"},{82,"R"},{83,"S"},{84,"T"},{85,"U"},
    {86,"V"},{87,"W"},{88,"X"},{89,"Y"},{90,"Z"},
    // Digits row
    {48,"0"},{49,"1"},{50,"2"},{51,"3"},{52,"4"},
    {53,"5"},{54,"6"},{55,"7"},{56,"8"},{57,"9"},
    // F-Keys
    {290,"F1"},{291,"F2"},{292,"F3"},{293,"F4"},{294,"F5"},{295,"F6"},
    {296,"F7"},{297,"F8"},{298,"F9"},{299,"F10"},{300,"F11"},{301,"F12"},
    // Navigation / misc
    {256,"Escape"},{257,"Enter"},{258,"Tab"},{259,"Backspace"},
    {260,"Insert"},{261,"Delete"},
    {262,"Right"},{263,"Left"},{264,"Down"},{265,"Up"},
    {266,"Page Up"},{267,"Page Down"},{268,"Home"},{269,"End"},
    // Modifiers
    {340,"Left Shift"},{341,"Left Ctrl"},{342,"Left Alt"},
    {344,"Right Shift"},{345,"Right Ctrl"},{346,"Right Alt"},
    {343,"Left Super"},{347,"Right Super"},
    // Symbols / whitespace
    {32,"Space"},{39,"Apostrophe"},{44,"Comma"},{45,"Minus"},
    {46,"Period"},{47,"Slash"},{59,"Semicolon"},{61,"Equal"},
    {91,"Left Bracket"},{92,"Backslash"},{93,"Right Bracket"},{96,"Grave"},
    // Numpad
    {320,"Num 0"},{321,"Num 1"},{322,"Num 2"},{323,"Num 3"},{324,"Num 4"},
    {325,"Num 5"},{326,"Num 6"},{327,"Num 7"},{328,"Num 8"},{329,"Num 9"},
    {330,"Num Decimal"},{331,"Num Divide"},{332,"Num Multiply"},
    {333,"Num Subtract"},{334,"Num Add"},{335,"Num Enter"},
    {0,nullptr}  // sentinel
};

const char* InputManager::KeyCodeToName(int code)
{
    for (int i = 0; kKeyTable[i].name; ++i)
        if (kKeyTable[i].code == code) return kKeyTable[i].name;
    return nullptr;
}

int InputManager::KeyNameToCode(const char* str)
{
    if (!str || !str[0]) return 0;
    for (int i = 0; kKeyTable[i].name; ++i)
    {
        const char* a = kKeyTable[i].name;
        const char* b = str;
        bool eq = true;
        while (*a && *b)
        {
            if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
                { eq = false; break; }
            ++a; ++b;
        }
        if (eq && !*a && !*b) return kKeyTable[i].code;
    }
    // Fallback: parse as integer
    char* end;
    long v = std::strtol(str, &end, 10);
    if (end != str) return (int)v;
    return 0;
}

} // namespace tsu