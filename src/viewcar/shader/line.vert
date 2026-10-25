// ============================================================
//  line.vert - 调试线段顶点着色器
//  顶点携带位置与颜色，直接输出到裁剪空间
// ============================================================
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 uViewProj;
uniform float uDepthOffset; // 沿视线方向的小偏移，避免线与面 z-fighting

out vec3 vColor;

void main()
{
    vColor = aColor;

    vec4 clip = uViewProj * vec4(aPos, 1.0);

    // 在 NDC 空间把深度往相机方向推一点，抑制 coplanar z-fighting
    // （车道线贴在路面上时尤其明显）
    clip.z -= uDepthOffset * clip.w;

    gl_Position = clip;
}
