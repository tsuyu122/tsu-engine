#include "renderer/mesh.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace tsu {

static const float PI = 3.14159265358979f;

// ----------------------------------------------------------------
// Utilities
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

bool Mesh::HasValidUV2() const
{
    if (m_Vertices.empty()) return false;
    glm::vec2 mn( std::numeric_limits<float>::max());
    glm::vec2 mx(-std::numeric_limits<float>::max());
    for (const auto& v : m_Vertices) {
        mn = glm::min(mn, v.uv2);
        mx = glm::max(mx, v.uv2);
    }
    glm::vec2 ex = mx - mn;
    return (ex.x > 1e-4f || ex.y > 1e-4f);
}

void Mesh::GenerateAutoUV2(float padding)
{
    if (m_Vertices.empty()) return;

    struct BucketInfo {
        glm::vec2 mn = glm::vec2( std::numeric_limits<float>::max());
        glm::vec2 mx = glm::vec2(-std::numeric_limits<float>::max());
        bool used = false;
    };
    BucketInfo buckets[6]; // +X -X +Y -Y +Z -Z
    std::vector<int> bucketOf(m_Vertices.size(), 0);
    std::vector<glm::vec2> rawUV(m_Vertices.size(), glm::vec2(0.0f));

    auto pickBucket = [](const glm::vec3& n) -> int {
        glm::vec3 an = glm::abs(n);
        if (an.x >= an.y && an.x >= an.z) return (n.x >= 0.0f) ? 0 : 1;
        if (an.y >= an.x && an.y >= an.z) return (n.y >= 0.0f) ? 2 : 3;
        return (n.z >= 0.0f) ? 4 : 5;
    };

    auto project = [](int b, const glm::vec3& p) -> glm::vec2 {
        switch (b)
        {
            case 0: return glm::vec2( p.z, p.y); // +X
            case 1: return glm::vec2(-p.z, p.y); // -X
            case 2: return glm::vec2( p.x, p.z); // +Y
            case 3: return glm::vec2( p.x,-p.z); // -Y
            case 4: return glm::vec2( p.x, p.y); // +Z
            default:return glm::vec2(-p.x, p.y); // -Z
        }
    };

    // Assign buckets PER TRIANGLE using the face normal.
    // This ensures all 3 vertices of each triangle share the same bucket/cell,
    // preventing UV2 from jumping across chart boundaries on curved surfaces.
    const size_t triCount = m_Vertices.size() / 3;
    for (size_t t = 0; t < triCount; ++t)
    {
        size_t i0 = t * 3, i1 = t * 3 + 1, i2 = t * 3 + 2;
        // Face normal from triangle edges (geometric, not vertex normal)
        glm::vec3 edge1 = m_Vertices[i1].pos - m_Vertices[i0].pos;
        glm::vec3 edge2 = m_Vertices[i2].pos - m_Vertices[i0].pos;
        glm::vec3 faceN = glm::cross(edge1, edge2);
        float fLen = glm::length(faceN);
        if (fLen > 1e-8f) faceN /= fLen;
        else faceN = m_Vertices[i0].normal; // degenerate: fall back to vertex normal

        int b = pickBucket(faceN);
        for (size_t vi = i0; vi <= i2; ++vi) {
            glm::vec2 uv = project(b, m_Vertices[vi].pos);
            bucketOf[vi] = b;
            rawUV[vi] = uv;
            buckets[b].mn = glm::min(buckets[b].mn, uv);
            buckets[b].mx = glm::max(buckets[b].mx, uv);
            buckets[b].used = true;
        }
    }

    const int cols = 3;
    const int rows = 2;
    const float cellW = 1.0f / (float)cols;
    const float cellH = 1.0f / (float)rows;
    const float inset = glm::clamp(padding, 0.0f, 0.45f) * std::min(cellW, cellH);

    for (size_t i = 0; i < m_Vertices.size(); ++i)
    {
        int b = bucketOf[i];
        glm::vec2 ex = buckets[b].mx - buckets[b].mn;
        if (ex.x < 1e-6f) ex.x = 1.0f;
        if (ex.y < 1e-6f) ex.y = 1.0f;
        glm::vec2 nuv = (rawUV[i] - buckets[b].mn) / ex;

        int cx = b % cols;
        int cy = b / cols;
        float ox = cx * cellW;
        float oy = cy * cellH;
        m_Vertices[i].uv2 = glm::vec2(
            ox + inset + nuv.x * (cellW - 2.0f * inset),
            oy + inset + nuv.y * (cellH - 2.0f * inset));
    }

    UploadGpuBuffer();
}

void Mesh::UploadGpuBuffer()
{
    if (m_Vertices.empty()) return;

    std::vector<float> packed;
    packed.reserve(m_Vertices.size() * 13);
    for (const auto& v : m_Vertices) {
        packed.push_back(v.pos.x); packed.push_back(v.pos.y); packed.push_back(v.pos.z);
        packed.push_back(v.normal.x); packed.push_back(v.normal.y); packed.push_back(v.normal.z);
        packed.push_back(v.color.x); packed.push_back(v.color.y); packed.push_back(v.color.z);
        packed.push_back(v.uv0.x); packed.push_back(v.uv0.y);
        packed.push_back(v.uv2.x); packed.push_back(v.uv2.y);
    }

    m_VertexCount = (int)m_Vertices.size();
    if (VAO == 0) glGenVertexArrays(1, &VAO);
    if (VBO == 0) glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, packed.size() * sizeof(float), packed.data(), GL_STATIC_DRAW);

    const GLsizei stride = 13 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, (void*)(11 * sizeof(float)));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);
}

Mesh Mesh::BuildFromVertices(const std::vector<float>& verts)
{
    std::vector<CpuVertex> cpu;
    cpu.reserve(verts.size() / 9);
    for (size_t i = 0; i + 8 < verts.size(); i += 9)
    {
        CpuVertex cv;
        cv.pos    = glm::vec3(verts[i+0], verts[i+1], verts[i+2]);
        cv.normal = glm::vec3(verts[i+3], verts[i+4], verts[i+5]);
        cv.color  = glm::vec3(verts[i+6], verts[i+7], verts[i+8]);
        cv.uv0    = glm::vec2(0.0f);
        cv.uv2    = glm::vec2(0.0f);
        cpu.push_back(cv);
    }
    return BuildFromCpuVertices(cpu);
}

Mesh Mesh::BuildFromCpuVertices(const std::vector<CpuVertex>& verts)
{
    Mesh mesh;
    mesh.m_Vertices = verts;
    mesh.UploadGpuBuffer();
    return mesh;
}

void Mesh::Draw()
{
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, m_VertexCount);
}

// ----------------------------------------------------------------
// Helper to add triangle with computed normal
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
// PYRAMID
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
// CYLINDER
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
// CAPSULE (cylinder with hemispheres at the ends)
// ----------------------------------------------------------------

Mesh Mesh::CreateCapsule(const std::string& hexColor, float radius, float height, int segments)
{
    glm::vec3 c = HexToRGB(hexColor);
    float r=c.r, g=c.g, b=c.b;
    std::vector<float> v;
    float halfH = height * 0.5f;
    int hemi = segments / 2;

    // Cylindrical body
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

    // Hemispheres
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
// GIZMO — small red translucent sphere
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
// CreateGizmoArrow — thin cylinder + cone tip, points in +Y
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

    // Shaft (cylinder along Y from 0 to shaftH)
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

    // Cone (from shaftH to shaftH + coneH)
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
// PLANE (1x1 quad in XZ, single-sided — visible from top only)
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

    // Top face only (normal up)
    addTri(verts, p0, p1, p2, r, g, b);
    addTri(verts, p0, p2, p3, r, g, b);

    return BuildFromVertices(verts);
}

// ----------------------------------------------------------------
// LoadOBJ  — load a Wavefront .obj file, return as engine Mesh
// Vertex format: pos(3) + normal(3) + color(3)
// ----------------------------------------------------------------
Mesh Mesh::LoadOBJ(const std::string& path)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    // Base directory for material files
    std::string baseDir;
    size_t sep = path.find_last_of("/\\");
    if (sep != std::string::npos) baseDir = path.substr(0, sep + 1);

    // v1.0.6 signature: (attrib, shapes, materials, err, filename, basedir, triangulate)
    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &err,
                               path.c_str(),
                               baseDir.empty() ? nullptr : baseDir.c_str(),
                               /*triangulate=*/true);
    if (!err.empty()) std::cerr << "[OBJ] " << err << "\n";
    if (!ok)          return Mesh{};

    std::vector<CpuVertex> verts;
    verts.reserve(shapes.size() * 3 * 3);

    for (const auto& shape : shapes)
    {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
        {
            int fv = (int)shape.mesh.num_face_vertices[f];
            for (int v = 0; v < fv; ++v)
            {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                float px = attrib.vertices[3 * idx.vertex_index + 0];
                float py = attrib.vertices[3 * idx.vertex_index + 1];
                float pz = attrib.vertices[3 * idx.vertex_index + 2];

                float nx = 0.0f, ny = 1.0f, nz = 0.0f;
                if (idx.normal_index >= 0)
                {
                    nx = attrib.normals[3 * idx.normal_index + 0];
                    ny = attrib.normals[3 * idx.normal_index + 1];
                    nz = attrib.normals[3 * idx.normal_index + 2];
                }

                float u0 = 0.0f, v0 = 0.0f;
                if (idx.texcoord_index >= 0 && !attrib.texcoords.empty()) {
                    u0 = attrib.texcoords[2 * idx.texcoord_index + 0];
                    v0 = attrib.texcoords[2 * idx.texcoord_index + 1];
                }

                CpuVertex cv;
                cv.pos    = glm::vec3(px, py, pz);
                cv.normal = glm::vec3(nx, ny, nz);
                cv.color  = glm::vec3(0.87f);
                cv.uv0    = glm::vec2(u0, v0);
                cv.uv2    = glm::vec2(0.0f);
                verts.push_back(cv);
            }
            indexOffset += fv;
        }
    }

    if (verts.empty()) return Mesh{};

    // Compute AABB from all vertex positions
    glm::vec3 lo( 1e30f), hi(-1e30f);
    for (const auto& v : verts) {
        glm::vec3 p = v.pos;
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }

    // Auto-scale: normalize vertices so the mesh fits within a 1-unit bounding box
    glm::vec3 extent = hi - lo;
    float maxExtent = std::max({extent.x, extent.y, extent.z});
    if (maxExtent > 1e-6f) {
        float scaleFactor = 1.0f / maxExtent;
        glm::vec3 center = (lo + hi) * 0.5f;
        for (auto& v : verts) {
            v.pos.x = (v.pos.x - center.x) * scaleFactor;
            v.pos.y = (v.pos.y - center.y) * scaleFactor;
            v.pos.z = (v.pos.z - center.z) * scaleFactor;
        }
        lo = (lo - center) * scaleFactor;
        hi = (hi - center) * scaleFactor;
    }

    Mesh m = BuildFromCpuVertices(verts);
    m.GenerateAutoUV2();
    m.BoundsMin = lo;
    m.BoundsMax = hi;
    return m;
}

void Mesh::UpdateCpuVertices(const std::vector<CpuVertex>& verts)
{
    if (verts.empty()) return;
    m_Vertices = verts;
    UploadGpuBuffer();
}

} // namespace tsu
