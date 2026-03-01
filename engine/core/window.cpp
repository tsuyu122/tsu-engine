#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "window.h"
#include <cmath>
#include <vector>

namespace tsu {

// ---------------------------------------------------------------------------
// Generate the engine smiley-face logo as an RGBA pixel buffer (white on transparent)
// ---------------------------------------------------------------------------
static void SetEngineIcon(GLFWwindow* win)
{
    constexpr int   ICON_W = 64;
    constexpr int   ICON_H = 64;
    // Map pixel coords (0..ICON_W-1) to SVG space (0..200)
    const float     S  = 200.0f / (float)ICON_W;       // SVG units per pixel
    constexpr float PI = 3.14159265358979f;

    // SVG geometry (in SVG units)
    const float cx   = 100.0f, cy = 100.0f;   // big-circle centre
    const float bigR = 90.0f,  sw = 6.0f;     // big-circle radius, half-stroke
    const float exL  = 65.0f,  eyL = 80.0f;   // left eye
    const float exR  = 135.0f, eyR = 80.0f;   // right eye
    const float eyeR = 14.0f;
    const float smR  = 60.0f;                  // smile arc radius (centre = cx,cy)
    const float smSw = 6.0f;                   // smile half-stroke
    // Smile angle range in radians (10° – 170°, Y-down so bottom arc = smile)
    const float smA0 = 10.0f  * PI / 180.0f;
    const float smA1 = 170.0f * PI / 180.0f;

    std::vector<unsigned char> pixels(ICON_W * ICON_H * 4, 0);

    for (int py = 0; py < ICON_H; ++py)
    {
        for (int px = 0; px < ICON_W; ++px)
        {
            // Sample centre of the pixel, in SVG units
            float sx = ((float)px + 0.5f) * S;
            float sy = ((float)py + 0.5f) * S;

            bool lit = false;

            // Outer ring
            float dr = std::sqrtf((sx-cx)*(sx-cx) + (sy-cy)*(sy-cy));
            if (std::fabsf(dr - bigR) < sw) lit = true;

            // Eyes
            if (!lit)
            {
                if ((sx-exL)*(sx-exL)+(sy-eyL)*(sy-eyL) < eyeR*eyeR) lit = true;
                if ((sx-exR)*(sx-exR)+(sy-eyR)*(sy-eyR) < eyeR*eyeR) lit = true;
            }

            // Smile arc
            if (!lit)
            {
                float ds = std::sqrtf((sx-cx)*(sx-cx) + (sy-cy)*(sy-cy));
                if (std::fabsf(ds - smR) < smSw)
                {
                    float angle = std::atan2f(sy - cy, sx - cx);
                    if (angle >= smA0 && angle <= smA1) lit = true;
                }
            }

            if (lit)
            {
                int idx = (py * ICON_W + px) * 4;
                pixels[idx + 0] = 255; // R
                pixels[idx + 1] = 255; // G
                pixels[idx + 2] = 255; // B
                pixels[idx + 3] = 255; // A
            }
        }
    }

    GLFWimage img;
    img.width  = ICON_W;
    img.height = ICON_H;
    img.pixels = pixels.data();
    glfwSetWindowIcon(win, 1, &img);
}

Window::Window(int width, int height, const char* title)
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    glfwMakeContextCurrent(m_Window);

    SetEngineIcon(m_Window);

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // Enable VSync — limits framerate to monitor refresh, drastically reduces GPU usage
    glfwSwapInterval(1);

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