// ============================================================
//  post.frag - 后处理
//  车载 HMI 的后处理目标不是「好看」，而是「提高可读性」：
//    · 暗角（vignette）：把注意力引导到画面中心 —— 前向感知区域
//    · 轻微对比度/饱和度增强：让目标框的彩色叠加在明亮日光下依然醒目
//    · 若做了超采样缩放，这里负责双线性上采样
// ============================================================
#version 330 core

in vec2 vUv;

uniform sampler2D uScene;
uniform float uVignetteStrength;
uniform float uVignetteRadius;
uniform float uContrast;
uniform float uSaturation;

out vec4 FragColor;

void main()
{
    vec3 color = texture(uScene, vUv).rgb;

    // ---- 饱和度调整：在灰度与彩色之间插值 ----
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, uSaturation);

    // ---- 对比度调整：围绕中灰 0.5 拉伸 ----
    color = (color - 0.5) * uContrast + 0.5;

    // ---- 暗角：距离画面中心越远越暗 ----
    // 用 uVignetteRadius 控制「保持全亮的中心区域」大小
    vec2 centered = vUv - vec2(0.5);
    float dist = length(centered) * 1.41421356; // 归一化到 [0,1]
    float vignette = 1.0 - smoothstep(uVignetteRadius, 1.0, dist) * uVignetteStrength;
    color *= vignette;

    // 防止过曝并做 gamma（场景着色器已输出 sRGB，这里仅做钳制）
    color = clamp(color, 0.0, 1.0);

    FragColor = vec4(color, 1.0);
}
