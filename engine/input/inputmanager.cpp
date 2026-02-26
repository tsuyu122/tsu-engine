#include "input/inputManager.h"
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <cstring>

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
    // Forward to ImGui so inspector/panels can scroll with mouse wheel
    ImGui_ImplGlfw_ScrollCallback(w, xoff, yoff);
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

} // namespace tsu