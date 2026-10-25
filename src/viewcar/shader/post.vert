// ============================================================
//  post.vert - 全屏四边形顶点着色器（后处理）
//  直接输出 NDC 坐标，无需矩阵变换
// ============================================================
#version 330 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUv;

out vec2 vUv;

void main()
{
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
