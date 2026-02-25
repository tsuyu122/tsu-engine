#include "renderer/renderer.h"
#include "renderer/mesh.h"
#include "editor/editorGizmo.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace tsu {

unsigned int Renderer::s_ShaderProgram    = 0;
unsigned int Renderer::s_GizmoShader      = 0;
unsigned int Renderer::s_HUDShader        = 0;
unsigned int Renderer::s_GizmoSphereVAO   = 0;
unsigned int Renderer::s_GizmoSphereCount = 0;
unsigned int Renderer::s_GizmoLineVAO     = 0;
unsigned int Renderer::s_GizmoLineCount   = 0;

static unsigned int CompileShader(unsigned int type, const char* src)
{
    unsigned int id = glCreateShader(type);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    return id;
}

static unsigned int LinkProgram(const char* vs, const char* fs)
{
    unsigned int v = CompileShader(GL_VERTEX_SHADER, vs);
    unsigned int f = CompileShader(GL_FRAGMENT_SHADER, fs);
    unsigned int p = glCreateProgram();
    glAttachShader(p,v); glAttachShader(p,f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ----------------------------------------------------------------
// Init
// ----------------------------------------------------------------

void Renderer::Init()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Shader principal com iluminação
    s_ShaderProgram = LinkProgram(R"(
        #version 330 core
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;
        layout(location=2) in vec3 aColor;
        out vec3 vColor; out vec3 vNormal; out vec3 vFragPos;
        uniform mat4 u_MVP; uniform mat4 u_Model;
        void main(){
            vColor=aColor;
            vNormal=mat3(transpose(inverse(u_Model)))*aNormal;
            vFragPos=vec3(u_Model*vec4(aPos,1.0));
            gl_Position=u_MVP*vec4(aPos,1.0);
        }
    )", R"(
        #version 330 core
        in vec3 vColor; in vec3 vNormal; in vec3 vFragPos;
        out vec4 FragColor;
        uniform vec3 u_LightPos;
        void main(){
            vec3 ld=normalize(u_LightPos-vFragPos);
            float d=max(dot(normalize(vNormal),ld),0.2);
            FragColor=vec4(vColor*d,1.0);
        }
    )");

    // Shader de gizmo — sem iluminação, com alpha
    s_GizmoShader = LinkProgram(R"(
        #version 330 core
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;
        layout(location=2) in vec3 aColor;
        out vec3 vColor;
        uniform mat4 u_MVP;
        void main(){ vColor=aColor; gl_Position=u_MVP*vec4(aPos,1.0); }
    )", R"(
        #version 330 core
        in vec3 vColor;
        out vec4 FragColor;
        uniform float u_Alpha;
        void main(){ FragColor=vec4(vColor,u_Alpha); }
    )");

    // Shader HUD 2D (NDC direto)
    s_HUDShader = LinkProgram(R"(
        #version 330 core
        layout(location=0) in vec2 aPos;
        layout(location=1) in vec3 aColor;
        out vec3 vColor;
        void main(){ vColor=aColor; gl_Position=vec4(aPos,0.0,1.0); }
    )", R"(
        #version 330 core
        in vec3 vColor;
        out vec4 FragColor;
        uniform float u_Alpha;
        void main(){ FragColor=vec4(vColor,u_Alpha); }
    )");

    InitGizmoMeshes();
}

void Renderer::InitGizmoMeshes()
{
    // Esfera gizmo
    {
        Mesh m = Mesh::CreateGizmoSphere(0.08f);
        // Reutilizamos o VAO criado dentro do Mesh — precisamos guardar
        // Hack: criamos um Mesh temporário e pegamos o VAO via Draw() não é ideal,
        // então vamos recriar inline com os dados
        // (O VAO do Mesh é private — usamos o Draw() normalmente via ponteiro)
    }
    // Usamos Mesh estáticos alocados uma vez
    static Mesh gizmoSphere = Mesh::CreateGizmoSphere(0.08f);
    static Mesh gizmoLine   = Mesh::CreateGizmoLine(0.7f);
    // Guardamos referência via ponteiro estático
    // (Draw() é público, então chamamos direto nas funções de render)
}

// ----------------------------------------------------------------
// BeginFrame
// ----------------------------------------------------------------

void Renderer::BeginFrame()
{
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// ----------------------------------------------------------------
// DrawScene (interno)
// ----------------------------------------------------------------

void Renderer::DrawScene(Scene& scene, const glm::mat4& view,
                         const glm::mat4& proj, unsigned int shader)
{
    glUseProgram(shader);
    glm::vec3 lightPos(3.0f, 6.0f, 3.0f);
    unsigned int mvpLoc   = glGetUniformLocation(shader, "u_MVP");
    unsigned int modelLoc = glGetUniformLocation(shader, "u_Model");
    unsigned int lightLoc = glGetUniformLocation(shader, "u_LightPos");
    glUniform3fv(lightLoc, 1, &lightPos[0]);

    for (size_t i=0; i<scene.Transforms.size(); i++)
    {
        if (!scene.MeshRenderers[i].MeshPtr) continue;
        glm::mat4 model = scene.GetEntityWorldMatrix((int)i);
        glm::mat4 mvp   = proj * view * model;
        glUniformMatrix4fv(mvpLoc,   1, GL_FALSE, &mvp[0][0]);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
        scene.MeshRenderers[i].MeshPtr->Draw();
    }
}

// ----------------------------------------------------------------
// DrawGizmos — gizmos de game cameras no editor
// ----------------------------------------------------------------

void Renderer::DrawGizmos(Scene& scene, const glm::mat4& view, const glm::mat4& proj)
{
    static Mesh gizmoSphere = Mesh::CreateGizmoSphere(0.08f);
    static Mesh gizmoLine   = Mesh::CreateGizmoLine(0.7f);

    glUseProgram(s_GizmoShader);
    unsigned int mvpLoc   = glGetUniformLocation(s_GizmoShader, "u_MVP");
    unsigned int alphaLoc = glGetUniformLocation(s_GizmoShader, "u_Alpha");
    glUniform1f(alphaLoc, 0.75f);

    glDisable(GL_DEPTH_TEST); // gizmos sempre visíveis

    for (size_t i=0; i<scene.GameCameras.size(); i++)
    {
        if (!scene.GameCameras[i].Active) continue;

        glm::vec3 pos = scene.GetEntityWorldPos((int)i);

        // Bolinha
        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
        glm::mat4 mvp   = proj * view * model;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        gizmoSphere.Draw();

        // Linha na direção Front da game camera
        glm::vec3 front = scene.GameCameras[i].Front;

        // A linha mesh aponta em +Z por padrão
        glm::vec3 defaultDir = {0.0f, 0.0f, 1.0f};
        glm::vec3 axis = glm::cross(defaultDir, front);
        float angle = acos(glm::clamp(glm::dot(defaultDir, front), -1.0f, 1.0f));

        glm::mat4 lineModel = glm::translate(glm::mat4(1.0f), pos);
        if (glm::length(axis) > 0.0001f)
            lineModel = glm::rotate(lineModel, angle, glm::normalize(axis));

        mvp = proj * view * lineModel;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        gizmoLine.Draw();
    }

    glEnable(GL_DEPTH_TEST);
}

// ----------------------------------------------------------------
// RenderSceneEditor
// ----------------------------------------------------------------

void Renderer::RenderSceneEditor(Scene& scene, const EditorCamera& camera, int winW, int winH)
{
    float aspect         = (float)winW / (float)winH;
    glm::mat4 view       = camera.GetViewMatrix();
    glm::mat4 projection = camera.GetProjection(aspect);

    DrawScene(scene, view, projection, s_ShaderProgram);
    DrawGizmos(scene, view, projection);
}

// ----------------------------------------------------------------
// RenderSceneGame
// ----------------------------------------------------------------

void Renderer::RenderSceneGame(Scene& scene, int winW, int winH)
{
    int camIdx = scene.GetActiveGameCamera();
    if (camIdx < 0) return;

    float aspect        = (float)winW / (float)winH;
    auto& gc            = scene.GameCameras[camIdx];
    glm::vec3 pos       = scene.Transforms[camIdx].Position;
    glm::mat4 view      = glm::lookAt(pos, pos + gc.Front, gc.Up);
    glm::mat4 proj      = gc.GetProjection(aspect);

    DrawScene(scene, view, proj, s_ShaderProgram);
}

// ----------------------------------------------------------------
// RenderToolbar — barra Play/Pause simples no topo
// ----------------------------------------------------------------

void Renderer::RenderToolbar(bool isPlaying, bool isPaused, int winW, int winH)
{
    // Desabilita depth test para desenhar por cima de tudo
    glDisable(GL_DEPTH_TEST);

    // Fundo da toolbar (faixa escura no topo)
    // Coordenadas NDC: y=1.0 topo, y=1.0-barH base da barra
    float barH = 0.07f; // altura em NDC
    float btnW = 0.06f;
    float btnH = 0.05f;
    float btnY = 1.0f - (barH - btnH) * 0.5f - btnH; // centro vertical da barra

    struct Vtx2D { float x, y, r, g, b; };

    auto makeQuad = [](float x0, float y0, float x1, float y1,
                       float r, float g, float b) -> std::vector<Vtx2D>
    {
        return {
            {x0,y0, r,g,b}, {x1,y0, r,g,b}, {x1,y1, r,g,b},
            {x0,y0, r,g,b}, {x1,y1, r,g,b}, {x0,y1, r,g,b},
        };
    };

    std::vector<Vtx2D> verts;

    // Fundo cinza escuro
    auto bg = makeQuad(-1.0f, 1.0f-barH, 1.0f, 1.0f, 0.18f, 0.18f, 0.18f);
    verts.insert(verts.end(), bg.begin(), bg.end());

    // Botão Play
    float playR = isPlaying ? 0.2f : 0.25f;
    float playG = isPlaying ? 0.8f : 0.9f;
    float playB = isPlaying ? 0.2f : 0.25f;
    auto playBtn = makeQuad(-btnW*0.5f - btnW, btnY, -btnW*0.5f, btnY+btnH, playR, playG, playB);
    verts.insert(verts.end(), playBtn.begin(), playBtn.end());

    // Botão Pause
    float pauseR = isPaused ? 0.9f : 0.25f;
    float pauseG = isPaused ? 0.9f : 0.25f;
    float pauseB = isPaused ? 0.2f : 0.9f;
    auto pauseBtn = makeQuad(btnW*0.5f, btnY, btnW*0.5f+btnW, btnY+btnH, pauseR, pauseG, pauseB);
    verts.insert(verts.end(), pauseBtn.begin(), pauseBtn.end());

    // Upload e draw
    static unsigned int hVAO=0, hVBO=0;
    if (!hVAO) { glGenVertexArrays(1,&hVAO); glGenBuffers(1,&hVBO); }

    glBindVertexArray(hVAO);
    glBindBuffer(GL_ARRAY_BUFFER, hVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(Vtx2D), verts.data(), GL_DYNAMIC_DRAW);

    // layout: pos(2) + color(3)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);

    glUseProgram(s_HUDShader);
    glUniform1f(glGetUniformLocation(s_HUDShader,"u_Alpha"), 0.92f);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());

    glEnable(GL_DEPTH_TEST);
}

// ----------------------------------------------------------------
// DrawTranslationGizmo
// ----------------------------------------------------------------

void Renderer::DrawTranslationGizmo(const glm::vec3& pos, int activeAxis,
                                     const EditorCamera& cam, int winW, int winH)
{
    // Setas estáticas por cor: vermelho=X, verde=Y, azul=Z
    // Criadas em Init (ver abaixo), mas usamos static local aqui por
    // simplicidade (init lazy na primeira chamada após gladLoadGL)
    static Mesh arrowX = Mesh::CreateGizmoArrow(1.0f, 0.9f, 0.15f, 0.15f);
    static Mesh arrowY = Mesh::CreateGizmoArrow(1.0f, 0.15f, 0.9f, 0.15f);
    static Mesh arrowZ = Mesh::CreateGizmoArrow(1.0f, 0.15f, 0.15f, 0.9f);

    float aspect = (float)winW / (float)winH;
    glm::mat4 view = cam.GetViewMatrix();
    glm::mat4 proj = cam.GetProjection(aspect);

    // Escala constante em tela
    float dist  = glm::length(pos - cam.GetPosition());
    float scale = dist * 0.15f * EditorGizmo::k_ArrowLen;

    glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(scale));

    // Rotações para alinhar a seta (+Y) a cada eixo
    glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,0,1));
    glm::mat4 rotY = glm::mat4(1.0f); // já aponta em +Y
    glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), glm::radians( 90.0f), glm::vec3(1,0,0));

    glUseProgram(s_GizmoShader);
    unsigned int mvpLoc   = glGetUniformLocation(s_GizmoShader, "u_MVP");
    unsigned int alphaLoc = glGetUniformLocation(s_GizmoShader, "u_Alpha");

    glDisable(GL_DEPTH_TEST);

    struct AxisDraw { Mesh* mesh; glm::mat4 rot; int axisId; };
    AxisDraw axes[3] = {
        { &arrowX, rotX, 1 },
        { &arrowY, rotY, 2 },
        { &arrowZ, rotZ, 3 },
    };

    for (auto& a : axes)
    {
        float alpha = (activeAxis == a.axisId) ? 1.0f : 0.75f;
        glUniform1f(alphaLoc, alpha);

        glm::mat4 model = T * a.rot * S;
        glm::mat4 mvp   = proj * view * model;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        a.mesh->Draw();
    }

    glEnable(GL_DEPTH_TEST);
}

// ----------------------------------------------------------------
// Hit test da toolbar
// ----------------------------------------------------------------

bool Renderer::ToolbarClickPlay(int x, int y, int winW, int winH)
{
    float nx =  2.0f * x / (float)winW - 1.0f;
    float ny = -2.0f * y / (float)winH + 1.0f;
    float barH=0.07f, btnW=0.06f, btnH=0.05f;
    float btnY = 1.0f - (barH-btnH)*0.5f - btnH;
    // Play button rendered at makeQuad(-btnW*1.5f, btnY, -btnW*0.5f, btnY+btnH)
    return nx >= -btnW*1.5f && nx <= -btnW*0.5f && ny >= btnY && ny <= btnY+btnH;
}

bool Renderer::ToolbarClickPause(int x, int y, int winW, int winH)
{
    float nx =  2.0f * x / (float)winW - 1.0f;
    float ny = -2.0f * y / (float)winH + 1.0f;
    float barH=0.07f, btnW=0.06f, btnH=0.05f;
    float btnY = 1.0f - (barH-btnH)*0.5f - btnH;
    return nx >= btnW*0.5f && nx <= btnW*1.5f && ny >= btnY && ny <= btnY+btnH;
}

} // namespace tsu