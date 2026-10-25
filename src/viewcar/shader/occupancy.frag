// ============================================================
//  occupancy.frag - BEV 占据栅格 / 可行驶区域着色
//
//  特斯拉 FSD 的可视化里，颜色本身就是信息：
//    · 占据栅格：越危险越红（车辆/行人/卡车权重不同）
//    · 可行驶区域：青色半透明填充，表示"这里可以走"
//  本着色器把这两路 R32F 浮点纹理合成为最终颜色。
//
//  关键设计：colormap 是自己写的「感知风险色阶」，
//  而不是常见的 jet/rainbow ——彩虹色阶在感知界面上有
//  两个致命问题：(1) 非单调，中间蓝色看起来"很安全"实则是中等风险；
//  (2) 对色盲不友好。这里用「透明→青→黄→橙→红」的单调递增色阶，
//  亮度与"危险程度"单调相关，任何色觉条件下都能读出风险顺序。
// ============================================================
#version 330 core

in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uOccupancy;   // R32F：占据概率 0~1
uniform sampler2D uDrivable;    // R32F：是否可行驶 0/1

uniform float uOccupancyOpacity;
uniform float uDrivableOpacity;
uniform bool  uShowOccupancy;
uniform bool  uShowDrivable;

uniform vec2  uGridSize;        // 栅格格数，用于画格线
uniform bool  uShowCellEdges;   // 是否勾勒格子边界
// 感知风险色阶：t = 0（空闲）→ 1（确定被占据）
//  0.00 完全透明（不画，露出底下的路面）
//  0.15 深青（弱占据，如远距离预测轨迹）
//  0.40 亮青绿（中低）
//  0.65 琥珀黄（中高）
//  0.85 橙红（高）
//  1.00 饱和红（确定障碍）
vec3 riskColormap(float t)
{
    t = clamp(t, 0.0, 1.0);

    if (t < 0.15)
        return mix(vec3(0.06, 0.30, 0.34), vec3(0.10, 0.62, 0.66), t / 0.15);
    if (t < 0.40)
        return mix(vec3(0.10, 0.62, 0.66), vec3(0.24, 0.86, 0.72),
                   (t - 0.15) / 0.25);
    if (t < 0.65)
        return mix(vec3(0.24, 0.86, 0.72), vec3(0.96, 0.76, 0.22),
                   (t - 0.40) / 0.25);
    if (t < 0.85)
        return mix(vec3(0.96, 0.76, 0.22), vec3(0.98, 0.45, 0.14),
                   (t - 0.65) / 0.20);

    return mix(vec3(0.98, 0.45, 0.14), vec3(0.95, 0.13, 0.11),
               (t - 0.85) / 0.15);
}

// 返回 (rgb, alpha)：透明度随占据值递增 —— 弱占据几乎透明，
//  强占据完全不透明。这样 BEV 底下的道路依然可见，
//  同时危险目标非常醒目。
vec4 riskColor(float t, float alphaMask)
{
    vec3 c = riskColormap(t);
    float a = alphaMask * smoothstep(0.03, 0.30, t);
    return vec4(c, a);
}

void main()
{
    // 这些值来自 uniform / texture，不能声明成 const ——
    //  NVIDIA 的 GLSL 编译器会报「non constant expression in initialization」。
    float occ = texture(uOccupancy, vTexCoord).r;
    float drv = texture(uDrivable, vTexCoord).r;

    vec3 color = vec3(0.0);
    float alpha = 0.0;

    // ---- 1. 可行驶区域：青色半透明底 ----
    if (uShowDrivable && drv > 0.5)
    {
        // 用略微偏暗的青色，避免大面积纯色显得「死板」，
        //  也给上层的风险热力图留出对比空间。
        color = vec3(0.10, 0.34, 0.42);
        alpha = uDrivableOpacity;
    }

    // ---- 2. 占据栅格：风险热力图叠在可行驶区域之上 ----
    if (uShowOccupancy && occ > 0.02)
    {
        vec4 rc = riskColor(occ, uOccupancyOpacity);

        // 标准「source over」合成：源色按自己的 alpha 覆盖到目标上
        color = mix(color, rc.rgb, rc.a);
        alpha = alpha + rc.a * (1.0 - alpha);
    }

    // ---- 3. 格子边界：让「栅格」这一离散结构可见 ----
    //  只在占据区域内画格线，否则整个 BEV 会变成一张网格布，
    //  干扰对目标形状的判断。
    if (uShowCellEdges && occ > 0.02)
    {
        vec2 cellUV = fract(vTexCoord * uGridSize);
        float edge = min(min(cellUV.x, 1.0 - cellUV.x),
                         min(cellUV.y, 1.0 - cellUV.y));

        // 用 fwidth 做屏幕空间等宽线：无论视口缩放到多大，
        //  格线始终是 1~2 像素，不会随缩放变粗变细。
        float lineW = fwidth(edge) * 1.2;
        float lineMask = 1.0 - smoothstep(0.0, lineW, edge);

        color = mix(color, vec3(0.85, 0.90, 0.95), lineMask * 0.22);
    }

    if (alpha < 0.004)
        discard; // 完全透明的格子直接丢弃，省掉后面的混合开销

    FragColor = vec4(color, alpha);
}
