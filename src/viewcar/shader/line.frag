// ============================================================
//  line.frag - 调试线段片段着色器
//  直接输出顶点色，可选 gamma 校正（与场景 shader 的输出空间保持一致）
// ============================================================
#version 330 core

in vec3 vColor;

uniform bool uGammaCorrect; // 场景已做 gamma 时，线段也要跟上，否则颜色不匹配
uniform float uAlpha;

out vec4 FragColor;

void main()
{
    vec3 color = vColor;

    if (uGammaCorrect)
        color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, uAlpha);
}
