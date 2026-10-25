#version 330 core

// ============================================================
//  Cook-Torrance PBR 片元着色器
//  包含：GGX 法线分布、Smith 几何函数、Fresnel 近似
//        平行光 + 点光源 + 聚光灯、阴影贴图 PCF、法线贴图
// ============================================================

const float PI = 3.14159265359;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;
in mat3 vTBN;

out vec4 FragColor;

// ---------------- 材质 ----------------
struct Material
{
    vec3 albedo;
    float metallic;
    float roughness;
    float ao;

    sampler2D albedoMap;
    sampler2D normalMap;
    sampler2D metallicMap;
    sampler2D roughnessMap;
    sampler2D aoMap;

    int hasAlbedoMap;
    int hasNormalMap;
    int hasMetallicMap;
    int hasRoughnessMap;
    int hasAoMap;
};

// ---------------- 光源 ----------------
struct DirectionalLight
{
    vec3 direction;
    vec3 color;
    float intensity;
    int castShadow;
    float shadowBias;
};

struct PointLight
{
    vec3 position;
    vec3 color;
    float intensity;
    float constant;
    float linear;
    float quadratic;
    int enabled;
};

struct SpotLight
{
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float cutOff;      // cos(内角)
    float outerCutOff; // cos(外角)
    float constant;
    float linear;
    float quadratic;
    int enabled;
};

#define MAX_POINT_LIGHTS 4

uniform Material uMaterial;
uniform DirectionalLight uDirLight;
uniform PointLight uPointLights[MAX_POINT_LIGHTS];
uniform int uPointLightCount;
uniform SpotLight uSpotLight;

uniform vec3 uCameraPos;

// 阴影
uniform sampler2D uShadowMap;
uniform mat4 uLightSpaceMatrix;

// ---------------- 材质采样 ----------------
vec3 getNormal()
{
    vec3 N = normalize(vNormal);
    if (uMaterial.hasNormalMap == 1)
    {
        // 采样法线贴图并映射到 [-1, 1]
        vec3 tangentNormal = texture(uMaterial.normalMap, vTexCoords).rgb * 2.0 - 1.0;
        N = normalize(vTBN * tangentNormal);
    }
    return N;
}

vec3 getAlbedo()
{
    vec3 albedo = uMaterial.albedo;
    if (uMaterial.hasAlbedoMap == 1)
        albedo *= texture(uMaterial.albedoMap, vTexCoords).rgb;
    return albedo;
}

float getMetallic()
{
    if (uMaterial.hasMetallicMap == 1)
        return texture(uMaterial.metallicMap, vTexCoords).b; // glTF: metallic 在 B 通道
    return uMaterial.metallic;
}

float getRoughness()
{
    if (uMaterial.hasRoughnessMap == 1)
        return texture(uMaterial.roughnessMap, vTexCoords).g; // glTF: roughness 在 G 通道
    return uMaterial.roughness;
}

float getAO()
{
    if (uMaterial.hasAoMap == 1)
        return texture(uMaterial.aoMap, vTexCoords).r;
    return uMaterial.ao;
}

// ---------------- PBR 核心 ----------------
// GGX/Trowbridge-Reitz 法线分布函数
float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.0000001);
}

// 几何衰减（Schlick-GGX + Smith）
float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggxV = geometrySchlickGGX(NdotV, roughness);
    float ggxL = geometrySchlickGGX(NdotL, roughness);

    return ggxV * ggxL;
}

// Fresnel-Schlick 近似
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 计算单个光源贡献
vec3 calculateLightContribution(vec3 L, vec3 radiance, vec3 N, vec3 V,
                                 vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    // Cook-Torrance BRDF
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    // 能量守恒：漫反射 = 1 - 镜面反射
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// 阴影计算（3x3 PCF 软化边缘）
float calculateShadow(vec3 N, vec3 L)
{
    if (uDirLight.castShadow == 0)
        return 0.0;

    vec4 fragPosLightSpace = uLightSpaceMatrix * vec4(vWorldPos, 1.0);

    // 透视除法，转换为 [0, 1] 范围
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    // 超出阴影贴图范围则视为无阴影
    if (projCoords.z > 1.0)
        return 0.0;

    // 根据法线与光方向夹角动态调整偏移，缓解阴影痤疮
    float bias = max(uDirLight.shadowBias * 10.0 * (1.0 - dot(N, L)), uDirLight.shadowBias * 0.1);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(uShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (projCoords.z - bias) > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return shadow;
}

void main()
{
    vec3 N = getNormal();
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 albedo = getAlbedo();
    float metallic = getMetallic();
    float roughness = clamp(getRoughness(), 0.04, 1.0);
    float ao = getAO();

    // 电介质基础反射率约 0.04，金属使用自身反照率
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    // ---------- 平行光 ----------
    if (uDirLight.intensity > 0.0)
    {
        vec3 L = normalize(-uDirLight.direction);
        vec3 radiance = uDirLight.color * uDirLight.intensity;
        float shadow = calculateShadow(N, L);
        Lo += (1.0 - shadow) * calculateLightContribution(L, radiance, N, V, albedo, metallic, roughness, F0);
    }

    // ---------- 点光源 ----------
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i)
    {
        if (i >= uPointLightCount || uPointLights[i].enabled == 0)
            continue;

        vec3 L = uPointLights[i].position - vWorldPos;
        float distance = length(L);
        L = normalize(L);

        float attenuation = 1.0 / (uPointLights[i].constant +
                                   uPointLights[i].linear * distance +
                                   uPointLights[i].quadratic * distance * distance);
        vec3 radiance = uPointLights[i].color * uPointLights[i].intensity * attenuation;

        Lo += calculateLightContribution(L, radiance, N, V, albedo, metallic, roughness, F0);
    }

    // ---------- 聚光灯 ----------
    if (uSpotLight.enabled == 1)
    {
        vec3 L = uSpotLight.position - vWorldPos;
        float distance = length(L);
        L = normalize(L);

        float theta = dot(L, normalize(-uSpotLight.direction));
        // 内圆锥到外圆锥做平滑过渡
        float epsilon = uSpotLight.cutOff - uSpotLight.outerCutOff;
        float spotFactor = clamp((theta - uSpotLight.outerCutOff) / max(epsilon, 0.0001), 0.0, 1.0);

        if (spotFactor > 0.0)
        {
            float attenuation = 1.0 / (uSpotLight.constant +
                                       uSpotLight.linear * distance +
                                       uSpotLight.quadratic * distance * distance);
            vec3 radiance = uSpotLight.color * uSpotLight.intensity * attenuation * spotFactor;
            Lo += calculateLightContribution(L, radiance, N, V, albedo, metallic, roughness, F0);
        }
    }

    // ---------- 环境光（半球近似） ----------
    vec3 ambient = vec3(0.03) * albedo * ao;

    vec3 color = ambient + Lo;

    // Reinhard 色调映射，避免高光过曝
    color = color / (color + vec3(1.0));
    // Gamma 校正（线性空间 -> sRGB）
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}

