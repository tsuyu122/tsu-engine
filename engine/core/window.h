#pragma once
#include <functional>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace tsu {

class Window
{
public:
    Window(int width, int height, const char* title);
    ~Window();

    void Update();
    bool ShouldClose();

    GLFWwindow* GetNativeWindow();
    int GetWidth()  const;
    int GetHeight() const;

    // Set a callback that will be invoked each timer tick while the OS
    // window is being dragged/resized, so the viewport doesn't freeze.
    static void SetRenderCallback(std::function<void()> cb);

private:
    GLFWwindow* m_Window;
};

}