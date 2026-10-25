#version 330 core

// ============================================================
//  PBR 顶点着色器
//  输出世界空间坐标、法线、UV、切线空间基向量
// ============================================================

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoords;
out mat3 vTBN;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    vNormal = normalize(uNormalMatrix * aNormal);
    vTexCoords = aTexCoords;

    // 构建切线空间矩阵（用于法线贴图）
    vec3 T = normalize(uNormalMatrix * aTangent);
    vec3 B = normalize(uNormalMatrix * aBitangent);
    vec3 N = vNormal;
    vTBN = mat3(T, B, N);

    gl_Position = uProjection * uView * worldPos;
}
