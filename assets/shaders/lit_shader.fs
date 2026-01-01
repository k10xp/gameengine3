#version 330 core
out vec4 FragColor;

in vec3 vWorldPos;
in vec3 vNormal;

uniform vec3 uLightPos;
uniform vec3 uViewPos;

uniform vec3 uObjectColor;
uniform vec3 uLightColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vWorldPos);

    // ambient
    float ambientStrength = 0.15;
    vec3 ambient = ambientStrength * uLightColor;

    // diffuse
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * uLightColor;

    // specular (Blinn-Phong)
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 64.0);
    float specStrength = 0.6;
    vec3 specular = specStrength * spec * uLightColor;

    vec3 color = (ambient + diffuse + specular) * uObjectColor;
    FragColor = vec4(color, 1.0);
}
