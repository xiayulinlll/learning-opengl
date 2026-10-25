#version 330 core

// ============================================================
//  天空盒片元着色器
// ============================================================

in vec3 vTexCoords;

out vec4 FragColor;

uniform samplerCube uSkybox;

void main()
{
    vec3 color = texture(uSkybox, vTexCoords).rgb;
    // 色调映射 + Gamma 校正，保持与场景一致
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
