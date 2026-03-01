#version 460 core

in vec3 vColor;
in vec3 vNormal;
in vec3 vFragPos;

out vec4 FragColor;

uniform vec3 u_LightPos;

void main()
{
    vec3 lightDir = normalize(u_LightPos - vFragPos);
    float diff = max(dot(normalize(vNormal), lightDir), 0.2);
    vec3 result = vColor * diff;
    FragColor = vec4(result, 1.0);
}