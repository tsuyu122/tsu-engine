#include "renderer/mesh.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace tsu {

static const float PI = 3.14159265358979f;

// ----------------------------------------------------------------
// Utilitários
// ----------------------------------------------------------------

glm::vec3 HexToRGB(const std::string& hex)
{
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() != 6) return glm::vec3(1.0f);
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return 0;
    };
    return glm::vec3(
        (hv(h[0])*16+hv(h[1]))/255.0f,
        (hv(h[2])*16+hv(h[3]))/255.0f,
        (hv(h[4])*16+hv(h[5]))/255.0f);
}

Mesh::Mesh() : VAO(0), VBO(0), m_VertexCount(0) {}

Mesh Mesh::BuildFromVertices(const std::vector<float>& verts)
{
    Mesh mesh;
    mesh.m_VertexCount = (int)(verts.size() / 9);
    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);
    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(6*sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    return mesh;
}

void Mesh::Draw()
{
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, m_VertexCount);
}

// ----------------------------------------------------------------
// Helper para adicionar triângulo com normal calculada
// ----------------------------------------------------------------

static void addTri(std::vector<float>& v,
                   glm::vec3 p0, glm::vec3 p1, glm::vec3 p2,
                   float r, float g, float b)
{
    glm::vec3 n = glm::normalize(glm::cross(p1-p0, p2-p0));
    v.insert(v.end(), {p0.x,p0.y,p0.z, n.x,n.y,n.z, r,g,b});
    v.insert(v.end(), {p1.x,p1.y,p1.z, n.x,n.y,n.z, r,g,b});
    v.insert(v.end(), {p2.x,p2.y,p2.z, n.x,n.y,n.z, r,g,b});
}

// ----------------------------------------------------------------
// CUBO
// ----------------------------------------------------------------

Mesh Mesh::CreateCube(const std::string& hexColor)
{
    glm::vec3 c = HexToRGB(hexColor);
    float r=c.r, g=c.g, b=c.b;
    std::vector<float> v = {
        -0.5f,-0.5f, 0.5f, 0,0,1, r,g,b,  0.5f,-0.5f, 0.5f, 0,0,1, r,g,b,  0.5f, 0.5f, 0.5f, 0,0,1, r,g,b,
         0.5f, 0.5f, 0.5f, 0,0,1, r,g,b, -0.5f, 0.5f, 0.5f, 0,0,1, r,g,b, -0.5f,-0.5f, 0.5f, 0,0,1, r,g,b,
        -0.5f,-0.5f,-0.5f, 0,0,-1,r,g,b,  0.5f,-0.5f,-0.5f, 0,0,-1,r,g,b,  0.5f, 0.5f,-0.5f, 0,0,-1,r,g,b,
         0.5f, 0.5f,-0.5f, 0,0,-1,r,g,b, -0.5f, 0.5f,-0.5f, 0,0,-1,r,g,b, -0.5f,-0.5f,-0.5f, 0,0,-1,r,g,b,
        -0.5f, 0.5f, 0.5f,-1,0,0, r,g,b, -0.5f, 0.5f,-0.5f,-1,0,0, r,g,b, -0.5f,-0.5f,-0.5f,-1,0,0, r,g,b,
        -0.5f,-0.5f,-0.5f,-1,0,0, r,g,b, -0.5f,-0.5f, 0.5f,-1,0,0, r,g,b, -0.5f, 0.5f, 0.5f,-1,0,0, r,g,b,
         0.5f, 0.5f, 0.5f, 1,0,0, r,g,b,  0.5f, 0.5f,-0.5f, 1,0,0, r,g,b,  0.5f,-0.5f,-0.5f, 1,0,0, r,g,b,
         0.5f,-0.5f,-0.5f, 1,0,0, r,g,b,  0.5f,-0.5f, 0.5f, 1,0,0, r,g,b,  0.5f, 0.5f, 0.5f, 1,0,0, r,g,b,
        -0.5f, 0.5f,-0.5f, 0,1,0, r,g,b,  0.5f, 0.5f,-0.5f, 0,1,0, r,g,b,  0.5f, 0.5f, 0.5f, 0,1,0, r,g,b,
         0.5f, 0.5f, 0.5f, 0,1,0, r,g,b, -0.5f, 0.5f, 0.5f, 0,1,0, r,g,b, -0.5f, 0.5f,-0.5f, 0,1,0, r,g,b,
        -0.5f,-0.5f,-0.5f, 0,-1,0,r,g,b,  0.5f,-0.5f,-0.5f, 0,-1,0,r,g,b,  0.5f,-0.5f, 0.5f, 0,-1,0,r,g,b,
         0.5f,-0.5f, 0.5f, 0,-1,0,r,g,b, -0.5f,-0.5f, 0.5f, 0,-1,0,r,g,b, -0.5f,-0.5f,-0.5f, 0,-1,0,r,g,b,
    };
    return BuildFromVertices(v);
}

// ----------------------------------------------------------------
// PIRÂMIDE
// ----------------------------------------------------------------

Mesh Mesh::CreatePyramid(const std::string& hexColor)
{
    glm::vec3 c = HexToRGB(hexColor);
    float r=c.r, g=c.g, b=c.b;
    float h=0.5f, s=0.5f;
    glm::vec3 apex={0,h,0}, ffl={-s,-h,s}, ffr={s,-h,s}, bfl={-s,-h,-s}, bfr={s,-h,-s};
    std::vector<float> v;
    addTri(v, ffl, ffr, apex, r,g,b);
    addTri(v, ffr, bfr, apex, r,g,b);
    addTri(v, bfr, bfl, apex, r,g,b);
    addTri(v, bfl, ffl, apex, r,g,b);
    glm::vec3 nb={0,-1,0};
    v.insert(v.end(), {ffl.x,ffl.y,ffl.z, nb.x,nb.y,nb.z, r,g,b});
    v.insert(v.end(), {bfr.x,bfr.y,bfr.z, nb.x,nb.y,nb.z, r,g,b});
    v.insert(v.end(), {bfl.x,bfl.y,bfl.z, nb.x,nb.y,nb.z, r,g,b});
    v.insert(v.end(), {ffl.x,ffl.y,ffl.z, nb.x,nb.y,nb.z, r,g,b});
    v.insert(v.end(), {ffr.x,ffr.y,ffr.z, nb.x,nb.y,nb.z, r,g,b});
    v.insert(v.end(), {bfr.x,bfr.y,bfr.z, nb.x,nb.y,nb.z, r,g,b});
    return BuildFromVertices(v);
}

// ----------------------------------------------------------------
// CILINDRO
// ----------------------------------------------------------------

Mesh Mesh::CreateCylinder(const std::string& hexColor, int segments)
{
    glm::vec3 c = HexToRGB(hexColor);
    float r=c.r, g=c.g, b=c.b;
    std::vector<float> v;
    float radius=0.5f, halfH=0.5f;
    for (int i=0; i<segments; i++)
    {
        float a0=(2*PI*i)/segments, a1=(2*PI*(i+1))/segments;
        float x0=radius*cosf(a0), z0=radius*sinf(a0);
        float x1=radius*cosf(a1), z1=radius*sinf(a1);
        glm::vec3 n0=glm::normalize(glm::vec3(x0,0,z0));
        glm::vec3 n1=glm::normalize(glm::vec3(x1,0,z1));
        v.insert(v.end(), {x0,-halfH,z0, n0.x,n0.y,n0.z, r,g,b});
        v.insert(v.end(), {x1,-halfH,z1, n1.x,n1.y,n1.z, r,g,b});
        v.insert(v.end(), {x1, halfH,z1, n1.x,n1.y,n1.z, r,g,b});
        v.insert(v.end(), {x0,-halfH,z0, n0.x,n0.y,n0.z, r,g,b});
        v.insert(v.end(), {x1, halfH,z1, n1.x,n1.y,n1.z, r,g,b});
        v.insert(v.end(), {x0, halfH,z0, n0.x,n0.y,n0.z, r,g,b});
        v.insert(v.end(), {0, halfH,0,   0,1,0, r,g,b});
        v.insert(v.end(), {x0,halfH,z0,  0,1,0, r,g,b});
        v.insert(v.end(), {x1,halfH,z1,  0,1,0, r,g,b});
        v.insert(v.end(), {0, -halfH,0,  0,-1,0, r,g,b});
        v.insert(v.end(), {x1,-halfH,z1, 0,-1,0, r,g,b});
        v.insert(v.end(), {x0,-halfH,z0, 0,-1,0, r,g,b});
    }
    return BuildFromVertices(v);
}

// ----------------------------------------------------------------
// ESFERA (UV sphere)
// ----------------------------------------------------------------

Mesh Mesh::CreateSphere(const std::string& hexColor, int stacks, int slices)
{
    glm::vec3 c = HexToRGB(hexColor);
    float r=c.r, g=c.g, b=c.b;
    std::vector<float> v;
    float radius = 0.5f;

    for (int i=0; i<stacks; i++)
    {
        float phi0 = PI*(float)i/stacks - PI/2;
        float phi1 = PI*(float)(i+1)/stacks - PI/2;
        for (int j=0; j<slices; j++)
        {
            float th0 = 2*PI*(float)j/slices;
            float th1 = 2*PI*(float)(j+1)/slices;

            auto vtx = [&](float phi, float th) -> std::pair<glm::vec3,glm::vec3> {
                glm::vec3 n = {cosf(phi)*cosf(th), sinf(phi), cosf(phi)*sinf(th)};
                return {n*radius, n};
            };

            auto [p00,n00] = vtx(phi0,th0);
            auto [p01,n01] = vtx(phi0,th1);
            auto [p10,n10] = vtx(phi1,th0);
            auto [p11,n11] = vtx(phi1,th1);

            auto push = [&](glm::vec3 p, glm::vec3 n) {
                v.insert(v.end(), {p.x,p.y,p.z, n.x,n.y,n.z, r,g,b});
            };
            push(p00,n00); push(p01,n01); push(p11,n11);
            push(p00,n00); push(p11,n11); push(p10,n10);
        }
    }
    return BuildFromVertices(v);
}

// ----------------------------------------------------------------
// CÁPSULA (cilindro com hemisférios nas pontas)
// ----------------------------------------------------------------

Mesh Mesh::CreateCapsule(const std::string& hexColor, float radius, float height, int segments)
{
    glm::vec3 c = HexToRGB(hexColor);
    float r=c.r, g=c.g, b=c.b;
    std::vector<float> v;
    float halfH = height * 0.5f;
    int hemi = segments / 2;

    // Corpo cilíndrico
    for (int i=0; i<segments; i++)
    {
        float a0=(2*PI*i)/segments, a1=(2*PI*(i+1))/segments;
        float x0=radius*cosf(a0), z0=radius*sinf(a0);
        float x1=radius*cosf(a1), z1=radius*sinf(a1);
        glm::vec3 n0=glm::normalize(glm::vec3(x0,0,z0));
        glm::vec3 n1=glm::normalize(glm::vec3(x1,0,z1));
        v.insert(v.end(), {x0,-halfH,z0, n0.x,n0.y,n0.z, r,g,b});
        v.insert(v.end(), {x1,-halfH,z1, n1.x,n1.y,n1.z, r,g,b});
        v.insert(v.end(), {x1, halfH,z1, n1.x,n1.y,n1.z, r,g,b});
        v.insert(v.end(), {x0,-halfH,z0, n0.x,n0.y,n0.z, r,g,b});
        v.insert(v.end(), {x1, halfH,z1, n1.x,n1.y,n1.z, r,g,b});
        v.insert(v.end(), {x0, halfH,z0, n0.x,n0.y,n0.z, r,g,b});
    }

    // Hemisférios
    auto hemisphere = [&](float yOffset, float dir)
    {
        for (int i=0; i<hemi; i++)
        {
            float phi0 = PI/2*(float)i/hemi * dir;
            float phi1 = PI/2*(float)(i+1)/hemi * dir;
            for (int j=0; j<segments; j++)
            {
                float th0=2*PI*(float)j/segments, th1=2*PI*(float)(j+1)/segments;
                auto vtx = [&](float phi, float th) -> std::pair<glm::vec3,glm::vec3> {
                    glm::vec3 n = {cosf(phi)*cosf(th), sinf(phi), cosf(phi)*sinf(th)};
                    return {n*radius + glm::vec3(0,yOffset,0), n};
                };
                auto [p00,n00]=vtx(phi0,th0); auto [p01,n01]=vtx(phi0,th1);
                auto [p10,n10]=vtx(phi1,th0); auto [p11,n11]=vtx(phi1,th1);
                auto push=[&](glm::vec3 p,glm::vec3 n){v.insert(v.end(),{p.x,p.y,p.z,n.x,n.y,n.z,r,g,b});};
                push(p00,n00); push(p01,n01); push(p11,n11);
                push(p00,n00); push(p11,n11); push(p10,n10);
            }
        }
    };
    hemisphere( halfH,  1.0f); // topo
    hemisphere(-halfH, -1.0f); // base

    return BuildFromVertices(v);
}

// ----------------------------------------------------------------
// GIZMO — esfera pequena vermelha translúcida
// ----------------------------------------------------------------

Mesh Mesh::CreateGizmoSphere(float radius)
{
    std::vector<float> v;
    int stacks=8, slices=16;
    float r=1.0f, g=0.2f, b=0.2f;
    for (int i=0; i<stacks; i++)
    {
        float phi0=PI*(float)i/stacks-PI/2;
        float phi1=PI*(float)(i+1)/stacks-PI/2;
        for (int j=0; j<slices; j++)
        {
            float th0=2*PI*(float)j/slices, th1=2*PI*(float)(j+1)/slices;
            auto vtx=[&](float phi,float th)->std::pair<glm::vec3,glm::vec3>{
                glm::vec3 n={cosf(phi)*cosf(th),sinf(phi),cosf(phi)*sinf(th)};
                return {n*radius,n};
            };
            auto [p00,n00]=vtx(phi0,th0); auto [p01,n01]=vtx(phi0,th1);
            auto [p10,n10]=vtx(phi1,th0); auto [p11,n11]=vtx(phi1,th1);
            auto push=[&](glm::vec3 p,glm::vec3 n){v.insert(v.end(),{p.x,p.y,p.z,n.x,n.y,n.z,r,g,b});};
            push(p00,n00); push(p01,n01); push(p11,n11);
            push(p00,n00); push(p11,n11); push(p10,n10);
        }
    }
    return BuildFromVertices(v);
}

// ----------------------------------------------------------------
// GIZMO — linha (cubo muito fino) apontando em +Z
// ----------------------------------------------------------------

Mesh Mesh::CreateGizmoLine(float length)
{
    float r=1.0f, g=0.1f, b=0.1f;
    float w=0.015f;
    std::vector<float> v = {
        -w,  -w, 0,      0,0,1, r,g,b,
         w,  -w, 0,      0,0,1, r,g,b,
         w,   w, 0,      0,0,1, r,g,b,
         w,   w, 0,      0,0,1, r,g,b,
        -w,   w, 0,      0,0,1, r,g,b,
        -w,  -w, 0,      0,0,1, r,g,b,
        -w,  -w, length, 0,0,1, r,g,b,
         w,  -w, length, 0,0,1, r,g,b,
         w,   w, length, 0,0,1, r,g,b,
         w,   w, length, 0,0,1, r,g,b,
        -w,   w, length, 0,0,1, r,g,b,
        -w,  -w, length, 0,0,1, r,g,b,
        -w,  -w, 0,     -1,0,0, r,g,b,
        -w,  -w, length,-1,0,0, r,g,b,
        -w,   w, length,-1,0,0, r,g,b,
        -w,   w, length,-1,0,0, r,g,b,
        -w,   w, 0,     -1,0,0, r,g,b,
        -w,  -w, 0,     -1,0,0, r,g,b,
         w,  -w, 0,      1,0,0, r,g,b,
         w,  -w, length, 1,0,0, r,g,b,
         w,   w, length, 1,0,0, r,g,b,
         w,   w, length, 1,0,0, r,g,b,
         w,   w, 0,      1,0,0, r,g,b,
         w,  -w, 0,      1,0,0, r,g,b,
    };
    return BuildFromVertices(v);
}

// ----------------------------------------------------------------
// CreateGizmoArrow — cilindro fino + cone na ponta, aponta em +Y
// ----------------------------------------------------------------

Mesh Mesh::CreateGizmoArrow(float length, float rCol, float gCol, float bCol)
{
    const int   sides   = 8;
    const float shaftR  = length * 0.04f;
    const float coneR   = length * 0.12f;
    const float coneH   = length * 0.25f;
    const float shaftH  = length - coneH;

    std::vector<float> verts;
    verts.reserve(sides * 6 * 9 + sides * 3 * 9);

    auto push = [&](glm::vec3 p, glm::vec3 n)
    {
        verts.insert(verts.end(),
            { p.x, p.y, p.z, n.x, n.y, n.z, rCol, gCol, bCol });
    };

    auto tri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c)
    {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        push(a, n); push(b, n); push(c, n);
    };

    float angle = 2.0f * PI / (float)sides;

    // Shaft (cilindro ao longo de Y de 0 a shaftH)
    for (int i = 0; i < sides; i++)
    {
        float a0 = angle * i, a1 = angle * (i + 1);
        glm::vec3 b0 = { shaftR * cosf(a0), 0.0f,  shaftR * sinf(a0) };
        glm::vec3 b1 = { shaftR * cosf(a1), 0.0f,  shaftR * sinf(a1) };
        glm::vec3 t0 = { shaftR * cosf(a0), shaftH, shaftR * sinf(a0) };
        glm::vec3 t1 = { shaftR * cosf(a1), shaftH, shaftR * sinf(a1) };
        tri(b0, t0, b1);
        tri(b1, t0, t1);
    }

    // Cone (de shaftH até shaftH + coneH)
    glm::vec3 apex = { 0.0f, shaftH + coneH, 0.0f };
    for (int i = 0; i < sides; i++)
    {
        float a0 = angle * i, a1 = angle * (i + 1);
        glm::vec3 b0 = { coneR * cosf(a0), shaftH, coneR * sinf(a0) };
        glm::vec3 b1 = { coneR * cosf(a1), shaftH, coneR * sinf(a1) };
        tri(b0, apex, b1);
    }

    return BuildFromVertices(verts);
}

// ----------------------------------------------------------------
// PLANE (quad de 1x1 em XZ, visível dos dois lados)
// ----------------------------------------------------------------

Mesh Mesh::CreatePlane(const std::string& hexColor)
{
    glm::vec3 c = HexToRGB(hexColor);
    float r = c.r, g = c.g, b = c.b;

    std::vector<float> verts;
    glm::vec3 p0 = {-0.5f, 0.0f, -0.5f};
    glm::vec3 p1 = { 0.5f, 0.0f, -0.5f};
    glm::vec3 p2 = { 0.5f, 0.0f,  0.5f};
    glm::vec3 p3 = {-0.5f, 0.0f,  0.5f};

    // Top face
    addTri(verts, p0, p1, p2, r, g, b);
    addTri(verts, p0, p2, p3, r, g, b);
    // Bottom face (visible from below)
    addTri(verts, p2, p1, p0, r, g, b);
    addTri(verts, p3, p2, p0, r, g, b);

    return BuildFromVertices(verts);
}

} // namespace tsu