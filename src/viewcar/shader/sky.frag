// ============================================================
//  sky.frag - 程序化天空
//  用视线方向做渐变：天顶偏蓝、地平线偏白，省去加载 cubemap 贴图
// ============================================================
#version 330 core

in vec3 vWorldPos;

uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyBottom;

out vec4 FragColor;

void main()
{
    // 从世界坐标推算地平线高度（天空盒中心在相机位置）
    vec3 dir = normalize(vWorldPos);

    float h = dir.y;

    vec3 color;
    if (h > 0.0)
    {
        // 天顶 -> 地平线插值；用 pow 让渐变更自然地集中在地平线附近
        float t = pow(clamp(h, 0.0, 1.0), 0.55);
        color = mix(uSkyHorizon, uSkyTop, t);
    }
    else
    {
        // 地平线以下：轻微变暗，模拟地面大气
        float t = clamp(-h * 3.0, 0.0, 1.0);
        color = mix(uSkyHorizon, uSkyBottom, t);
    }

    FragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
