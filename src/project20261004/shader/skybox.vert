#version 330 core

// ============================================================
//  天空盒顶点着色器
//  技巧：让天空盒始终跟随相机（去掉视图矩阵的平移部分）
// ============================================================

layout(location = 0) in vec3 aPos;

out vec3 vTexCoords;

uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    vTexCoords = aPos;
    // 使用 mat4(mat3(uView)) 去掉平移，使天空盒锁定在相机位置
    vec4 pos = uProjection * mat4(mat3(uView)) * vec4(aPos, 1.0);
    // 把 z 强制设为 w，保证天空盒永远处于远裁剪面
    gl_Position = pos.xyww;
}
