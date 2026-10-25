// ============================================================
//  occupancy.vert - BEV 占据栅格合成顶点着色器
//
//  BEV 图层是「一个个铺在地面上的薄板」，因此顶点格式与
//  line.vert 一致（位置 + 颜色），直接输出到裁剪空间。
//  之所以不复用 scene.vert，是因为 BEV 不需要光照：
//  它的观感来自「颜色即语义」（红=危险、绿=可行），
//  任何光照都会把语义色污染掉。
// ============================================================
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord; // 用于栅格纹理采样

uniform mat4 uViewProj;
uniform float uDepthOffset; // 沿视线方向的深度偏移，抑制同层板之间 z-fighting

out vec2 vTexCoord;

void main()
{
    vTexCoord = aTexCoord;

    vec4 clip = uViewProj * vec4(aPos, 1.0);
    clip.z -= uDepthOffset * clip.w;

    gl_Position = clip;
}
