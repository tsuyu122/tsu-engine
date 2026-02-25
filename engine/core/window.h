#pragma once
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

private:
    GLFWwindow* m_Window;
};

}