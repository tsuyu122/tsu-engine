#pragma once

// We don't include GLFW here to avoid circular include issues.
// GLFWwindow is forward-declared.
struct GLFWwindow;

namespace tsu {

namespace Key {
    // Values mirrored from GLFW — without depending on its header here
    constexpr int W           = 87;
    constexpr int A           = 65;
    constexpr int S           = 83;
    constexpr int D           = 68;
    constexpr int E           = 69;
    constexpr int R           = 82;
    constexpr int Q           = 81;
    constexpr int Space       = 32;
    constexpr int LeftShift   = 340;
    constexpr int LeftControl = 341;
    constexpr int Escape      = 256;
    constexpr int F1          = 290;
    constexpr int F2          = 291;
}

namespace Mouse {
    constexpr int Left   = 0;
    constexpr int Right  = 1;
    constexpr int Middle = 2;
}

struct MouseDelta {
    float x = 0.0f;
    float y = 0.0f;
};

class InputManager
{
public:
    static void Update(GLFWwindow* window);

    static bool IsKeyPressed(int key);
    static bool IsKeyDown(int key);
    static bool IsKeyUp(int key);

    static bool IsMousePressed(int button);
    static bool IsMouseDown(int button);
    static bool IsMouseUp(int button);

    static MouseDelta GetMouseDelta();
    static void GetMousePosition(double& x, double& y);
    static float GetScrollDelta();   // positive = scroll up/forward

    // Key name <-> GLFW code helpers (shared between inspector and runtime)
    static int         KeyNameToCode(const char* name);  // "W"->87, "Left Shift"->340 etc.
    static const char* KeyCodeToName(int code);           // 87->"W", 0/unknown -> nullptr

private:
    static GLFWwindow* s_Window;

    static bool s_Keys[349];
    static bool s_KeysPrev[349];

    static bool s_MouseButtons[8];
    static bool s_MouseButtonsPrev[8];

    static double s_MouseX, s_MouseY;
    static double s_LastMouseX, s_LastMouseY;
    static bool   s_FirstMouse;

    static float s_ScrollDeltaThisFrame; // read once per frame
};

} // namespace tsu