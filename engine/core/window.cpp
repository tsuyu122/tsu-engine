#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "window.h"

namespace tsu {

Window::Window(int width, int height, const char* title)
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    glfwMakeContextCurrent(m_Window);

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glEnable(GL_DEPTH_TEST);
}

Window::~Window()
{
    glfwTerminate();
}

void Window::Update()
{
    glfwSwapBuffers(m_Window);
    glfwPollEvents();
}

bool Window::ShouldClose()
{
    return glfwWindowShouldClose(m_Window);
}

GLFWwindow* Window::GetNativeWindow()
{
    return m_Window;
}

int Window::GetWidth() const
{
    int w, h;
    glfwGetWindowSize(m_Window, &w, &h);
    return w;
}

int Window::GetHeight() const
{
    int w, h;
    glfwGetWindowSize(m_Window, &w, &h);
    return h;
}

}