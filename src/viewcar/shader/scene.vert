// ============================================================
//  scene.vert - 主场景顶点着色器
//  支持 GPU 实例化：普通物体走 uModel，实例物体走实例矩阵 aInstanceMatrix
// ============================================================
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

// 实例属性（location 2..5 为 mat4，6 为颜色）
layout (location = 2) in vec4 aInstanceRow0;
layout (location = 3) in vec4 aInstanceRow1;
layout (location = 4) in vec4 aInstanceRow2;
layout (location = 5) in vec4 aInstanceRow3;
layout (location = 6) in vec3 aInstanceColor;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

uniform bool uUseInstancing;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vInstanceColor;

void main()
{
    mat4 model = uModel;
    if (uUseInstancing)
    {
        // 列主序：把 4 个行向量组装回矩阵（GLSL 中 mat4 构造函数按列填充，
        // 因此这里按列传入 row0..row3 即可得到正确矩阵）
        model = mat4(aInstanceRow0, aInstanceRow1, aInstanceRow2, aInstanceRow3);
    }

    vec4 worldPos = model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vInstanceColor = aInstanceColor;

    // 实例矩阵只含平移/旋转/缩放，直接取其 3x3 部分变换法线即可
    mat3 normalMat = uNormalMatrix;
    if (uUseInstancing)
        normalMat = mat3(model);

    vNormal = normalize(normalMat * aNormal);

    gl_Position = uProjection * uView * worldPos;
}
