#include "renderer/renderer.h"
#include "renderer/mesh.h"
#include "renderer/textureLoader.h"
#include "editor/editorGizmo.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
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
int          Renderer::s_ShadowLightIdx  = -1;

unsigned int Renderer::s_PointShadowShader   = 0;
unsigned int Renderer::s_PointShadowFBO      = 0;
unsigned int Renderer::s_PointShadowCubemap  = 0;
int          Renderer::s_HasPointShadow      = 0;
int          Renderer::s_PointShadowLightIdx = -1;
glm::vec3    Renderer::s_PointShadowPos      = {0,0,0};
float        Renderer::s_PointShadowFar      = 25.0f;

Renderer::ShaderLocs Renderer::s_Locs = {};
std::vector<glm::mat4> Renderer::s_CachedWorldMats;

// Build world matrix cache once per frame — avoids redundant recursive parent traversal
void Renderer::CacheWorldMatrices(Scene& scene)
{
    int n = (int)scene.Transforms.size();
    s_CachedWorldMats.resize(n);
    for (int i = 0; i < n; i++)
        s_CachedWorldMats[i] = scene.GetEntityWorldMatrix(i);
}

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

static unsigned int LinkProgramVGF(const char* vs, const char* gs, const char* fs)
{
    unsigned int v = CompileShader(GL_VERTEX_SHADER, vs);
    unsigned int g = CompileShader(GL_GEOMETRY_SHADER, gs);
    unsigned int f = CompileShader(GL_FRAGMENT_SHADER, fs);
    unsigned int p = glCreateProgram();
    glAttachShader(p,v); glAttachShader(p,g); glAttachShader(p,f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(g); glDeleteShader(f);
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

    // ----------------------------------------------------------------
    // PBR Shader (Cook-Torrance BRDF, triplanar mapping)
    // ----------------------------------------------------------------
    s_ShaderProgram = LinkProgram(R"(
        #version 460 core
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;
        layout(location=2) in vec3 aColor;
        out vec3 vColor; out vec3 vNormal; out vec3 vFragPos; out vec3 vLocalPos;
        uniform mat4 u_MVP; uniform mat4 u_Model;
        void main(){
            vColor    = aColor;
            vLocalPos = aPos;
            vNormal   = mat3(transpose(inverse(u_Model))) * aNormal;
            vFragPos  = vec3(u_Model * vec4(aPos, 1.0));
            gl_Position = u_MVP * vec4(aPos, 1.0);
        }
    )", R"(
        #version 460 core
        in vec3 vColor; in vec3 vNormal; in vec3 vFragPos; in vec3 vLocalPos;
        out vec4 FragColor;

        // ---------- Light system ----------
        #define MAX_LIGHTS 8
        struct Light {
            int   type;       // 0=off 1=dir 2=point 3=spot 4=area
            vec3  position;
            vec3  direction;
            vec3  color;      // pre-multiplied (intensity * temp filter)
            float range;
            float innerCos;
            float outerCos;
        };
        uniform Light     u_Lights[MAX_LIGHTS];
        uniform int       u_NumLights;

        // ---------- Material ----------
        uniform vec3      u_MaterialColor;
        uniform sampler2D u_Texture;         // unit 0
        uniform int       u_UseTexture;
        uniform sampler2D u_NormalMap;       // unit 2
        uniform int       u_UseNormal;
        uniform sampler2D u_ORMMap;          // unit 3
        uniform int       u_UseORM;
        uniform float     u_Roughness;
        uniform float     u_Metallic;
        uniform float     u_AOValue;
        uniform vec2      u_Tiling;
        uniform int       u_WorldSpaceUV;

        // ---------- Camera ----------
        uniform vec3      u_ViewPos;

        // ---------- 2D Shadow (dir/spot, unit 1) ----------
        uniform int       u_HasShadow;
        uniform mat4      u_LightSpace;
        uniform sampler2D u_ShadowMap;
        uniform vec3      u_ShadowDir;
        uniform int       u_ShadowLightIdx;

        // ---------- Point shadow (cubemap, unit 4) ----------
        uniform int          u_HasPointShadow;
        uniform samplerCube  u_PointShadowMap;
        uniform vec3         u_PointShadowPos;
        uniform float        u_PointShadowFar;
        uniform int          u_PointShadowLightIdx;

        // =====================================================================
        // PBR helpers
        // =====================================================================
        const float PI = 3.14159265359;

        float DistGGX(float NdotH, float a2){
            float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
            return a2 / (PI * d * d + 0.0001);
        }
        float GeoSchlick(float NdotV, float k){ return NdotV / (NdotV*(1.0-k)+k); }
        float GeoSmith(float NdotV, float NdotL, float rough){
            float k = (rough+1.0);  k = k*k/8.0;
            return GeoSchlick(max(NdotV,0.0),k) * GeoSchlick(max(NdotL,0.0),k);
        }
        vec3 FresnelSchlick(float cos, vec3 F0){
            return F0 + (1.0-F0)*pow(clamp(1.0-cos,0.0,1.0),5.0);
        }
        vec3 FresnelRoughness(float cos, vec3 F0, float rough){
            return F0 + (max(vec3(1.0-rough),F0)-F0)*pow(clamp(1.0-cos,0.0,1.0),5.0);
        }

        // Triplanar UV helper
        vec3 triBlend(vec3 n){ vec3 b=pow(abs(n),vec3(8.0)); return b/(b.x+b.y+b.z+0.0001); }
        vec4 triSample(sampler2D t, vec3 pos, vec3 b){
            return texture(t,pos.zy)*b.x + texture(t,pos.xz)*b.y + texture(t,pos.xy)*b.z;
        }

        // ---------- Shadow helpers ----------
        float calcShadow2D(vec3 fragPos, vec3 N){
            vec4 fragLS = u_LightSpace * vec4(fragPos, 1.0);
            vec3 proj   = fragLS.xyz / fragLS.w * 0.5 + 0.5;
            if(proj.z >= 1.0) return 1.0;
            float bias = max(0.005*(1.0-dot(N,-u_ShadowDir)),0.0005);
            float shadow = 0.0;
            vec2 ts = vec2(1.0/1024.0);
            for(int x=-1;x<=1;x++)
                for(int y=-1;y<=1;y++){
                    float d=texture(u_ShadowMap,proj.xy+vec2(x,y)*ts).r;
                    shadow+=(proj.z-bias>d)?1.0:0.0;
                }
            return 1.0 - shadow*(0.7/9.0);
        }

        float calcPointShadow(vec3 fragPos){
            vec3 fragToLight = fragPos - u_PointShadowPos;
            float currentDepth = length(fragToLight);
            // PCF: sample 4 offset directions for smoother edges
            vec3 sampleOffsetDir[4] = vec3[](
                vec3(1,1,1), vec3(-1,-1,1), vec3(-1,1,-1), vec3(1,-1,-1)
            );
            float diskRadius = 0.03;
            float shadow = 0.0;
            float bias = 0.15 + 0.05 * (currentDepth / u_PointShadowFar);
            for(int s=0; s<4; s++){
                vec3 dir = fragToLight + sampleOffsetDir[s] * diskRadius * currentDepth;
                float closestDepth = texture(u_PointShadowMap, dir).r * u_PointShadowFar;
                shadow += (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
            }
            shadow /= 4.0;
            return 1.0 - shadow * 0.8;
        }

        void main(){
            vec3 geoNorm = normalize(vNormal);
            vec3 blend   = triBlend(geoNorm);
            vec3 pos     = (u_WorldSpaceUV!=0) ? vFragPos : vLocalPos;
            pos.x *= u_Tiling.x; pos.z *= u_Tiling.x; pos.y *= u_Tiling.y;

            // ---- Albedo ----
            vec3 albedo;
            if(u_UseTexture!=0){
                albedo = triSample(u_Texture, pos, blend).rgb * u_MaterialColor;
            } else {
                albedo = vColor * u_MaterialColor;
            }

            // ---- Normal map ----
            vec3 N = geoNorm;
            if(u_UseNormal!=0){
                vec3 tnX = texture(u_NormalMap, pos.zy).rgb * 2.0 - 1.0;
                vec3 tnY = texture(u_NormalMap, pos.xz).rgb * 2.0 - 1.0;
                vec3 tnZ = texture(u_NormalMap, pos.xy).rgb * 2.0 - 1.0;
                vec3 pertX = vec3(0.0,  tnX.y, tnX.x) * blend.x;
                vec3 pertY = vec3(tnY.x, 0.0, tnY.y)  * blend.y;
                vec3 pertZ = vec3(tnZ.x, tnZ.y, 0.0)  * blend.z;
                N = normalize(geoNorm + pertX + pertY + pertZ);
            }

            // ---- ORM ----
            float ao        = u_AOValue;
            float roughness = u_Roughness;
            float metallic  = u_Metallic;
            if(u_UseORM!=0){
                vec3 orm = triSample(u_ORMMap, pos, blend).rgb;
                ao        = orm.r;
                roughness = orm.g;
                metallic  = orm.b;
            }

            // ---- PBR BRDF (Cook-Torrance) ----
            vec3  V  = normalize(u_ViewPos - vFragPos);
            float NdotV = max(dot(N, V), 0.0);
            vec3 F0 = mix(vec3(0.04), albedo, metallic);
            float a  = roughness * roughness;
            float a2 = a * a;

            vec3 Lo = vec3(0.0);

            if(u_NumLights == 0){
                // Fallback single light
                vec3 L = normalize(vec3(0.6, 1.0, 0.4));
                vec3 H = normalize(V + L);
                float NdotL = max(dot(N,L), 0.0);
                float NdotH = max(dot(N,H), 0.0);
                float HdotV = max(dot(H,V), 0.0);
                vec3  F     = FresnelSchlick(HdotV, F0);
                float G     = GeoSmith(NdotV, NdotL, roughness);
                float D     = DistGGX(NdotH, a2);
                vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
                vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
                Lo += (kD * albedo / PI + specular) * vec3(1.0) * NdotL;
            } else {
                for(int i=0; i<u_NumLights && i<MAX_LIGHTS; i++){
                    if(u_Lights[i].type==0) continue;
                    vec3  L; float atten=1.0;
                    if(u_Lights[i].type==1){
                        L = normalize(-u_Lights[i].direction);
                    } else {
                        vec3 toL  = u_Lights[i].position - vFragPos;
                        float dist = length(toL);
                        L = dist>0.0001 ? toL/dist : vec3(0,1,0);
                        if(u_Lights[i].range>0.0){
                            float tt=clamp(1.0-dist/u_Lights[i].range,0.0,1.0);
                            atten=tt*tt;
                        }
                        if(u_Lights[i].type==3){
                            float theta=dot(L,-u_Lights[i].direction);
                            float eps=u_Lights[i].innerCos-u_Lights[i].outerCos;
                            atten*=clamp((theta-u_Lights[i].outerCos)/max(eps,0.0001),0.0,1.0);
                        }
                    }
                    vec3  H     = normalize(V + L);
                    float NdotL = max(dot(N,L), 0.0);
                    float NdotH = max(dot(N,H), 0.0);
                    float HdotV = max(dot(H,V), 0.0);
                    vec3  F     = FresnelSchlick(HdotV, F0);
                    float G     = GeoSmith(NdotV, NdotL, roughness);
                    float D     = DistGGX(NdotH, a2);
                    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
                    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

                    // Per-light shadow
                    float shadowAtten = 1.0;
                    if(u_HasShadow != 0 && i == u_ShadowLightIdx)
                        shadowAtten = min(shadowAtten, calcShadow2D(vFragPos, N));
                    if(u_HasPointShadow != 0 && i == u_PointShadowLightIdx)
                        shadowAtten = min(shadowAtten, calcPointShadow(vFragPos));

                    Lo += (kD * albedo / PI + specular) * u_Lights[i].color * NdotL * atten * shadowAtten;
                }
            }

            // Ambient
            vec3 kS_amb = FresnelRoughness(NdotV, F0, roughness);
            vec3 kD_amb = (1.0 - kS_amb) * (1.0 - metallic);
            vec3 ambient = kD_amb * albedo * 0.08 * ao;

            vec3 color = ambient + Lo;

            // Tone-mapping: Reinhard
            color = color / (color + vec3(1.0));
            // Gamma correction
            color = pow(color, vec3(1.0/2.2));

            FragColor = vec4(color, 1.0);
        }
    )");;


    // Gizmo shader — unlit, with alpha
    s_GizmoShader = LinkProgram(R"(
        #version 460 core
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;
        layout(location=2) in vec3 aColor;
        out vec3 vColor;
        uniform mat4 u_MVP;
        void main(){ vColor=aColor; gl_Position=u_MVP*vec4(aPos,1.0); }
    )", R"(
        #version 460 core
        in vec3 vColor;
        out vec4 FragColor;
        uniform float u_Alpha;
        void main(){ FragColor=vec4(vColor,u_Alpha); }
    )");

    // Shader HUD 2D (NDC direto)
    s_HUDShader = LinkProgram(R"(
        #version 460 core
        layout(location=0) in vec2 aPos;
        layout(location=1) in vec3 aColor;
        out vec3 vColor;
        void main(){ vColor=aColor; gl_Position=vec4(aPos,0.0,1.0); }
    )", R"(
        #version 460 core
        in vec3 vColor;
        out vec4 FragColor;
        uniform float u_Alpha;
        void main(){ FragColor=vec4(vColor,u_Alpha); }
    )");

    InitGizmoMeshes();

    // ----------------------------------------------------------------
    // Shadow map: depth-only shader + FBO (1024x1024)
    // ----------------------------------------------------------------
    s_ShadowShader = LinkProgram(
        "#version 460 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "uniform mat4 u_LightMVP;\n"
        "void main(){ gl_Position = u_LightMVP * vec4(aPos,1.0); }",
        "#version 460 core\nvoid main(){}"
    );

    glGenFramebuffers(1, &s_ShadowFBO);
    glGenTextures(1, &s_ShadowDepthTex);
    glBindTexture(GL_TEXTURE_2D, s_ShadowDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 1024, 1024,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderCol[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderCol);
    glBindFramebuffer(GL_FRAMEBUFFER, s_ShadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, s_ShadowDepthTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ----------------------------------------------------------------
    // Point light cubemap shadow: VS + GS + FS + cubemap FBO (512x512)
    // ----------------------------------------------------------------
    s_PointShadowShader = LinkProgramVGF(
        // Vertex: transform to world space
        "#version 460 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "uniform mat4 u_Model;\n"
        "void main(){ gl_Position = u_Model * vec4(aPos,1.0); }\n",
        // Geometry: emit to each cubemap face
        "#version 460 core\n"
        "layout(triangles) in;\n"
        "layout(triangle_strip, max_vertices=18) out;\n"
        "uniform mat4 u_ShadowMatrices[6];\n"
        "out vec4 gFragPos;\n"
        "void main(){\n"
        "  for(int face=0;face<6;face++){\n"
        "    gl_Layer=face;\n"
        "    for(int i=0;i<3;i++){\n"
        "      gFragPos=gl_in[i].gl_Position;\n"
        "      gl_Position=u_ShadowMatrices[face]*gFragPos;\n"
        "      EmitVertex();\n"
        "    }\n"
        "    EndPrimitive();\n"
        "  }\n"
        "}\n",
        // Fragment: write linear depth
        "#version 460 core\n"
        "in vec4 gFragPos;\n"
        "uniform vec3  u_LightPos;\n"
        "uniform float u_FarPlane;\n"
        "void main(){\n"
        "  float dist=length(gFragPos.xyz - u_LightPos);\n"
        "  gl_FragDepth = dist / u_FarPlane;\n"
        "}\n"
    );

    const int CUBE_SZ = 512;
    glGenTextures(1, &s_PointShadowCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, s_PointShadowCubemap);
    for (int i = 0; i < 6; i++)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT,
                     CUBE_SZ, CUBE_SZ, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &s_PointShadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, s_PointShadowFBO);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, s_PointShadowCubemap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ----------------------------------------------------------------
    // Cache all uniform locations for the main PBR shader
    // ----------------------------------------------------------------
    {
        auto L = [](const char* n) { return glGetUniformLocation(s_ShaderProgram, n); };
        s_Locs.mvp           = L("u_MVP");
        s_Locs.model         = L("u_Model");
        s_Locs.materialColor = L("u_MaterialColor");
        s_Locs.texture0      = L("u_Texture");
        s_Locs.useTexture    = L("u_UseTexture");
        s_Locs.shadowMap     = L("u_ShadowMap");
        s_Locs.normalMap     = L("u_NormalMap");
        s_Locs.ormMap        = L("u_ORMMap");
        s_Locs.viewPos       = L("u_ViewPos");
        s_Locs.hasShadow     = L("u_HasShadow");
        s_Locs.lightSpace    = L("u_LightSpace");
        s_Locs.shadowDir     = L("u_ShadowDir");
        s_Locs.shadowLightIdx= L("u_ShadowLightIdx");
        s_Locs.hasPointShadow      = L("u_HasPointShadow");
        s_Locs.pointShadowMap      = L("u_PointShadowMap");
        s_Locs.pointShadowPos      = L("u_PointShadowPos");
        s_Locs.pointShadowFar      = L("u_PointShadowFar");
        s_Locs.pointShadowLightIdx = L("u_PointShadowLightIdx");
        s_Locs.numLights     = L("u_NumLights");
        s_Locs.useNormal     = L("u_UseNormal");
        s_Locs.useORM        = L("u_UseORM");
        s_Locs.roughness     = L("u_Roughness");
        s_Locs.metallic      = L("u_Metallic");
        s_Locs.aoValue       = L("u_AOValue");
        s_Locs.tiling        = L("u_Tiling");
        s_Locs.worldSpaceUV  = L("u_WorldSpaceUV");
        char nb[64];
        for (int i = 0; i < 8; i++) {
            auto& ll = s_Locs.lights[i];
            snprintf(nb,64,"u_Lights[%d].type",i);      ll.type     = glGetUniformLocation(s_ShaderProgram,nb);
            snprintf(nb,64,"u_Lights[%d].position",i);   ll.position = glGetUniformLocation(s_ShaderProgram,nb);
            snprintf(nb,64,"u_Lights[%d].direction",i);  ll.direction= glGetUniformLocation(s_ShaderProgram,nb);
            snprintf(nb,64,"u_Lights[%d].color",i);      ll.color    = glGetUniformLocation(s_ShaderProgram,nb);
            snprintf(nb,64,"u_Lights[%d].range",i);      ll.range    = glGetUniformLocation(s_ShaderProgram,nb);
            snprintf(nb,64,"u_Lights[%d].innerCos",i);   ll.innerCos = glGetUniformLocation(s_ShaderProgram,nb);
            snprintf(nb,64,"u_Lights[%d].outerCos",i);   ll.outerCos = glGetUniformLocation(s_ShaderProgram,nb);
        }
    }
}

void Renderer::InitGizmoMeshes()
{
    // Gizmo sphere
    {
        Mesh m = Mesh::CreateGizmoSphere(0.08f);
        // We reuse the VAO created inside the Mesh — need to keep it
        // Hack: we create a temporary Mesh and get the VAO via Draw() which is not ideal,
        // so we recreate inline with the data
        // (The Mesh VAO is private — we use Draw() normally via pointer)
    }
    // We use static Meshes allocated once
    static Mesh gizmoSphere = Mesh::CreateGizmoSphere(0.08f);
    static Mesh gizmoLine   = Mesh::CreateGizmoLine(0.7f);
    // We keep reference via static pointer
    // (Draw() is public, so we call directly in the render functions)
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
// DrawScene (optimized — cached uniform locations, per-light shadow)
// ----------------------------------------------------------------

void Renderer::DrawScene(Scene& scene, const glm::mat4& view,
                         const glm::mat4& proj, unsigned int shader)
{
    glUseProgram(shader);
    const auto& L = s_Locs;  // shorthand

    // ---- Bind texture unit assignments (constant) ----
    glUniform1i(L.texture0,  0);   // albedo  → unit 0
    glUniform1i(L.shadowMap, 1);   // 2D shadow → unit 1
    glUniform1i(L.normalMap, 2);   // normal  → unit 2
    glUniform1i(L.ormMap,    3);   // ORM     → unit 3
    glUniform1i(L.pointShadowMap, 4); // point shadow cubemap → unit 4

    // ---- Camera world position ----
    glm::vec3 viewPos = glm::vec3(glm::inverse(view)[3]);
    glUniform3fv(L.viewPos, 1, &viewPos[0]);

    // ---- 2D Shadow map uniforms (always bind to avoid stale data) ----
    glUniform1i(L.hasShadow, s_HasShadow);
    glUniform1i(L.shadowLightIdx, s_ShadowLightIdx);
    glActiveTexture(GL_TEXTURE1);
    if (s_HasShadow)
    {
        glUniformMatrix4fv(L.lightSpace, 1, GL_FALSE, &s_ShadowLightSpace[0][0]);
        glUniform3fv(L.shadowDir, 1, &s_ShadowLightDir[0]);
        glBindTexture(GL_TEXTURE_2D, s_ShadowDepthTex);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ---- Point shadow cubemap uniforms (always bind to avoid stale data) ----
    glUniform1i(L.hasPointShadow, s_HasPointShadow);
    glUniform1i(L.pointShadowLightIdx, s_PointShadowLightIdx);
    glActiveTexture(GL_TEXTURE4);
    if (s_HasPointShadow)
    {
        glUniform3fv(L.pointShadowPos, 1, &s_PointShadowPos[0]);
        glUniform1f(L.pointShadowFar, s_PointShadowFar);
        glBindTexture(GL_TEXTURE_CUBE_MAP, s_PointShadowCubemap);
    }
    else
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    // ---- Gather active lights (up to 8) ----
    auto kelvinRGB = [](float T) -> glm::vec3 {
        T = glm::clamp(T, 1000.0f, 12000.0f) / 100.0f;
        float r = (T <= 66.0f) ? 1.0f : glm::clamp(329.698727f * powf(T-60.0f,-0.1332048f)/255.0f, 0.0f, 1.0f);
        float g = (T <= 66.0f) ? glm::clamp((99.4708f*logf(T)-161.1196f)/255.0f, 0.0f,1.0f)
                               : glm::clamp(288.1222f*powf(T-60.0f,-0.0755148f)/255.0f, 0.0f,1.0f);
        float b = (T >= 66.0f) ? 1.0f : (T <= 19.0f) ? 0.0f
                               : glm::clamp((138.5177f*logf(T-10.0f)-305.0448f)/255.0f, 0.0f,1.0f);
        return {r, g, b};
    };

    int nl = 0;
    for (int li = 0; li < (int)scene.Lights.size() && nl < 8; ++li)
    {
        const auto& lc = scene.Lights[li];
        if (!lc.Active || !lc.Enabled) continue;
        glm::mat4 wm  = s_CachedWorldMats[li];
        glm::vec3 pos = glm::vec3(wm[3]);
        glm::vec3 fwd = glm::normalize(glm::vec3(wm * glm::vec4(1,0,0,0)));
        const auto& ll = L.lights[nl];
        glUniform1i (ll.type,      (int)lc.Type + 1);
        glUniform3fv(ll.position,  1, &pos[0]);
        glUniform3fv(ll.direction, 1, &fwd[0]);
        glm::vec3 col = lc.Color * kelvinRGB(lc.Temperature) * lc.Intensity;
        glUniform3fv(ll.color, 1, &col[0]);
        glUniform1f (ll.range,    lc.Range);
        glUniform1f (ll.innerCos, cosf(glm::radians(lc.InnerAngle)));
        glUniform1f (ll.outerCos, cosf(glm::radians(lc.OuterAngle)));
        nl++;
    }
    // Zero out unused light slots
    for (int li = nl; li < 8; ++li)
        glUniform1i(L.lights[li].type, 0);
    glUniform1i(L.numLights, nl);

    // ---- Draw entities ----
    for (size_t i = 0; i < scene.Transforms.size(); i++)
    {
        if (!scene.MeshRenderers[i].MeshPtr) continue;
        glm::mat4 model = s_CachedWorldMats[(int)i];
        glm::mat4 mvp   = proj * view * model;

        // ---- Resolve material ----
        glm::vec3    matCol(1.0f, 1.0f, 1.0f);
        unsigned int albedoID = 0;
        unsigned int normalID = 0;
        unsigned int ormID    = 0;
        float roughness = 0.5f, metallic = 0.0f, aoVal = 1.0f;
        glm::vec2 matTiling(1.0f, 1.0f);
        int       matWorldUV = 0;

        if (i < (int)scene.EntityMaterial.size())
        {
            int mid = scene.EntityMaterial[i];
            if (mid >= 0 && mid < (int)scene.Materials.size())
            {
                auto& mat = scene.Materials[mid];
                matCol    = mat.Color;
                albedoID  = mat.AlbedoID;
                normalID  = mat.NormalID;
                roughness = mat.Roughness;
                metallic  = mat.Metallic;
                aoVal     = mat.AOValue;
                matTiling  = mat.Tiling;
                matWorldUV = mat.WorldSpaceUV ? 1 : 0;

                // Auto-repack ORM if dirty (any source changed)
                if (mat.ORM_Dirty)
                {
                    if (mat.ORMID > 0) { glDeleteTextures(1, &mat.ORMID); mat.ORMID = 0; }
                    mat.ORMID     = PackORM(mat.AOPath, mat.RoughnessPath, mat.MetallicPath,
                                            mat.AOValue, mat.Roughness, mat.Metallic);
                    mat.ORM_Dirty = false;
                }
                ormID = mat.ORMID;
            }
            else if (i < (int)scene.EntityColors.size())
            {
                matCol = scene.EntityColors[i];
            }
        }

        // ---- Bind albedo (unit 0) ----
        glActiveTexture(GL_TEXTURE0);
        if (albedoID > 0) {
            glBindTexture(GL_TEXTURE_2D, albedoID);
            glUniform1i(L.useTexture, 1);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glUniform1i(L.useTexture, 0);
        }

        // ---- Bind normal map (unit 2) ----
        glActiveTexture(GL_TEXTURE2);
        if (normalID > 0) {
            glBindTexture(GL_TEXTURE_2D, normalID);
            glUniform1i(L.useNormal, 1);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glUniform1i(L.useNormal, 0);
        }

        // ---- Bind ORM (unit 3) ----
        glActiveTexture(GL_TEXTURE3);
        if (ormID > 0) {
            glBindTexture(GL_TEXTURE_2D, ormID);
            glUniform1i(L.useORM, 1);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glUniform1i(L.useORM, 0);
        }
        glActiveTexture(GL_TEXTURE0);

        // ---- PBR scalar fallbacks ----
        glUniform1f (L.roughness,   roughness);
        glUniform1f (L.metallic,    metallic);
        glUniform1f (L.aoValue,     aoVal);
        glUniform2fv(L.tiling,      1, &matTiling[0]);
        glUniform1i (L.worldSpaceUV, matWorldUV);

        glUniform3fv(L.materialColor, 1, &matCol[0]);
        glUniformMatrix4fv(L.mvp,   1, GL_FALSE, &mvp[0][0]);
        glUniformMatrix4fv(L.model, 1, GL_FALSE, &model[0][0]);
        scene.MeshRenderers[i].MeshPtr->Draw();
    }
    // Unbind texture units (2D + cubemap)
    for (int u : {0, 1, 2, 3}) {
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE0);
}

// ----------------------------------------------------------------
// DrawColliderGizmos — green wireframe of collision shapes
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

        // World matrix of entity: contains position + rotation + scale from parent chain
        glm::mat4 worldMat = (i < (int)s_CachedWorldMats.size()) ? s_CachedWorldMats[i] : scene.GetEntityWorldMatrix(i);

        // Extract only the rotation part (normalize columns to remove scale)
        glm::mat4 rotMat(1.0f);
        rotMat[0] = glm::vec4(glm::normalize(glm::vec3(worldMat[0])), 0.0f);
        rotMat[1] = glm::vec4(glm::normalize(glm::vec3(worldMat[1])), 0.0f);
        rotMat[2] = glm::vec4(glm::normalize(glm::vec3(worldMat[2])), 0.0f);

        // World position of collider center (offset is also rotated)
        glm::vec3 entityPos = glm::vec3(worldMat[3]);
        glm::vec3 wpos = entityPos + glm::vec3(rotMat * glm::vec4(rb.ColliderOffset, 0.0f));

        // Collider size applying world scale extracted from worldMat
        glm::vec3 worldScale = {
            glm::length(glm::vec3(worldMat[0])),
            glm::length(glm::vec3(worldMat[1])),
            glm::length(glm::vec3(worldMat[2]))
        };
        glm::vec3 sz  = rb.ColliderSize   * worldScale;
        float     rad = rb.ColliderRadius * worldScale.x;
        float     hgt = rb.ColliderHeight * worldScale.y;

        // Helper: transforms local point (relative to collider center) to world-space
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
        // Vertices already in world-space → model = identity
        glm::mat4 mvp = proj * view;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        glDrawArrays(GL_LINES, 0, (GLsizei)verts.size());
    }

    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
}

// ----------------------------------------------------------------
// DrawGizmos — game camera + light gizmos in the editor
// ----------------------------------------------------------------

void Renderer::DrawGizmos(Scene& scene, const glm::mat4& view, const glm::mat4& proj)
{
    static Mesh gizmoSphere = Mesh::CreateGizmoSphere(0.08f);
    static Mesh gizmoLine   = Mesh::CreateGizmoLine(0.7f);

    // Inline line buffer (same approach as DrawColliderGizmos)
    struct LVtx { float x,y,z, nx,ny,nz, r,g,b; };
    static unsigned int lVAO = 0, lVBO = 0;
    if (!lVAO) { glGenVertexArrays(1, &lVAO); glGenBuffers(1, &lVBO); }
    std::vector<LVtx> lines;

    auto addLine = [&](glm::vec3 a, glm::vec3 b, float r, float g, float bv) {
        lines.push_back({a.x,a.y,a.z, 0,1,0, r,g,bv});
        lines.push_back({b.x,b.y,b.z, 0,1,0, r,g,bv});
    };

    const int CIRC = 20;
    auto addCircle = [&](glm::vec3 c, glm::vec3 ax1, glm::vec3 ax2, float rad,
                          float r, float g, float bv) {
        for (int k=0; k<CIRC; k++) {
            float a0 = 2*3.14159f*k/CIRC, a1 = 2*3.14159f*(k+1)/CIRC;
            glm::vec3 p0 = c + ax1*(cosf(a0)*rad) + ax2*(sinf(a0)*rad);
            glm::vec3 p1 = c + ax1*(cosf(a1)*rad) + ax2*(sinf(a1)*rad);
            addLine(p0, p1, r, g, bv);
        }
    };

    glUseProgram(s_GizmoShader);
    unsigned int mvpLoc   = glGetUniformLocation(s_GizmoShader, "u_MVP");
    unsigned int alphaLoc = glGetUniformLocation(s_GizmoShader, "u_Alpha");
    glUniform1f(alphaLoc, 0.75f);
    glDisable(GL_DEPTH_TEST);

    // ---- Camera gizmos (unchanged) ----
    for (size_t i=0; i<scene.GameCameras.size(); i++)
    {
        if (!scene.GameCameras[i].Active) continue;
        glm::vec3 pos = (i < s_CachedWorldMats.size()) ? glm::vec3(s_CachedWorldMats[(int)i][3]) : scene.GetEntityWorldPos((int)i);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
        glm::mat4 mvp   = proj * view * model;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        gizmoSphere.Draw();

        glm::mat4 wm        = (i < (int)s_CachedWorldMats.size()) ? s_CachedWorldMats[(int)i] : scene.GetEntityWorldMatrix((int)i);
        glm::vec3 camRight   =  glm::normalize(glm::vec3(wm[0]));
        glm::vec3 camUp      =  glm::normalize(glm::vec3(wm[1]));
        glm::vec3 camForward = -glm::normalize(glm::vec3(wm[2]));
        glm::mat4 rot(1.0f);
        rot[0] = glm::vec4(camRight,   0.0f);
        rot[1] = glm::vec4(camUp,      0.0f);
        rot[2] = glm::vec4(camForward, 0.0f);
        glm::mat4 lineModel = glm::translate(glm::mat4(1.0f), pos) * rot;
        mvp = proj * view * lineModel;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        gizmoLine.Draw();
    }

    // ---- Light gizmos ----
    for (size_t i=0; i<scene.Lights.size(); i++)
    {
        auto& lc = scene.Lights[i];
        if (!lc.Active || !lc.Enabled) continue;

        glm::mat4 wm  = (i < s_CachedWorldMats.size()) ? s_CachedWorldMats[(int)i] : scene.GetEntityWorldMatrix((int)i);
        glm::vec3 pos = glm::vec3(wm[3]);
        // Use light color clamped to visible range
        float lr = glm::clamp(lc.Color.r, 0.15f, 1.0f);
        float lg = glm::clamp(lc.Color.g, 0.15f, 1.0f);
        float lb = glm::clamp(lc.Color.b, 0.15f, 1.0f);
        // Forward = +X (matches DrawScene: wm * vec4(1,0,0,0) = wm[0])
        glm::vec3 fwd   =  glm::normalize(glm::vec3(wm[0]));
        glm::vec3 up    =  glm::normalize(glm::vec3(wm[1]));
        glm::vec3 right =  glm::normalize(glm::vec3(wm[2]));

        // Small dot at centre for all types
        const float dotR = 0.06f;
        addCircle(pos, glm::vec3(1,0,0), glm::vec3(0,1,0), dotR, lr, lg, lb);
        addCircle(pos, glm::vec3(1,0,0), glm::vec3(0,0,1), dotR, lr, lg, lb);

        if (lc.Type == LightType::Directional)
        {
            // Sun icon: small circle + 8 outward rays
            const float sunR  = 0.18f;
            const float rayL  = 0.22f;
            const float rayGap= 0.07f;
            addCircle(pos, right, up, sunR, lr, lg, lb);
            for (int k=0; k<8; k++) {
                float a = 2*3.14159f*k/8;
                glm::vec3 dir = glm::normalize(right*cosf(a) + up*sinf(a));
                addLine(pos + dir*(sunR+rayGap), pos + dir*(sunR+rayGap+rayL), lr, lg, lb);
            }
            // Main direction line
            addLine(pos, pos + fwd*0.6f, lr, lg, lb);
        }
        else if (lc.Type == LightType::Point)
        {
            // 3 rings (XY, XZ, YZ planes) + range sphere (1 ring per plane)
            float range = lc.Range;
            addCircle(pos, right, up,  range, lr, lg, lb);
            addCircle(pos, right, fwd, range, lr, lg, lb);
            addCircle(pos, up,    fwd, range, lr, lg, lb);
            // Inner bright core rings
            float core = 0.25f;
            addCircle(pos, right, up,  core, lr, lg, lb);
            addCircle(pos, right, fwd, core, lr, lg, lb);
        }
        else if (lc.Type == LightType::Spot)
        {
            // Cone: apex at pos, base circle along fwd at distance range
            float halfOuter = glm::radians(lc.OuterAngle);
            float halfInner = glm::radians(lc.InnerAngle);
            float dist      = lc.Range;
            float outerRad  = tanf(halfOuter) * dist;
            float innerRad  = tanf(halfInner) * dist;
            glm::vec3 centre = pos + fwd * dist;
            // Outer cone circle
            addCircle(centre, right, up, outerRad, lr, lg, lb);
            // Inner cone circle (dashed-ish: same circle, dimmer)
            addCircle(centre, right, up, innerRad, lr*0.6f, lg*0.6f, lb*0.6f);
            // 4 lines from apex to outer rim
            for (int k=0; k<4; k++) {
                float a = 2*3.14159f*k/4;
                glm::vec3 rim = centre + right*(cosf(a)*outerRad) + up*(sinf(a)*outerRad);
                addLine(pos, rim, lr, lg, lb);
            }
            // Forward axis line
            addLine(pos, centre, lr*0.5f, lg*0.5f, lb*0.5f);
        }
        else if (lc.Type == LightType::Area)
        {
            // Rectangle: width along right, height along up
            float hw = lc.Width  * 0.5f;
            float hh = lc.Height * 0.5f;
            glm::vec3 c0 = pos + right*(-hw) + up*(-hh);
            glm::vec3 c1 = pos + right*( hw) + up*(-hh);
            glm::vec3 c2 = pos + right*( hw) + up*( hh);
            glm::vec3 c3 = pos + right*(-hw) + up*( hh);
            addLine(c0, c1, lr, lg, lb);
            addLine(c1, c2, lr, lg, lb);
            addLine(c2, c3, lr, lg, lb);
            addLine(c3, c0, lr, lg, lb);
            // Normal indicator
            glm::vec3 cent = pos;
            addLine(cent, cent + fwd*0.35f, lr, lg, lb);
            // Diagonal cross
            addLine(c0, c2, lr*0.5f, lg*0.5f, lb*0.5f);
            addLine(c1, c3, lr*0.5f, lg*0.5f, lb*0.5f);
        }
    }

    // Flush line buffer
    if (!lines.empty())
    {
        glLineWidth(1.5f);
        glBindVertexArray(lVAO);
        glBindBuffer(GL_ARRAY_BUFFER, lVBO);
        glBufferData(GL_ARRAY_BUFFER, lines.size()*sizeof(LVtx), lines.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(6*sizeof(float)));
        glEnableVertexAttribArray(2);
        glUniform1f(alphaLoc, 0.9f);
        glm::mat4 vp = proj * view;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &vp[0][0]);
        glDrawArrays(GL_LINES, 0, (GLsizei)lines.size());
        glLineWidth(1.0f);
    }

    glEnable(GL_DEPTH_TEST);
}


// ----------------------------------------------------------------
// RenderSceneEditor
// ----------------------------------------------------------------

void Renderer::RenderSceneEditor(Scene& scene, const EditorCamera& camera, int winW, int winH)
{
    CacheWorldMatrices(scene);  // build once, reused by shadow + draw passes

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

    CacheWorldMatrices(scene);  // build once, reused by shadow + draw passes

    // View derivada do world matrix do Transform (so the entity Rotation drives the camera)
    glm::mat4 wm        = s_CachedWorldMats[camIdx];
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
    s_HasShadow        = 0;
    s_HasPointShadow   = 0;
    s_ShadowLightIdx       = -1;
    s_PointShadowLightIdx  = -1;

    // Build the same light-index mapping as DrawScene (only Active+Enabled, up to 8)
    // so we know which shader-light-index each scene light maps to
    struct LightEntry { int sceneIdx; LightType type; };
    LightEntry entries[8];
    int numShaderLights = 0;
    for (int li = 0; li < (int)scene.Lights.size() && numShaderLights < 8; ++li)
    {
        const auto& lc = scene.Lights[li];
        if (!lc.Active || !lc.Enabled) continue;
        entries[numShaderLights] = { li, lc.Type };
        numShaderLights++;
    }

    for (int si = 0; si < numShaderLights; si++)
    {
        int li = entries[si].sceneIdx;
        const auto& lc = scene.Lights[li];

        // ---- Directional / Spot → 2D depth map (first one only) ----
        if (!s_HasShadow && (lc.Type == LightType::Directional || lc.Type == LightType::Spot))
        {
            glm::mat4 wm  = s_CachedWorldMats[li];
            glm::vec3 pos = glm::vec3(wm[3]);
            glm::vec3 fwd = glm::normalize(glm::vec3(wm * glm::vec4(1,0,0,0)));
            glm::vec3 up  = (fabsf(glm::dot(fwd, glm::vec3(0,1,0))) < 0.99f)
                            ? glm::vec3(0,1,0) : glm::vec3(1,0,0);

            glm::mat4 lightView, lightProj;
            if (lc.Type == LightType::Directional)
            {
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
            s_ShadowLightIdx   = si;
            s_HasShadow        = 1;

            glViewport(0, 0, 1024, 1024);
            glBindFramebuffer(GL_FRAMEBUFFER, s_ShadowFBO);
            glClear(GL_DEPTH_BUFFER_BIT);
            glCullFace(GL_FRONT);
            glUseProgram(s_ShadowShader);
            unsigned int loc = glGetUniformLocation(s_ShadowShader, "u_LightMVP");
            for (int i = 0; i < (int)scene.Transforms.size(); ++i)
            {
                if (!scene.MeshRenderers[i].MeshPtr) continue;
                glm::mat4 m = s_ShadowLightSpace * s_CachedWorldMats[i];
                glUniformMatrix4fv(loc, 1, GL_FALSE, &m[0][0]);
                scene.MeshRenderers[i].MeshPtr->Draw();
            }
            glCullFace(GL_BACK);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // ---- Point light → cubemap depth (first one only) ----
        if (!s_HasPointShadow && lc.Type == LightType::Point)
        {
            glm::mat4 wm  = s_CachedWorldMats[li];
            glm::vec3 lpos = glm::vec3(wm[3]);
            float farP = lc.Range > 0.0f ? lc.Range : 25.0f;

            s_PointShadowPos      = lpos;
            s_PointShadowFar      = farP;
            s_PointShadowLightIdx = si;
            s_HasPointShadow      = 1;

            glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farP);
            glm::mat4 shadowViews[6] = {
                glm::lookAt(lpos, lpos + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
                glm::lookAt(lpos, lpos + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
                glm::lookAt(lpos, lpos + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
                glm::lookAt(lpos, lpos + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
                glm::lookAt(lpos, lpos + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
                glm::lookAt(lpos, lpos + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)),
            };
            glm::mat4 shadowMats[6];
            for (int f = 0; f < 6; f++)
                shadowMats[f] = shadowProj * shadowViews[f];

            const int CUBE_SZ = 512;
            glViewport(0, 0, CUBE_SZ, CUBE_SZ);
            glBindFramebuffer(GL_FRAMEBUFFER, s_PointShadowFBO);
            glClear(GL_DEPTH_BUFFER_BIT);

            glUseProgram(s_PointShadowShader);
            unsigned int smLoc  = glGetUniformLocation(s_PointShadowShader, "u_ShadowMatrices[0]");
            unsigned int mdlLoc = glGetUniformLocation(s_PointShadowShader, "u_Model");
            unsigned int lpLoc  = glGetUniformLocation(s_PointShadowShader, "u_LightPos");
            unsigned int fpLoc  = glGetUniformLocation(s_PointShadowShader, "u_FarPlane");

            glUniformMatrix4fv(smLoc, 6, GL_FALSE, &shadowMats[0][0][0]);
            glUniform3fv(lpLoc, 1, &lpos[0]);
            glUniform1f(fpLoc, farP);

            glCullFace(GL_FRONT);   // avoid self-shadow (peter-panning)
            for (int i = 0; i < (int)scene.Transforms.size(); ++i)
            {
                if (!scene.MeshRenderers[i].MeshPtr) continue;
                glm::mat4 mdl = s_CachedWorldMats[i];
                glUniformMatrix4fv(mdlLoc, 1, GL_FALSE, &mdl[0][0]);
                scene.MeshRenderers[i].MeshPtr->Draw();
            }
            glCullFace(GL_BACK);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // Stop if we have both types
        if (s_HasShadow && s_HasPointShadow) break;
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
    // Static arrows by color: red=X, green=Y, blue=Z
    // Created in Init (see below), but we use static local here for
    // simplicity (lazy init on first call after gladLoadGL)
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

    // Rotations to align the arrow (+Y) to each axis
    glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,0,1));
    glm::mat4 rotY = glm::mat4(1.0f); // already points in +Y
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
        // Highlight if this single axis is active, or if it's part of an active plane
        bool highlight = (activeAxis == a.axisId);
        if (a.axisId == 1 && (activeAxis == 4 || activeAxis == 5)) highlight = true; // X in XY(4) or XZ(5)
        if (a.axisId == 2 && (activeAxis == 4 || activeAxis == 6)) highlight = true; // Y in XY(4) or YZ(6)
        if (a.axisId == 3 && (activeAxis == 5 || activeAxis == 6)) highlight = true; // Z in XZ(5) or YZ(6)
        float alpha = highlight ? 1.0f : 0.75f;
        glUniform1f(alphaLoc, alpha);

        glm::mat4 model = T * a.rot * S;
        glm::mat4 mvp   = proj * view * model;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        a.mesh->Draw();
    }

    // Draw plane handles (small colored quads)
    {
        float pOff = EditorGizmo::k_PlaneOff;
        float pSz  = EditorGizmo::k_PlaneSz;

        auto drawPlaneQuad = [&](glm::vec3 axis1, glm::vec3 axis2, float r, float g, float b, int planeId) {
            glm::vec3 center = pos + (axis1 + axis2) * pOff * scale;
            glm::vec3 c0 = center - (axis1 + axis2) * pSz * scale;
            glm::vec3 c1 = center + axis1 * pSz * scale * 2.0f - axis2 * pSz * scale;
            glm::vec3 c2 = center + (axis1 + axis2) * pSz * scale;
            glm::vec3 c3 = center - axis1 * pSz * scale + axis2 * pSz * scale * 2.0f - axis2 * pSz * scale;
            // Actually just draw a simple quad with 2 triangles
            // Vertices: pos + normal + color  (9 floats each)
            glm::vec3 n = glm::normalize(glm::cross(axis1, axis2));
            float a2 = (activeAxis == planeId) ? 0.7f : 0.35f;
            float verts[] = {
                // Triangle 1
                center.x - axis1.x*pSz*scale - axis2.x*pSz*scale,
                center.y - axis1.y*pSz*scale - axis2.y*pSz*scale,
                center.z - axis1.z*pSz*scale - axis2.z*pSz*scale,
                n.x, n.y, n.z,  r*a2, g*a2, b*a2,
                center.x + axis1.x*pSz*scale - axis2.x*pSz*scale,
                center.y + axis1.y*pSz*scale - axis2.y*pSz*scale,
                center.z + axis1.z*pSz*scale - axis2.z*pSz*scale,
                n.x, n.y, n.z,  r*a2, g*a2, b*a2,
                center.x + axis1.x*pSz*scale + axis2.x*pSz*scale,
                center.y + axis1.y*pSz*scale + axis2.y*pSz*scale,
                center.z + axis1.z*pSz*scale + axis2.z*pSz*scale,
                n.x, n.y, n.z,  r*a2, g*a2, b*a2,
                // Triangle 2
                center.x - axis1.x*pSz*scale - axis2.x*pSz*scale,
                center.y - axis1.y*pSz*scale - axis2.y*pSz*scale,
                center.z - axis1.z*pSz*scale - axis2.z*pSz*scale,
                n.x, n.y, n.z,  r*a2, g*a2, b*a2,
                center.x + axis1.x*pSz*scale + axis2.x*pSz*scale,
                center.y + axis1.y*pSz*scale + axis2.y*pSz*scale,
                center.z + axis1.z*pSz*scale + axis2.z*pSz*scale,
                n.x, n.y, n.z,  r*a2, g*a2, b*a2,
                center.x - axis1.x*pSz*scale + axis2.x*pSz*scale,
                center.y - axis1.y*pSz*scale + axis2.y*pSz*scale,
                center.z - axis1.z*pSz*scale + axis2.z*pSz*scale,
                n.x, n.y, n.z,  r*a2, g*a2, b*a2,
            };

            unsigned int vao, vbo;
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(3*sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(6*sizeof(float)));
            glEnableVertexAttribArray(2);

            glUniform1f(alphaLoc, 1.0f);
            glm::mat4 mvp = proj * view;
            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glDeleteBuffers(1, &vbo);
            glDeleteVertexArrays(1, &vao);
        };

        // XY plane = blue quad (Z normal)   activeAxis=4
        drawPlaneQuad(glm::vec3(1,0,0), glm::vec3(0,1,0), 0.3f, 0.3f, 0.9f, 4);
        // XZ plane = green quad (Y normal)  activeAxis=5
        drawPlaneQuad(glm::vec3(1,0,0), glm::vec3(0,0,1), 0.3f, 0.9f, 0.3f, 5);
        // YZ plane = red quad (X normal)    activeAxis=6
        drawPlaneQuad(glm::vec3(0,1,0), glm::vec3(0,0,1), 0.9f, 0.3f, 0.3f, 6);
    }

    glEnable(GL_DEPTH_TEST);
}

// ----------------------------------------------------------------
// DrawRotationGizmo — 3 rings around X/Y/Z
// ----------------------------------------------------------------

void Renderer::DrawRotationGizmo(const glm::vec3& pos, int activeAxis,
                                  const EditorCamera& cam, int winW, int winH)
{
    float aspect = (float)winW / (float)winH;
    glm::mat4 view = cam.GetViewMatrix();
    glm::mat4 proj = cam.GetProjection(aspect);

    float dist  = glm::length(pos - cam.GetPosition());
    float scale = dist * 0.15f * EditorGizmo::k_ArrowLen;

    static constexpr int kSegs = 64;
    static constexpr float kPi = 3.14159265358979f;

    glUseProgram(s_GizmoShader);
    unsigned int mvpLoc   = glGetUniformLocation(s_GizmoShader, "u_MVP");
    unsigned int alphaLoc = glGetUniformLocation(s_GizmoShader, "u_Alpha");

    glDisable(GL_DEPTH_TEST);

    auto drawRing = [&](glm::vec3 ax1, glm::vec3 ax2, float r, float g, float b, int axId) {
        bool hl = (activeAxis == axId);
        float alpha = hl ? 1.0f : 0.6f;
        float lineW = hl ? 5.0f : 3.0f;

        // Build line strip as very thin quads (2 tris per segment)
        std::vector<float> verts;
        float thick = scale * 0.02f;
        for (int i = 0; i <= kSegs; ++i) {
            float a = 2.0f * kPi * (float)i / (float)kSegs;
            glm::vec3 p = pos + (ax1 * cosf(a) + ax2 * sinf(a)) * scale;
            // Use ImGui-style line rendering via GL_LINES instead
            verts.insert(verts.end(), {p.x, p.y, p.z, 0,0,0, r*alpha, g*alpha, b*alpha});
        }

        unsigned int vao, vbo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(6*sizeof(float)));
        glEnableVertexAttribArray(2);

        glUniform1f(alphaLoc, 1.0f);
        glm::mat4 mvp = proj * view;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        glLineWidth(lineW);
        glDrawArrays(GL_LINE_STRIP, 0, kSegs + 1);
        glLineWidth(1.0f);

        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    };

    // X ring (rotate around X): Y-Z plane
    drawRing(glm::vec3(0,1,0), glm::vec3(0,0,1), 0.9f, 0.15f, 0.15f, 1);
    // Y ring (rotate around Y): X-Z plane
    drawRing(glm::vec3(1,0,0), glm::vec3(0,0,1), 0.15f, 0.9f, 0.15f, 2);
    // Z ring (rotate around Z): X-Y plane
    drawRing(glm::vec3(1,0,0), glm::vec3(0,1,0), 0.15f, 0.15f, 0.9f, 3);

    glEnable(GL_DEPTH_TEST);
}

// ----------------------------------------------------------------
// DrawScaleGizmo — 3 axes with cube tips
// ----------------------------------------------------------------

void Renderer::DrawScaleGizmo(const glm::vec3& pos, int activeAxis,
                               const EditorCamera& cam, int winW, int winH)
{
    // Reuse arrow meshes but draw cubes at tips
    static Mesh arrowX = Mesh::CreateGizmoArrow(1.0f, 0.9f, 0.15f, 0.15f);
    static Mesh arrowY = Mesh::CreateGizmoArrow(1.0f, 0.15f, 0.9f, 0.15f);
    static Mesh arrowZ = Mesh::CreateGizmoArrow(1.0f, 0.15f, 0.15f, 0.9f);

    float aspect = (float)winW / (float)winH;
    glm::mat4 view = cam.GetViewMatrix();
    glm::mat4 proj = cam.GetProjection(aspect);

    float dist  = glm::length(pos - cam.GetPosition());
    float scale = dist * 0.15f * EditorGizmo::k_ArrowLen;

    glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(scale));

    glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,0,1));
    glm::mat4 rotY = glm::mat4(1.0f);
    glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), glm::radians( 90.0f), glm::vec3(1,0,0));

    glUseProgram(s_GizmoShader);
    unsigned int mvpLoc   = glGetUniformLocation(s_GizmoShader, "u_MVP");
    unsigned int alphaLoc = glGetUniformLocation(s_GizmoShader, "u_Alpha");

    glDisable(GL_DEPTH_TEST);

    // Draw the axis lines (same as translation but shorter)
    struct AxisDraw { Mesh* mesh; glm::mat4 rot; int axisId; };
    AxisDraw axes[3] = {
        { &arrowX, rotX, 1 },
        { &arrowY, rotY, 2 },
        { &arrowZ, rotZ, 3 },
    };

    for (auto& a : axes)
    {
        bool highlight = (activeAxis == a.axisId);
        float alpha = highlight ? 1.0f : 0.75f;
        glUniform1f(alphaLoc, alpha);

        glm::mat4 model = T * a.rot * S;
        glm::mat4 mvp   = proj * view * model;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        a.mesh->Draw();
    }

    // Draw cube tips at each axis end
    static Mesh cubeTip = Mesh::CreateCube("#FFFFFF");
    float tipScale = scale * 0.08f;

    struct TipDraw { glm::vec3 offset; float r, g, b; int axisId; };
    TipDraw tips[3] = {
        { glm::vec3(scale, 0, 0), 0.9f, 0.15f, 0.15f, 1 },
        { glm::vec3(0, scale, 0), 0.15f, 0.9f, 0.15f, 2 },
        { glm::vec3(0, 0, scale), 0.15f, 0.15f, 0.9f, 3 },
    };

    for (auto& t : tips)
    {
        bool highlight = (activeAxis == t.axisId);
        float alpha = highlight ? 1.0f : 0.75f;
        glUniform1f(alphaLoc, alpha);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos + t.offset)
                        * glm::scale(glm::mat4(1.0f), glm::vec3(tipScale));
        glm::mat4 mvp = proj * view * model;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        cubeTip.Draw();
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