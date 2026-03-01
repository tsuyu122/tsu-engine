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

unsigned int Renderer::s_ShadowShader    = 0;
unsigned int Renderer::s_ShadowFBO       = 0;
unsigned int Renderer::s_ShadowDepthTex  = 0;
glm::mat4    Renderer::s_ShadowLightSpace = glm::mat4(1.0f);
glm::vec3    Renderer::s_ShadowLightDir  = {0.0f,-1.0f,0.0f};
int          Renderer::s_HasShadow       = 0;

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
        out vec3 vColor; out vec3 vNormal; out vec3 vFragPos; out vec3 vLocalPos;
        uniform mat4 u_MVP; uniform mat4 u_Model;
        void main(){
            vColor=aColor;
            vLocalPos=aPos;
            vNormal=mat3(transpose(inverse(u_Model)))*aNormal;
            vFragPos=vec3(u_Model*vec4(aPos,1.0));
            gl_Position=u_MVP*vec4(aPos,1.0);
        }
    )", R"(
        #version 330 core
        in vec3 vColor; in vec3 vNormal; in vec3 vFragPos; in vec3 vLocalPos;
        out vec4 FragColor;

        #define MAX_LIGHTS 8
        struct Light {
            int   type;      // 0=off 1=directional 2=point 3=spot 4=area
            vec3  position;
            vec3  direction; // normalized, for directional/spot
            vec3  color;     // pre-multiplied by intensity & temperature
            float range;
            float innerCos;
            float outerCos;
        };
        uniform Light u_Lights[MAX_LIGHTS];
        uniform int   u_NumLights;
        uniform vec3  u_MaterialColor;
        uniform sampler2D u_Texture;
        uniform int       u_UseTexture;
        // Shadow map (set when at least one Directional/Spot light exists)
        uniform int       u_HasShadow;
        uniform mat4      u_LightSpace;   // lightProj * lightView
        uniform sampler2D u_ShadowMap;
        uniform vec3      u_ShadowDir;    // normalised world-space light direction

        void main(){
            vec3 norm=normalize(vNormal);
            vec3 base;
            if(u_UseTexture!=0){
                vec3 n=abs(norm); n=pow(n,vec3(8.0));
                float sum=n.x+n.y+n.z+0.0001; n/=sum;
                vec2 uvX=(vLocalPos.zy+0.5);
                vec2 uvY=(vLocalPos.xz+0.5);
                vec2 uvZ=(vLocalPos.xy+0.5);
                vec4 tc=texture(u_Texture,uvX)*n.x+texture(u_Texture,uvY)*n.y+texture(u_Texture,uvZ)*n.z;
                base=tc.rgb*u_MaterialColor;
            }else{
                base=vColor*u_MaterialColor;
            }
            vec3 outLight=vec3(0.07);
            if(u_NumLights==0){
                // Fallback: single soft default light when no lights are placed
                vec3 dl=normalize(vec3(0.6,1.0,0.4));
                outLight=vec3(max(dot(norm,dl),0.18));
            } else {
                for(int i=0;i<u_NumLights&&i<MAX_LIGHTS;i++){
                    if(u_Lights[i].type==0) continue;
                    vec3 ldir; float atten=1.0;
                    if(u_Lights[i].type==1){
                        ldir=normalize(-u_Lights[i].direction);
                    } else {
                        vec3 toL=u_Lights[i].position-vFragPos;
                        float dist=length(toL);
                        ldir=dist>0.0001?toL/dist:vec3(0.0,1.0,0.0);
                        if(u_Lights[i].range>0.0){
                            float t=clamp(1.0-dist/u_Lights[i].range,0.0,1.0);
                            atten=t*t;
                        }
                        if(u_Lights[i].type==3){
                            float theta=dot(ldir,-u_Lights[i].direction);
                            float eps=u_Lights[i].innerCos-u_Lights[i].outerCos;
                            atten*=clamp((theta-u_Lights[i].outerCos)/max(eps,0.0001),0.0,1.0);
                        }
                    }
                    float diff=max(dot(norm,ldir),0.0);
                    outLight+=u_Lights[i].color*diff*atten;
                }
            }
            // --- Shadow ---
            float shadowFactor = 1.0;
            if (u_HasShadow != 0) {
                vec4 fragLS = u_LightSpace * vec4(vFragPos, 1.0);
                vec3 proj   = fragLS.xyz / fragLS.w * 0.5 + 0.5;
                if (proj.z < 1.0) {
                    // adaptive bias to avoid shadow acne
                    float bias = max(0.005 * (1.0 - dot(norm, -u_ShadowDir)), 0.0005);
                    float shadow = 0.0;
                    vec2 ts = vec2(1.0 / 2048.0);
                    for (int x = -1; x <= 1; x++)
                        for (int y = -1; y <= 1; y++) {
                            float d = texture(u_ShadowMap, proj.xy + vec2(x,y)*ts).r;
                            shadow += (proj.z - bias > d) ? 1.0 : 0.0;
                        }
                    shadowFactor = 1.0 - shadow * (0.8 / 9.0); // max 80% shadow
                }
            }
            FragColor=vec4(base*outLight*shadowFactor,1.0);
        }
    )");;


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

    // ----------------------------------------------------------------
    // Shadow map: depth-only shader + FBO
    // ----------------------------------------------------------------
    s_ShadowShader = LinkProgram(
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "uniform mat4 u_LightMVP;\n"
        "void main(){ gl_Position = u_LightMVP * vec4(aPos,1.0); }",
        "#version 330 core\nvoid main(){}"
    );

    glGenFramebuffers(1, &s_ShadowFBO);
    glGenTextures(1, &s_ShadowDepthTex);
    glBindTexture(GL_TEXTURE_2D, s_ShadowDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 2048, 2048,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderCol[] = {1.0f, 1.0f, 1.0f, 1.0f}; // area outside map = no shadow
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderCol);
    glBindFramebuffer(GL_FRAMEBUFFER, s_ShadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, s_ShadowDepthTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    unsigned int mvpLoc      = glGetUniformLocation(shader, "u_MVP");
    unsigned int modelLoc    = glGetUniformLocation(shader, "u_Model");
    unsigned int materialLoc = glGetUniformLocation(shader, "u_MaterialColor");
    unsigned int texLoc      = glGetUniformLocation(shader, "u_Texture");
    unsigned int useTexLoc   = glGetUniformLocation(shader, "u_UseTexture");
    glUniform1i(texLoc, 0); // GL_TEXTURE0

    // ---- Pass shadow map uniforms ----
    glUniform1i(glGetUniformLocation(shader, "u_HasShadow"), s_HasShadow);
    if (s_HasShadow)
    {
        glUniformMatrix4fv(glGetUniformLocation(shader, "u_LightSpace"), 1, GL_FALSE, &s_ShadowLightSpace[0][0]);
        glUniform3fv(glGetUniformLocation(shader, "u_ShadowDir"), 1, &s_ShadowLightDir[0]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, s_ShadowDepthTex);
        glUniform1i(glGetUniformLocation(shader, "u_ShadowMap"), 1);
        glActiveTexture(GL_TEXTURE0); // always restore to unit 0
    }

    // ---- Gather active lights and pass to shader (up to 8) ----
    auto kelvinRGB = [](float T) -> glm::vec3 {
        T = glm::clamp(T, 1000.0f, 12000.0f) / 100.0f;
        float r = (T <= 66.0f) ? 1.0f : glm::clamp(329.698727f * powf(T-60.0f,-0.1332048f)/255.0f, 0.0f, 1.0f);
        float g = (T <= 66.0f) ? glm::clamp((99.4708f*logf(T)-161.1196f)/255.0f, 0.0f,1.0f)
                               : glm::clamp(288.1222f*powf(T-60.0f,-0.0755148f)/255.0f, 0.0f,1.0f);
        float b = (T >= 66.0f) ? 1.0f : (T <= 19.0f) ? 0.0f
                               : glm::clamp((138.5177f*logf(T-10.0f)-305.0448f)/255.0f, 0.0f,1.0f);
        return {r, g, b};
    };
    struct LU { int type; glm::vec3 pos, dir, col; float range, ic, oc; };
    std::vector<LU> luArr;
    for (int li = 0; li < (int)scene.Lights.size() && (int)luArr.size() < 8; ++li)
    {
        const auto& lc = scene.Lights[li];
        if (!lc.Active || !lc.Enabled) continue;
        glm::mat4 wm  = scene.GetEntityWorldMatrix(li);
        glm::vec3 pos = glm::vec3(wm[3]);
        glm::vec3 fwd = glm::normalize(glm::vec3(wm * glm::vec4(1,0,0,0)));
        LU lu;
        lu.type  = (int)lc.Type + 1;
        lu.pos   = pos;
        lu.dir   = fwd;
        lu.col   = lc.Color * kelvinRGB(lc.Temperature) * lc.Intensity;
        lu.range = lc.Range;
        lu.ic    = cosf(glm::radians(lc.InnerAngle));
        lu.oc    = cosf(glm::radians(lc.OuterAngle));
        luArr.push_back(lu);
    }
    {
        char nb[64];
        int nl = (int)luArr.size();
        glUniform1i(glGetUniformLocation(shader, "u_NumLights"), nl);
        for (int li = 0; li < 8; ++li)
        {
            snprintf(nb,sizeof(nb),"u_Lights[%d].type",li);
            glUniform1i(glGetUniformLocation(shader,nb), li < nl ? luArr[li].type : 0);
            if (li >= nl) continue;
            const auto& lu = luArr[li];
            snprintf(nb,sizeof(nb),"u_Lights[%d].position", li);  glUniform3fv(glGetUniformLocation(shader,nb),1,&lu.pos[0]);
            snprintf(nb,sizeof(nb),"u_Lights[%d].direction",li);  glUniform3fv(glGetUniformLocation(shader,nb),1,&lu.dir[0]);
            snprintf(nb,sizeof(nb),"u_Lights[%d].color",    li);  glUniform3fv(glGetUniformLocation(shader,nb),1,&lu.col[0]);
            snprintf(nb,sizeof(nb),"u_Lights[%d].range",    li);  glUniform1f(glGetUniformLocation(shader,nb),lu.range);
            snprintf(nb,sizeof(nb),"u_Lights[%d].innerCos", li);  glUniform1f(glGetUniformLocation(shader,nb),lu.ic);
            snprintf(nb,sizeof(nb),"u_Lights[%d].outerCos", li);  glUniform1f(glGetUniformLocation(shader,nb),lu.oc);
        }
    }

    for (size_t i=0; i<scene.Transforms.size(); i++)
    {
        if (!scene.MeshRenderers[i].MeshPtr) continue;
        glm::mat4 model = scene.GetEntityWorldMatrix((int)i);
        glm::mat4 mvp   = proj * view * model;
        // Material
        glm::vec3 matCol(1.0f, 1.0f, 1.0f);
        unsigned int texID = 0;
        if (i < (int)scene.EntityMaterial.size())
        {
            int mid = scene.EntityMaterial[i];
            if (mid >= 0 && mid < (int)scene.Materials.size()) {
                matCol = scene.Materials[mid].Color;
                texID  = scene.Materials[mid].TextureID;
            } else if (i < (int)scene.EntityColors.size()) {
                matCol = scene.EntityColors[i];
            }
        }
        // Bind texture
        if (texID > 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texID);
            glUniform1i(useTexLoc, 1);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glUniform1i(useTexLoc, 0);
        }
        glUniform3fv(materialLoc, 1, &matCol[0]);
        glUniformMatrix4fv(mvpLoc,   1, GL_FALSE, &mvp[0][0]);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
        scene.MeshRenderers[i].MeshPtr->Draw();
    }
    // Unbind texture after draw loop
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ----------------------------------------------------------------
// DrawColliderGizmos — wireframe verde das formas de colisão
// ----------------------------------------------------------------

void Renderer::DrawColliderGizmos(Scene& scene, const glm::mat4& view, const glm::mat4& proj)
{
    struct Vtx { float x,y,z, nx,ny,nz, r,g,b; };
    static unsigned int cVAO = 0, cVBO = 0;
    if (!cVAO) { glGenVertexArrays(1, &cVAO); glGenBuffers(1, &cVBO); }

    glUseProgram(s_GizmoShader);
    unsigned int mvpLoc   = glGetUniformLocation(s_GizmoShader, "u_MVP");
    unsigned int alphaLoc = glGetUniformLocation(s_GizmoShader, "u_Alpha");
    glUniform1f(alphaLoc, 0.9f);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.5f);

    std::vector<Vtx> verts;

    auto addLine = [&](glm::vec3 a, glm::vec3 b, float r=0.1f, float g=1.0f, float bv=0.2f) {
        verts.push_back({a.x,a.y,a.z, 0,1,0, r,g,bv});
        verts.push_back({b.x,b.y,b.z, 0,1,0, r,g,bv});
    };

    const int CIRC_SEG = 16;

    for (int i = 0; i < (int)scene.RigidBodies.size(); i++)
    {
        auto& rb = scene.RigidBodies[i];
        if (!rb.ShowCollider || !rb.HasColliderModule) continue;

        // World matrix do entity: contém posição + rotação + escala do parent chain
        glm::mat4 worldMat = scene.GetEntityWorldMatrix(i);

        // Extrai só a parte de rotação (normaliza as colunas para remover escala)
        glm::mat4 rotMat(1.0f);
        rotMat[0] = glm::vec4(glm::normalize(glm::vec3(worldMat[0])), 0.0f);
        rotMat[1] = glm::vec4(glm::normalize(glm::vec3(worldMat[1])), 0.0f);
        rotMat[2] = glm::vec4(glm::normalize(glm::vec3(worldMat[2])), 0.0f);

        // Posição world do centro do collider (offset também é rotacionado)
        glm::vec3 entityPos = glm::vec3(worldMat[3]);
        glm::vec3 wpos = entityPos + glm::vec3(rotMat * glm::vec4(rb.ColliderOffset, 0.0f));

        // Tamanho do collider aplicando escala world extraída da worldMat
        glm::vec3 worldScale = {
            glm::length(glm::vec3(worldMat[0])),
            glm::length(glm::vec3(worldMat[1])),
            glm::length(glm::vec3(worldMat[2]))
        };
        glm::vec3 sz  = rb.ColliderSize   * worldScale;
        float     rad = rb.ColliderRadius * worldScale.x;
        float     hgt = rb.ColliderHeight * worldScale.y;

        // Helper: transforma ponto local (relativo ao centro do collider) para world-space
        auto toWorld = [&](glm::vec3 local) -> glm::vec3 {
            return wpos + glm::vec3(rotMat * glm::vec4(local, 0.0f));
        };

        if (rb.Collider == ColliderType::Box)
        {
            glm::vec3 h = sz * 0.5f;
            glm::vec3 c[8] = {
                toWorld({-h.x,-h.y,-h.z}), toWorld({+h.x,-h.y,-h.z}),
                toWorld({+h.x,-h.y,+h.z}), toWorld({-h.x,-h.y,+h.z}),
                toWorld({-h.x,+h.y,-h.z}), toWorld({+h.x,+h.y,-h.z}),
                toWorld({+h.x,+h.y,+h.z}), toWorld({-h.x,+h.y,+h.z}),
            };
            int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for (auto& e : edges) addLine(c[e[0]], c[e[1]]);
        }
        else if (rb.Collider == ColliderType::Pyramid)
        {
            glm::vec3 h = sz * 0.5f;
            glm::vec3 apex = toWorld({0.0f, h.y, 0.0f});
            glm::vec3 c[4] = {
                toWorld({-h.x,-h.y,-h.z}), toWorld({+h.x,-h.y,-h.z}),
                toWorld({+h.x,-h.y,+h.z}), toWorld({-h.x,-h.y,+h.z}),
            };
            for (int k=0;k<4;k++) addLine(c[k], c[(k+1)%4]);
            for (int k=0;k<4;k++) addLine(c[k], apex);
        }
        else if (rb.Collider == ColliderType::Sphere)
        {
            for (int axis = 0; axis < 3; axis++) {
                for (int k = 0; k < CIRC_SEG; k++) {
                    float a0 = 2*3.14159f*k/CIRC_SEG, a1 = 2*3.14159f*(k+1)/CIRC_SEG;
                    glm::vec3 p0(0), p1(0);
                    if      (axis==0) { p0={0,sinf(a0)*rad,cosf(a0)*rad}; p1={0,sinf(a1)*rad,cosf(a1)*rad}; }
                    else if (axis==1) { p0={sinf(a0)*rad,0,cosf(a0)*rad}; p1={sinf(a1)*rad,0,cosf(a1)*rad}; }
                    else              { p0={sinf(a0)*rad,cosf(a0)*rad,0}; p1={sinf(a1)*rad,cosf(a1)*rad,0}; }
                    addLine(toWorld(p0), toWorld(p1));
                }
            }
        }
        else if (rb.Collider == ColliderType::Capsule)
        {
            float halfH = hgt * 0.5f;
            // --- Cylinder body ---
            for (int k = 0; k < CIRC_SEG; k++) {
                float a0=2*3.14159f*k/CIRC_SEG, a1=2*3.14159f*(k+1)/CIRC_SEG;
                glm::vec3 t0={sinf(a0)*rad, halfH, cosf(a0)*rad};
                glm::vec3 t1={sinf(a1)*rad, halfH, cosf(a1)*rad};
                glm::vec3 b0={sinf(a0)*rad,-halfH, cosf(a0)*rad};
                glm::vec3 b1={sinf(a1)*rad,-halfH, cosf(a1)*rad};
                addLine(toWorld(t0), toWorld(t1));  // top ring
                addLine(toWorld(b0), toWorld(b1));  // bottom ring
                if (k%4==0) addLine(toWorld(t0), toWorld(b0));  // 4 vertical struts
            }
            // --- Hemisphere caps (4 meridian arcs each) ---
            const int HEMI = CIRC_SEG / 2;
            for (int arc = 0; arc < 4; arc++) {
                float phi = arc * 3.14159f * 0.5f;
                float cp = cosf(phi), sp = sinf(phi);
                for (int k = 0; k < HEMI; k++) {
                    float t0 = k * 3.14159f / (2 * HEMI);
                    float t1 = (k+1) * 3.14159f / (2 * HEMI);
                    // Top hemisphere: goes from equator (t=0) to pole (t=pi/2)
                    glm::vec3 tp0 = {cp*cosf(t0)*rad,  halfH+sinf(t0)*rad, sp*cosf(t0)*rad};
                    glm::vec3 tp1 = {cp*cosf(t1)*rad,  halfH+sinf(t1)*rad, sp*cosf(t1)*rad};
                    // Bottom hemisphere: goes from equator down to pole
                    glm::vec3 bp0 = {cp*cosf(t0)*rad, -halfH-sinf(t0)*rad, sp*cosf(t0)*rad};
                    glm::vec3 bp1 = {cp*cosf(t1)*rad, -halfH-sinf(t1)*rad, sp*cosf(t1)*rad};
                    addLine(toWorld(tp0), toWorld(tp1));
                    addLine(toWorld(bp0), toWorld(bp1));
                }
            }
        }
    }

    if (!verts.empty())
    {
        glBindVertexArray(cVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cVBO);
        glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(Vtx), verts.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(6*sizeof(float)));
        glEnableVertexAttribArray(2);
        // Vértices já em world-space → model = identidade
        glm::mat4 mvp = proj * view;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        glDrawArrays(GL_LINES, 0, (GLsizei)verts.size());
    }

    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
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

        // Seta aponta para onde a câmera olha (-Z local = coluna 2 negada)
        // Derivado do world matrix do Transform do entity (não de GameCamera.Front)
        glm::mat4 wm        = scene.GetEntityWorldMatrix((int)i);
        glm::vec3 camRight   =  glm::normalize(glm::vec3(wm[0]));
        glm::vec3 camUp      =  glm::normalize(glm::vec3(wm[1]));
        glm::vec3 camForward = -glm::normalize(glm::vec3(wm[2])); // câmera olha -Z
        // Rotation matrix: columns are the new axes (GLM column-major: mat[col][row])
        glm::mat4 rot(1.0f);
        rot[0] = glm::vec4(camRight,   0.0f);  // column 0 = camera right
        rot[1] = glm::vec4(camUp,      0.0f);  // column 1 = camera up
        rot[2] = glm::vec4(camForward, 0.0f);  // column 2 = camera forward
        glm::mat4 lineModel = glm::translate(glm::mat4(1.0f), pos) * rot;
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

    RenderShadowPass(scene);
    glViewport(0, 0, winW, winH);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    DrawScene(scene, view, projection, s_ShaderProgram);
    DrawColliderGizmos(scene, view, projection);
    DrawGizmos(scene, view, projection);
}

// ----------------------------------------------------------------
// RenderSceneGame
// ----------------------------------------------------------------

void Renderer::RenderSceneGame(Scene& scene, int winW, int winH)
{
    int camIdx = scene.GetActiveGameCamera();
    if (camIdx < 0)
    {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }

    float aspect        = (float)winW / (float)winH;
    auto& gc            = scene.GameCameras[camIdx];

    // View derivada do world matrix do Transform (so the entity Rotation drives the camera)
    glm::mat4 wm        = scene.GetEntityWorldMatrix(camIdx);
    glm::vec3 pos       = glm::vec3(wm[3]);
    glm::vec3 camFwd    = -glm::normalize(glm::vec3(wm[2]));
    glm::vec3 camUp     =  glm::normalize(glm::vec3(wm[1]));
    glm::mat4 view      = glm::lookAt(pos, pos + camFwd, camUp);
    glm::mat4 proj      = gc.GetProjection(aspect);

    RenderShadowPass(scene);
    glViewport(0, 0, winW, winH);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    DrawScene(scene, view, proj, s_ShaderProgram);
}

// ----------------------------------------------------------------
// RenderShadowPass
// ----------------------------------------------------------------

void Renderer::RenderShadowPass(Scene& scene)
{
    s_HasShadow = 0;
    for (int li = 0; li < (int)scene.Lights.size(); ++li)
    {
        const auto& lc = scene.Lights[li];
        if (!lc.Active || !lc.Enabled) continue;
        if (lc.Type != LightType::Directional && lc.Type != LightType::Spot) continue;

        glm::mat4 wm  = scene.GetEntityWorldMatrix(li);
        glm::vec3 pos = glm::vec3(wm[3]);
        glm::vec3 fwd = glm::normalize(glm::vec3(wm * glm::vec4(1,0,0,0)));
        glm::vec3 up  = (fabsf(glm::dot(fwd, glm::vec3(0,1,0))) < 0.99f)
                        ? glm::vec3(0,1,0) : glm::vec3(1,0,0);

        glm::mat4 lightView, lightProj;
        if (lc.Type == LightType::Directional)
        {
            // Sun: orthographic from a far eye position, centred on world origin
            glm::vec3 eye = -fwd * 40.0f;
            lightView = glm::lookAt(eye, glm::vec3(0,0,0), up);
            lightProj = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f, 0.1f, 100.0f);
        }
        else // Spot
        {
            lightView = glm::lookAt(pos, pos + fwd, up);
            lightProj = glm::perspective(glm::radians(lc.OuterAngle * 2.0f), 1.0f, 0.1f,
                                         lc.Range > 0.0f ? lc.Range : 50.0f);
        }

        s_ShadowLightSpace = lightProj * lightView;
        s_ShadowLightDir   = fwd;
        s_HasShadow        = 1;

        // Render depth to the shadow FBO
        glViewport(0, 0, 2048, 2048);
        glBindFramebuffer(GL_FRAMEBUFFER, s_ShadowFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);   // avoid peter-panning self-shadow artifacts

        glUseProgram(s_ShadowShader);
        unsigned int loc = glGetUniformLocation(s_ShadowShader, "u_LightMVP");
        for (int i = 0; i < (int)scene.Transforms.size(); ++i)
        {
            if (!scene.MeshRenderers[i].MeshPtr) continue;
            glm::mat4 m = s_ShadowLightSpace * scene.GetEntityWorldMatrix(i);
            glUniformMatrix4fv(loc, 1, GL_FALSE, &m[0][0]);
            scene.MeshRenderers[i].MeshPtr->Draw();
        }

        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        break; // only the first matching light casts shadows
    }
}

// ----------------------------------------------------------------
// RenderToolbar — barra Play/Pause simples no topo
// ----------------------------------------------------------------

void Renderer::RenderToolbar(bool isPlaying, bool isPaused, int winW, int winH,
                             float topOffsetPx)
{
    glDisable(GL_DEPTH_TEST);

    const float barH = 34.0f;
    const float btnW = 68.0f;
    const float btnH = 24.0f;
    const float spacing = 12.0f;
    const float totalW = btnW * 2.0f + spacing;
    const float centerX = (float)winW * 0.5f;
    const float barY0 = topOffsetPx;
    const float barY1 = barY0 + barH;
    const float btnY0 = barY0 + (barH - btnH) * 0.5f;
    const float btnY1 = btnY0 + btnH;
    const float playX0 = centerX - totalW * 0.5f;
    const float playX1 = playX0 + btnW;
    const float pauseX0 = playX1 + spacing;
    const float pauseX1 = pauseX0 + btnW;

    struct Vtx2D { float x, y, r, g, b; };

    auto makeQuadPx = [winW, winH](float x0, float y0, float x1, float y1,
                                    float r, float g, float b) -> std::vector<Vtx2D>
    {
        float nx0 =  2.0f * x0 / (float)winW - 1.0f;
        float nx1 =  2.0f * x1 / (float)winW - 1.0f;
        float ny0 = -2.0f * y0 / (float)winH + 1.0f;
        float ny1 = -2.0f * y1 / (float)winH + 1.0f;
        return {
            {nx0,ny0, r,g,b}, {nx1,ny0, r,g,b}, {nx1,ny1, r,g,b},
            {nx0,ny0, r,g,b}, {nx1,ny1, r,g,b}, {nx0,ny1, r,g,b},
        };
    };

    std::vector<Vtx2D> verts;

    auto bg = makeQuadPx(0.0f, barY0, (float)winW, barY1, 0.13f, 0.13f, 0.15f);
    verts.insert(verts.end(), bg.begin(), bg.end());

    // Start: red when engine is running, green when idle
    float playR = isPlaying ? 0.88f : 0.18f;
    float playG = isPlaying ? 0.20f : 0.72f;
    float playB = isPlaying ? 0.20f : 0.26f;
    auto playBtn = makeQuadPx(playX0, btnY0, playX1, btnY1, playR, playG, playB);
    verts.insert(verts.end(), playBtn.begin(), playBtn.end());

    float pauseR = isPaused ? 0.94f : 0.28f;
    float pauseG = isPaused ? 0.84f : 0.30f;
    float pauseB = isPaused ? 0.16f : 0.86f;
    auto pauseBtn = makeQuadPx(pauseX0, btnY0, pauseX1, btnY1, pauseR, pauseG, pauseB);
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
    glUniform1f(glGetUniformLocation(s_HUDShader,"u_Alpha"), 0.94f);
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

bool Renderer::ToolbarClickPlay(int x, int y, int winW, int winH, float topOffsetPx)
{
    const float barH=34.0f, btnW=68.0f, spacing=12.0f;
    const float totalW = btnW * 2.0f + spacing;
    const float centerX = (float)winW * 0.5f;
    const float playX0 = centerX - totalW * 0.5f;
    const float playX1 = playX0 + btnW;
    return (float)x >= playX0 && (float)x <= playX1 &&
           (float)y >= topOffsetPx && (float)y <= (topOffsetPx + barH);
}

bool Renderer::ToolbarClickPause(int x, int y, int winW, int winH, float topOffsetPx)
{
    const float barH=34.0f, btnW=68.0f, spacing=12.0f;
    const float totalW = btnW * 2.0f + spacing;
    const float centerX = (float)winW * 0.5f;
    const float pauseX0 = centerX - totalW * 0.5f + btnW + spacing;
    const float pauseX1 = pauseX0 + btnW;
    return (float)x >= pauseX0 && (float)x <= pauseX1 &&
           (float)y >= topOffsetPx && (float)y <= (topOffsetPx + barH);
}

} // namespace tsu