// ============================================================
//  scene.frag - 主场景片段着色器
//
//  车载可视化场景的光照需求与通用 PBR 场景不同：
//    · 主体是「车身与感知目标」，需要清晰可辨的固有色
//    · 强调可读性而非照片级真实感
//  因此这里用「半兰伯特 + 菲涅尔边缘光 + 半球环境光」的混合模型：
//    - 半兰伯特让背光面也保留细节，避免目标框内一片黑
//    - 菲涅尔边缘光勾勒轮廓，车身/卡车立体感更强
// ============================================================
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vInstanceColor;

uniform vec3 uBaseColor;      // 物体固有色
uniform vec3 uLightDir;       // 主光方向（指向光源）
uniform vec3 uLightColor;
uniform float uLightIntensity;

uniform vec3 uAmbientSky;     // 半球环境光：天顶色
uniform vec3 uAmbientGround;  // 半球环境光：地面色
uniform float uAmbientIntensity;

uniform vec3 uCameraPos;
uniform vec3 uFogColor;
uniform float uFogDensity;

uniform float uMetallic;
uniform float uRoughness;
uniform float uRimStrength;   // 边缘光强度
uniform float uOpacity;
uniform float uEmissive;      // 自发光强度（车灯 / 路灯 / 窗带）

uniform bool uUseInstancing;

out vec4 FragColor;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 L = normalize(uLightDir);

    vec3 albedo = uBaseColor;
    if (uUseInstancing)
        albedo = vInstanceColor;

    // ---- 半兰伯特漫反射 ----
    // 把 N·L 从 [-1,1] 映射到 [0,1]，背光面不会全黑，保证目标轮廓可读
    float ndl = dot(N, L);
    float halfLambert = ndl * 0.5 + 0.5;
    halfLambert = halfLambert * halfLambert; // 稍微加强对比

    vec3 diffuse = albedo * uLightColor * uLightIntensity * halfLambert;

    // ---- 高光（Blinn-Phong，用粗糙度控制光泽）----
    vec3 H = normalize(L + V);
    float specPower = mix(128.0, 4.0, clamp(uRoughness, 0.0, 1.0));
    float spec = pow(max(dot(N, H), 0.0), specPower);
    // 金属度越高，高光带物体固有色
    vec3 specColor = mix(vec3(1.0), albedo, uMetallic);
    diffuse += specColor * uLightColor * uLightIntensity * spec * (1.0 - uRoughness) * 0.6;

    // ---- 半球环境光 ----
    // 法线朝上取天顶色，朝下取地面色，模拟「天空亮、地面暗」的自然环境光
    float up = N.y * 0.5 + 0.5;
    vec3 ambient = mix(uAmbientGround, uAmbientSky, up) * albedo * uAmbientIntensity;

    // ---- 菲涅尔边缘光：勾勒物体轮廓 ----
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    vec3 rim = uLightColor * fresnel * uRimStrength;

    vec3 color = diffuse + ambient + rim;

    // ---- 自发光：车灯 / 路灯灯头 / 楼体窗带 ----
    // 直接加固有色，并随雾一起衰减（远处灯光不该穿透浓雾）
    if (uEmissive > 0.0)
        color += albedo * uEmissive;

    // ---- 指数雾：远处目标淡出，既符合视觉习惯又隐藏场景边界 ----
    float dist = length(uCameraPos - vWorldPos);
    float fogFactor = 1.0 - exp(-dist * uFogDensity);
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    color = mix(color, uFogColor, fogFactor);

    // ---- Gamma 校正（线性空间 -> sRGB）----
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, uOpacity);
}
