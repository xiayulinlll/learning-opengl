#include "renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vc
{

namespace
{
// 本项目自己的 shader 目录（相对于工程根目录）
const char *kShaderDir = "src/viewcar/shader/";

// 目标标签引线的长度（从包围盒顶部向上）
constexpr float kLabelLeaderHeight = 2.2f;
} // namespace

Renderer::~Renderer()
{
}

bool Renderer::init()
{
  if (!initShaders())
    return false;

  if (!initGeometry())
    return false;

  if (!initPostProcess())
    return false;

  m_lines.init();
  m_bevLines.init();

  if (!m_bev.init())
    return false;

  logInfo("渲染器初始化完成");
  return true;
}

bool Renderer::initShaders()
{
  const std::string dir = kShaderDir;

  if (!m_sceneShader.loadFromFiles(dir + "scene.vert", dir + "scene.frag"))
    return false;
  if (!m_lineShader.loadFromFiles(dir + "line.vert", dir + "line.frag"))
    return false;
  if (!m_groundShader.loadFromFiles(dir + "ground.vert", dir + "scene.frag"))
    return false;
  if (!m_skyShader.loadFromFiles(dir + "sky.vert", dir + "sky.frag"))
    return false;

  return true;
}

bool Renderer::initGeometry()
{
  if (!m_cube.createBox())
  {
    logError("创建立方体网格失败");
    return false;
  }
  m_cube.setupInstancing(); // 目标用实例化绘制

  if (!m_ground.createPlane())
  {
    logError("创建平面网格失败");
    return false;
  }

  if (!m_skyCube.createBox())
  {
    logError("创建天空盒网格失败");
    return false;
  }

  if (!m_wheel.createCylinder(20))
  {
    logError("创建车轮网格失败");
    return false;
  }

  // 车型化部件 + 街景道具网格库（程序化生成，无外部资源）
  if (!m_props.init())
  {
    logError("创建道具网格库失败");
    return false;
  }

  // ---- 占据栅格纹理用的板面（位置 3 + UV 2）----
  //  BEV 的两张栅格纹理（占据 / 可行驶）贴在这块板上。
  //  顶点坐标直接用「自车坐标系下的米数」：
  //    x ∈ [-rangeSide, +rangeSide]，z ∈ [-rangeBack, +rangeForward]
  //  于是模型矩阵可以保持单位矩阵，省一次矩阵乘法，
  //  也让 UV 与栅格采样范围天然对应。
  {
    const float vertices[] = {
        // 位置(x, 0, z)                 UV(u, v)
        -1.0f, 0.0f, -1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 1.0f, 1.0f,
        -1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
    };

    glGenVertexArrays(1, &m_occQuadVao);
    glGenBuffers(1, &m_occQuadVbo);

    glBindVertexArray(m_occQuadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_occQuadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));

    glBindVertexArray(0);
  }

  return true;
}

bool Renderer::initPostProcess()
{
  // 全屏四边形：位置(2) + UV(2)
  const float quadVertices[] = {
      // 位置         UV
      -1.0f, 1.0f, 0.0f, 1.0f,
      -1.0f, -1.0f, 0.0f, 0.0f,
      1.0f, -1.0f, 1.0f, 0.0f,

      -1.0f, 1.0f, 0.0f, 1.0f,
      1.0f, -1.0f, 1.0f, 0.0f,
      1.0f, 1.0f, 1.0f, 1.0f};

  glGenVertexArrays(1, &m_quadVao);
  glGenBuffers(1, &m_quadVbo);

  glBindVertexArray(m_quadVao);
  glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void *>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));
  glBindVertexArray(0);

  const std::string dir = kShaderDir;
  if (!m_postShader.loadFromFiles(dir + "post.vert", dir + "post.frag"))
  {
    logError("后处理着色器加载失败");
    return false;
  }

  createFramebuffer(m_width, m_height);
  return true;
}

void Renderer::createFramebuffer(int width, int height)
{
  // 渲染分辨率 = 窗口尺寸 * renderScale，支持超采样（>1）与性能降采样（<1）
  const int rw = std::max(1, static_cast<int>(static_cast<float>(width) * m_renderScale));
  const int rh = std::max(1, static_cast<int>(static_cast<float>(height) * m_renderScale));

  if (m_fbo)
  {
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_colorTex);
    glDeleteRenderbuffers(1, &m_rbo);
    m_fbo = m_colorTex = m_rbo = 0;
  }

  glGenFramebuffers(1, &m_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

  // 颜色附件
  glGenTextures(1, &m_colorTex);
  glBindTexture(GL_TEXTURE_2D, m_colorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);

  // 深度/模板附件（深度测试必需）
  glGenRenderbuffers(1, &m_rbo);
  glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, rw, rh);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_rbo);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    logError("后处理帧缓冲不完整");

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::resize(int width, int height)
{
  if (width <= 0 || height <= 0)
    return;

  m_width = width;
  m_height = height;
  createFramebuffer(m_width, m_height);
}

void Renderer::destroy()
{
  m_sceneShader.destroy();
  m_lineShader.destroy();
  m_groundShader.destroy();
  m_skyShader.destroy();
  m_postShader.destroy();

  m_cube.destroy();
  m_ground.destroy();
  m_skyCube.destroy();
  m_wheel.destroy();
  m_lines.destroy();
  m_props.destroy();

  m_bev.destroy();
  m_bevLines.destroy();

  if (m_occQuadVbo)
    glDeleteBuffers(1, &m_occQuadVbo);
  if (m_occQuadVao)
    glDeleteVertexArrays(1, &m_occQuadVao);
  m_occQuadVbo = m_occQuadVao = 0;

  if (m_fbo)
    glDeleteFramebuffers(1, &m_fbo);
  if (m_colorTex)
    glDeleteTextures(1, &m_colorTex);
  if (m_rbo)
    glDeleteRenderbuffers(1, &m_rbo);
  if (m_quadVbo)
    glDeleteBuffers(1, &m_quadVbo);
  if (m_quadVao)
    glDeleteVertexArrays(1, &m_quadVao);

  m_fbo = m_colorTex = m_rbo = m_quadVao = m_quadVbo = 0;
}

// ============================================================
//  setSceneCommonUniforms
// ============================================================

void Renderer::setSceneCommonUniforms(const Camera &camera, const VisualSettings &s)
{
  m_sceneShader.setVec3("uLightDir", glm::normalize(s.lightDir));
  m_sceneShader.setVec3("uLightColor", s.lightColor);
  m_sceneShader.setFloat("uLightIntensity", s.lightIntensity);

  m_sceneShader.setVec3("uAmbientSky", s.ambientSky);
  m_sceneShader.setVec3("uAmbientGround", s.ambientGround);
  m_sceneShader.setFloat("uAmbientIntensity", s.ambientIntensity);

  m_sceneShader.setVec3("uCameraPos", camera.position());
  m_sceneShader.setVec3("uFogColor", s.fogColor);
  m_sceneShader.setFloat("uFogDensity", s.enableFog ? s.fogDensity : 0.0f);
}

void Renderer::drawSolid(const Camera &camera, const VisualSettings &s,
                         const glm::mat4 &model, const glm::vec3 &color,
                         float metallic, float roughness, float rim)
{
  (void)camera;
  (void)s;

  m_sceneShader.setBool("uUseInstancing", false);
  m_sceneShader.setMat4("uModel", model);
  m_sceneShader.setMat3("uNormalMatrix",
                        glm::mat3(glm::transpose(glm::inverse(model))));
  m_sceneShader.setVec3("uBaseColor", color);
  m_sceneShader.setFloat("uMetallic", metallic);
  m_sceneShader.setFloat("uRoughness", roughness);
  m_sceneShader.setFloat("uRimStrength", rim);
  m_sceneShader.setFloat("uOpacity", 1.0f);
  m_sceneShader.setFloat("uEmissive", 0.0f);

  m_cube.draw();
  m_stats.drawCalls++;
  m_stats.triangleCount += static_cast<int>(m_cube.triangleCount());
}

// ============================================================
//  复合模型批量绘制
//
//  一辆车由十几个部件组成，如果每个部件单独 draw 会产生大量 draw call。
//  这里的做法是：把「同一个网格 + 相同材质」的部件累积成一批，
//  最后每批一次 drawInstanced 提交。轿车 5 类部件 => 5 次 draw call，
//  即使场景里有 24 个目标，draw call 也只有 20 余次。
// ============================================================

void Renderer::pushPart(std::vector<InstanceBatch> &batches, ShapeMesh *mesh,
                        const glm::mat4 &model, const glm::vec3 &color,
                        float metallic, float roughness, float rim,
                        float emissive)
{
  if (!mesh)
    return;

  // 找一批网格与材质都一致的批次（材质量化到 1/1000，避免浮点比较问题）
  const int qm = static_cast<int>(metallic * 1000.0f);
  const int qr = static_cast<int>(roughness * 1000.0f);
  const int qrim = static_cast<int>(rim * 1000.0f);

  for (auto &b : batches)
  {
    if (b.mesh != mesh)
      continue;
    if (static_cast<int>(b.metallic * 1000.0f) != qm ||
        static_cast<int>(b.roughness * 1000.0f) != qr ||
        static_cast<int>(b.rim * 1000.0f) != qrim)
      continue;

    // 颜色差异较大的不合并（想保留「同色车合并」这一最常见情形）
    const glm::vec3 dc = glm::abs(b.color - color);
    if (dc.x > 0.004f || dc.y > 0.004f || dc.z > 0.004f)
      continue;

    b.transforms.push_back(model);
    return;
  }

  InstanceBatch nb;
  nb.mesh = mesh;
  nb.color = color;
  nb.metallic = metallic;
  nb.roughness = roughness;
  nb.rim = rim;
  nb.emissive = emissive;
  nb.transforms.push_back(model);
  batches.push_back(nb);
}

void Renderer::flushBatches(const std::vector<InstanceBatch> &batches)
{
  // 颜色列表在批内是常量（同批同色），这里复用同一个数组即可
  for (const auto &b : batches)
  {
    if (b.transforms.empty())
      continue;

    const std::vector<glm::vec3> colors(b.transforms.size(), b.color);

    m_sceneShader.setBool("uUseInstancing", true);
    m_sceneShader.setFloat("uMetallic", b.metallic);
    m_sceneShader.setFloat("uRoughness", b.roughness);
    m_sceneShader.setFloat("uRimStrength", b.rim);
    m_sceneShader.setFloat("uOpacity", 1.0f);
    m_sceneShader.setFloat("uEmissive", b.emissive);

    b.mesh->uploadInstances(b.transforms, colors);
    b.mesh->drawInstanced();

    m_stats.drawCalls++;
    m_stats.triangleCount +=
        static_cast<int>(b.mesh->triangleCount()) *
        static_cast<int>(b.transforms.size());
  }
}

// ============================================================
//  感知目标的部件化装配
//
//  原实现：所有类别都画成一个缩放的立方体。问题很明显 ——
//  轿车、卡车、行人、骑行者、锥桶在画面里长得一模一样。
//
//  现在按类别装配不同部件组合，并加入「喷漆色」概念：
//  车身用接近真实的车漆色（珍珠白/银灰/深蓝/中国红...），
//  类别信息则由 3D 包围盒的线框颜色承载（见 buildDebugLines），
//  这样既好看、又能和数据语义解耦。
// ============================================================

namespace
{
// 由目标 id 稳定地挑一个车漆色 —— 同一个目标每帧颜色一致，不会闪烁
glm::vec3 paintColorFor(int seed, bool truck)
{
  // 轿车：偏家用车的实色与金属漆
  static const glm::vec3 kCarPaints[] = {
      {0.90f, 0.91f, 0.93f}, // 珍珠白
      {0.62f, 0.65f, 0.70f}, // 银灰
      {0.16f, 0.19f, 0.26f}, // 深蓝黑
      {0.70f, 0.14f, 0.14f}, // 中国红
      {0.14f, 0.32f, 0.58f}, // 深海蓝
      {0.20f, 0.22f, 0.24f}, // 石墨黑
      {0.86f, 0.72f, 0.32f}, // 香槟金
  };
  // 卡车：偏商用车的素色
  static const glm::vec3 kTruckPaints[] = {
      {0.92f, 0.93f, 0.94f}, // 冷白
      {0.30f, 0.38f, 0.52f}, // 蓝灰
      {0.62f, 0.30f, 0.16f}, // 铁锈橙
      {0.24f, 0.42f, 0.34f}, // 墨绿
  };

  if (truck)
  {
    const int n = static_cast<int>(sizeof(kTruckPaints) / sizeof(kTruckPaints[0]));
    return kTruckPaints[((seed % n) + n) % n];
  }

  const int n = static_cast<int>(sizeof(kCarPaints) / sizeof(kCarPaints[0]));
  return kCarPaints[((seed % n) + n) % n];
}

// 行人衣服配色（同样按 seed 稳定选取）
glm::vec3 clothColorFor(int seed)
{
  static const glm::vec3 kCloth[] = {
      {0.78f, 0.22f, 0.24f}, // 红
      {0.20f, 0.36f, 0.66f}, // 蓝
      {0.24f, 0.52f, 0.32f}, // 绿
      {0.86f, 0.78f, 0.42f}, // 米黄
      {0.42f, 0.28f, 0.52f}, // 紫
      {0.22f, 0.24f, 0.28f}, // 深灰
  };
  const int n = static_cast<int>(sizeof(kCloth) / sizeof(kCloth[0]));
  return kCloth[((seed % n) + n) % n];
}

// 玻璃 / 轮胎 / 灯具等公共色
const glm::vec3 kGlassColor(0.055f, 0.075f, 0.095f);
const glm::vec3 kTireColor(0.055f, 0.055f, 0.060f);
const glm::vec3 kRimColor(0.72f, 0.74f, 0.78f);
const glm::vec3 kHeadLampColor(1.00f, 0.96f, 0.80f);
const glm::vec3 kTailLampColor(0.95f, 0.16f, 0.12f);
const glm::vec3 kSkinColor(0.85f, 0.68f, 0.54f);

// 轮胎 / 轮辋材质
constexpr float kTireMetal = 0.05f, kTireRough = 0.86f, kTireRim = 0.18f;
constexpr float kRimMetal = 0.70f, kRimRough = 0.34f, kRimRim = 0.42f;
constexpr float kGlassMetal = 0.55f, kGlassRough = 0.12f, kGlassRim = 0.85f;
constexpr float kPaintMetal = 0.42f, kPaintRough = 0.26f, kPaintRim = 0.52f;
} // namespace

// ---- 车轮摆放：车身局部坐标 -> 世界坐标 ----
namespace
{
// 把「车身局部坐标」下的点变换到世界坐标
// center: 车辆中心世界坐标; yawDeg: 航向; 车身局部 X=横向, Y=高, Z=纵向
glm::vec3 localToWorld(const glm::vec3 &center, float yawDeg,
                       const glm::vec3 &local)
{
  const float a = glm::radians(yawDeg);
  const float c = std::cos(a);
  const float s = std::sin(a);
  // glm::rotate 绕 Y 轴：x' = x*cos + z*sin, z' = -x*sin + z*cos
  return center + glm::vec3(local.x * c + local.z * s, local.y,
                            -local.x * s + local.z * c);
}

// 构造「车辆局部坐标系」的基矩阵（平移 + 绕 Y 旋转，不含缩放）
glm::mat4 vehicleFrame(const glm::vec3 &center, float yawDeg)
{
  glm::mat4 m = glm::translate(glm::mat4(1.0f), center);
  m = glm::rotate(m, glm::radians(yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
  return m;
}

// 部件变换：车辆局部变换 * 局部平移 * 缩放
glm::mat4 partInFrame(const glm::mat4 &frame, const glm::vec3 &offset,
                      const glm::vec3 &size)
{
  glm::mat4 m = glm::translate(frame, offset);
  m = glm::scale(m, size);
  return m;
}

// 一个车轮的世界变换：车轮网格轴向已是 X（横向），
// 因此只需要车辆朝向旋转 + 位置，无需再绕 Z 转
glm::mat4 wheelTransform(const glm::vec3 &center, float yawDeg,
                         const glm::vec3 &local, float radius, float width)
{
  glm::mat4 m = vehicleFrame(localToWorld(center, yawDeg, local), yawDeg);
  // 车轮网格：直径 1、宽 1（轴向 X），故 X 乘宽度、Y/Z 乘直径
  m = glm::scale(m, glm::vec3(width, radius * 2.0f, radius * 2.0f));
  return m;
}
} // namespace

void Renderer::buildCarParts(std::vector<InstanceBatch> &out,
                             const glm::vec3 &pos, float yawDeg, float length,
                             float height, float width,
                             const glm::vec3 &paint, float emissive)
{
  const glm::mat4 frame = vehicleFrame(pos, yawDeg);

  // ---- 车身：网格局部是「长 1 / 宽 1 / 高 1 归一化」，直接按实际尺寸缩放 ----
  // 车身高 62%，下方留给离地间隙与车轮
  const float bodyH = height * 0.62f;
  const float bodyY = -height * 0.5f + height * 0.19f + bodyH * 0.5f;
  const glm::vec3 bodyOffset(0.0f, bodyY, 0.0f);
  const glm::vec3 bodySize(width, bodyH, length);

  pushPart(out, &m_props.carBody, partInFrame(frame, bodyOffset, bodySize),
           paint, kPaintMetal, kPaintRough, kPaintRim, 0.0f);

  // ---- 座舱玻璃带：与车身同尺寸套在车身外侧（网格本身比车身小一圈）----
  pushPart(out, &m_props.carGlass, partInFrame(frame, bodyOffset, bodySize),
           kGlassColor, kGlassMetal, kGlassRough, kGlassRim, 0.0f);

  // ---- 四个车轮 ----
  const float wheelRadius = 0.34f * (height / 1.5f);
  const float wheelWidth = width * 0.13f;
  const float wheelY = -height * 0.5f + wheelRadius;
  const float halfTrack = width * 0.44f;
  const float axle = length * 0.31f;

  const glm::vec3 wheelLocal[4] = {
      {halfTrack, wheelY, axle},  {-halfTrack, wheelY, axle},
      {halfTrack, wheelY, -axle}, {-halfTrack, wheelY, -axle}};

  for (const glm::vec3 &wl : wheelLocal)
  {
    pushPart(out, &m_props.wheel,
             wheelTransform(pos, yawDeg, wl, wheelRadius, wheelWidth),
             kTireColor, kTireMetal, kTireRough, kTireRim, 0.0f);

    // 轮辋稍微外移，避免被轮胎完全包住看不见
    glm::vec3 rimLocal = wl;
    rimLocal.x += (wl.x > 0.0f ? 1.0f : -1.0f) * wheelWidth * 0.30f;
    pushPart(out, &m_props.wheelRim,
             wheelTransform(pos, yawDeg, rimLocal, wheelRadius * 0.62f,
                            wheelWidth * 0.45f),
             kRimColor, kRimMetal, kRimRough, kRimRim, 0.0f);
  }

  // ---- 车灯：亮度由 emissive 控制（夜间或开启车灯开关时更亮）----
  pushPart(out, &m_props.headLight,
           partInFrame(frame,
                       glm::vec3(0.0f, -height * 0.5f + bodyH * 0.42f,
                                 length * 0.5f - length * 0.02f),
                       glm::vec3(width, bodyH * 0.42f, length * 0.03f)),
           kHeadLampColor, 0.30f, 0.16f, 0.45f, emissive * 1.10f);

  pushPart(out, &m_props.tailLight,
           partInFrame(frame,
                       glm::vec3(0.0f, -height * 0.5f + bodyH * 0.46f,
                                 -length * 0.5f + length * 0.02f),
                       glm::vec3(width, bodyH * 0.40f, length * 0.03f)),
           kTailLampColor, 0.30f, 0.20f, 0.45f, emissive * 0.70f);
}

void Renderer::buildTruckParts(std::vector<InstanceBatch> &out,
                               const glm::vec3 &pos, float yawDeg, float length,
                               float height, float width,
                               const glm::vec3 &cabColor,
                               const glm::vec3 &cargoColor, float emissive)
{
  const glm::mat4 frame = vehicleFrame(pos, yawDeg);

  // 卡车是「驾驶室 + 货箱」两段式：
  //   驾驶室占车头 26%，货箱占其余部分
  const float cabLen = length * 0.26f;
  const float cargoLen = length * 0.70f;
  const float cabH = height * 0.62f;
  const float cargoH = height * 0.84f;

  const float baseY = -height * 0.5f;
  const float cabZ = length * 0.5f - cabLen * 0.5f;
  const float cargoZ = length * 0.5f - cabLen - cargoLen * 0.5f;

  // ---- 驾驶室（位置略高，坐在车轮之上）----
  const glm::vec3 cabOffset(0.0f, baseY + height * 0.16f + cabH * 0.5f, cabZ);
  pushPart(out, &m_props.truckCab,
           partInFrame(frame, cabOffset, glm::vec3(width * 0.98f, cabH, cabLen)),
           cabColor, kPaintMetal, kPaintRough * 1.3f, kPaintRim * 0.8f, 0.0f);

  // ---- 驾驶室风挡玻璃 ----
  pushPart(out, &m_props.truckGlass,
           partInFrame(frame, cabOffset, glm::vec3(width * 0.98f, cabH, cabLen)),
           kGlassColor, kGlassMetal, kGlassRough, kGlassRim, 0.0f);

  // ---- 货箱：方正高大，这是卡车最醒目的特征 ----
  pushPart(out, &m_props.truckCargo,
           partInFrame(frame,
                       glm::vec3(0.0f, baseY + height * 0.06f + cargoH * 0.5f,
                                 cargoZ),
                       glm::vec3(width, cargoH, cargoLen)),
           cargoColor, 0.20f, 0.60f, 0.30f, 0.0f);

  // ---- 车轮：重卡轮径更大，且后轴为双联轴（共 6 个轮位）----
  const float wheelRadius = 0.20f + 0.46f * (height / 3.2f);
  const float wheelWidth = width * 0.14f;
  const float halfTrack = width * 0.45f;
  const float wheelY = -height * 0.5f + wheelRadius;

  const float frontAxleZ = length * 0.40f;
  const float rearAxleZ1 = -length * 0.22f;
  const float rearAxleZ2 = -length * 0.36f;

  const glm::vec3 truckWheels[6] = {
      {halfTrack, wheelY, frontAxleZ}, {-halfTrack, wheelY, frontAxleZ},
      {halfTrack, wheelY, rearAxleZ1}, {-halfTrack, wheelY, rearAxleZ1},
      {halfTrack, wheelY, rearAxleZ2}, {-halfTrack, wheelY, rearAxleZ2}};

  for (const glm::vec3 &wl : truckWheels)
  {
    pushPart(out, &m_props.wheel,
             wheelTransform(pos, yawDeg, wl, wheelRadius, wheelWidth),
             kTireColor, kTireMetal, kTireRough, kTireRim, 0.0f);

    glm::vec3 rimLocal = wl;
    rimLocal.x += (wl.x > 0.0f ? 1.0f : -1.0f) * wheelWidth * 0.30f;
    pushPart(out, &m_props.wheelRim,
             wheelTransform(pos, yawDeg, rimLocal, wheelRadius * 0.58f,
                            wheelWidth * 0.45f),
             kRimColor, kRimMetal, kRimRough, kRimRim, 0.0f);
  }

  // ---- 车灯 ----
  pushPart(out, &m_props.headLight,
           partInFrame(frame,
                       glm::vec3(0.0f, baseY + height * 0.16f + cabH * 0.28f,
                                 length * 0.5f - length * 0.01f),
                       glm::vec3(width * 0.9f, cabH * 0.22f, length * 0.02f)),
           kHeadLampColor, 0.30f, 0.16f, 0.45f, emissive * 1.10f);

  pushPart(out, &m_props.tailLight,
           partInFrame(frame,
                       glm::vec3(0.0f, baseY + height * 0.06f + cargoH * 0.22f,
                                 -length * 0.5f + length * 0.01f),
                       glm::vec3(width * 0.9f, cargoH * 0.16f, length * 0.02f)),
           kTailLampColor, 0.30f, 0.20f, 0.45f, emissive * 0.70f);
}

// ---- 行人：躯干 + 头 + 双腿（variant 决定站姿微差，避免整街人形雷同）----
void Renderer::buildPedestrianParts(std::vector<InstanceBatch> &out,
                                    const glm::vec3 &pos, float yawDeg,
                                    float height, const glm::vec3 &cloth,
                                    int variant)
{
  const glm::mat4 frame = vehicleFrame(pos, yawDeg);

  const float baseY = -height * 0.5f;
  // 人体比例（按身高归一化）：腿 ~47%、躯干 ~34%、头 ~16%
  const float legH = height * 0.47f;
  const float torsoH = height * 0.34f;
  const float headD = height * 0.16f;

  // 站姿差异：不同 variant 让躯干轻微前后偏移
  const float lean = 0.01f + 0.025f * static_cast<float>(variant % 3);

  // ---- 双腿（裤子，比上衣深一档）----
  pushPart(out, &m_props.pedLegs,
           partInFrame(frame, glm::vec3(0.0f, baseY + legH * 0.5f, -lean * 0.5f),
                       glm::vec3(height * 0.30f, legH, height * 0.22f)),
           glm::vec3(cloth.x * 0.42f, cloth.y * 0.42f, cloth.z * 0.48f), 0.05f,
           0.72f, 0.22f, 0.0f);

  // ---- 躯干（着衣，用衣服颜色）----
  pushPart(out, &m_props.pedBody,
           partInFrame(frame,
                       glm::vec3(0.0f, baseY + legH + torsoH * 0.5f, lean),
                       glm::vec3(height * 0.40f, torsoH, height * 0.24f)),
           cloth, 0.05f, 0.78f, 0.24f, 0.0f);

  // ---- 头 + 颈（肤色）----
  pushPart(out, &m_props.pedHead,
           partInFrame(frame,
                       glm::vec3(0.0f, baseY + legH + torsoH + headD * 0.52f,
                                 lean),
                       glm::vec3(headD * 1.05f, headD, headD * 1.05f)),
           kSkinColor, 0.02f, 0.80f, 0.28f, 0.0f);
}

// ---- 骑行者：自行车 + 前倾骑手 ----
void Renderer::buildCyclistParts(std::vector<InstanceBatch> &out,
                                 const glm::vec3 &pos, float yawDeg,
                                 float height, const glm::vec3 &cloth)
{
  const glm::mat4 frame = vehicleFrame(pos, yawDeg);

  const float baseY = -height * 0.5f;
  const float bikeH = height * 0.52f;

  // ---- 自行车（深色金属，高金属度低粗糙度）----
  pushPart(out, &m_props.cycleFrame,
           partInFrame(frame, glm::vec3(0.0f, baseY + bikeH * 0.5f, 0.0f),
                       glm::vec3(height * 0.55f, bikeH, height * 1.05f)),
           glm::vec3(0.12f, 0.14f, 0.18f), 0.72f, 0.34f, 0.48f, 0.0f);

  // ---- 骑手（前倾姿态已在网格中内建）----
  pushPart(out, &m_props.riderBody,
           partInFrame(frame, glm::vec3(0.0f, baseY + bikeH * 0.62f, 0.0f),
                       glm::vec3(height * 0.68f, height * 0.72f, height * 0.95f)),
           cloth, 0.05f, 0.76f, 0.26f, 0.0f);
}

// ---- 锥桶：方形底座 + 橙色锥体 ----
void Renderer::buildConeParts(std::vector<InstanceBatch> &out,
                              const glm::vec3 &pos, float yawDeg, float height)
{
  const glm::mat4 frame = vehicleFrame(pos, yawDeg);
  const float baseY = -height * 0.5f;

  // ---- 底座 ----
  pushPart(out, &m_props.coneBase,
           partInFrame(frame, glm::vec3(0.0f, baseY + height * 0.05f, 0.0f),
                       glm::vec3(height * 0.62f, height * 0.10f, height * 0.62f)),
           glm::vec3(0.16f, 0.17f, 0.19f), 0.10f, 0.70f, 0.25f, 0.0f);

  // ---- 锥体（橙红，带一点自发光让它从场景里跳出来）----
  pushPart(out, &m_props.coneBody,
           partInFrame(frame, glm::vec3(0.0f, baseY + height * 0.52f, 0.0f),
                       glm::vec3(height * 0.55f, height * 0.90f, height * 0.55f)),
           glm::vec3(0.96f, 0.36f, 0.10f), 0.05f, 0.55f, 0.30f, 0.06f);
}


void Renderer::drawSky(const Camera &camera, const VisualSettings &s)
{
  // 天空不写深度；配合 sky.vert 中 gl_Position.z = w 的技巧，
  // 用 LEQUAL 让天空刚好通过深度测试，从而只填充背景像素
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);

  const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);

  m_skyShader.use();
  m_skyShader.setMat4("uProjection", camera.projectionMatrix(aspect));
  m_skyShader.setMat4("uView", camera.viewMatrix());
  m_skyShader.setVec3("uSkyTop", s.skyTop);
  m_skyShader.setVec3("uSkyHorizon", s.skyHorizon);
  m_skyShader.setVec3("uSkyBottom", s.skyBottom);

  // 天空盒立方体放大到 2x2x2，顶点着色器里会去掉视图平移使天空始终跟随相机
  m_skyCube.draw();
  m_stats.drawCalls++;

  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
}

// ============================================================
//  2. 路面
// ============================================================

// ============================================================
//  2. 路面（沥青主体 + 两侧人行道 + 路缘石 + 中央分隔带）
//
//  原实现只有一块巨大的沥青平面，视觉上像「跑道」而不是「街道」。
//  这里在主体之上再叠三层次要几何：
//    · 人行道：路缘外侧的浅灰水泥板（把人行道「抬高」到 +0.15）
//    · 路缘石：路缘处的一条窄亮带，是街道最关键的视觉分界
//    · 中央分隔带：双向道路之间的绿化带（仅视觉）
//  这些都复用同一套无光照贴图的简化着色（m_groundShader）。
// ============================================================

void Renderer::drawGround(const Camera &camera, const AdasWorld &world,
                          const VisualSettings &s)
{
  const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);

  m_groundShader.use();
  m_groundShader.setMat4("uView", camera.viewMatrix());
  m_groundShader.setMat4("uProjection", camera.projectionMatrix(aspect));

  // 路面跟随自车位置，产生「无限道路」的效果；尺寸需覆盖雾的能见范围
  const glm::vec3 egoPos = world.ego().position;
  glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                   glm::vec3(egoPos.x, 0.0f, egoPos.z + 120.0f));
  model = glm::scale(model, glm::vec3(360.0f, 1.0f, 560.0f));
  m_groundShader.setMat4("uModel", model);

  // 复用 scene.frag，因此必须提供它声明的全部 uniform
  m_groundShader.setVec3("uBaseColor", glm::vec3(0.155f, 0.165f, 0.175f)); // 沥青灰
  m_groundShader.setVec3("uLightDir", glm::normalize(s.lightDir));
  m_groundShader.setVec3("uLightColor", s.lightColor);
  m_groundShader.setFloat("uLightIntensity", s.lightIntensity);
  m_groundShader.setVec3("uAmbientSky", s.ambientSky);
  m_groundShader.setVec3("uAmbientGround", s.ambientGround);
  m_groundShader.setFloat("uAmbientIntensity", s.ambientIntensity);
  m_groundShader.setVec3("uCameraPos", camera.position());
  m_groundShader.setVec3("uFogColor", s.fogColor);
  m_groundShader.setFloat("uFogDensity", s.enableFog ? s.fogDensity : 0.0f);
  m_groundShader.setFloat("uMetallic", 0.0f);
  m_groundShader.setFloat("uRoughness", 0.85f);
  m_groundShader.setFloat("uRimStrength", 0.0f); // 地面不要边缘光
  m_groundShader.setFloat("uOpacity", 1.0f);
  m_groundShader.setFloat("uEmissive", 0.0f);
  m_groundShader.setBool("uUseInstancing", false);

  m_ground.draw();
  m_stats.drawCalls++;
  m_stats.triangleCount += static_cast<int>(m_ground.triangleCount());

  // ---- 街道结构层：主路面 / 人行道 / 路缘石 / 中央分隔带 ----
  // 用同一个平面网格实例化绘制，因此只多几次 draw call。
  //
  //  横断面（与 adas.cpp 顶部的约定严格一致）：
  //      |X| <= 13.1            双向 6 车道路面
  //      |X| ≈ 13.25            路缘石
  //      13.4 <= |X| <= 21.0    人行道（抬高 15cm）
  //      |X| > 21.0             绿化带 / 建筑退线
  //      |X| <= 0.9             中央分隔带（双黄线）
  {
    // 一个辅助 lambda：画一块水平平板（厚度由 Y 缩放体现）
    auto slab = [&](const glm::vec3 &center, float sx, float sz, float thick,
                    const glm::vec3 &color, float rough)
    {
      glm::mat4 m = glm::translate(glm::mat4(1.0f), center);
      m = glm::scale(m, glm::vec3(sx, thick, sz));
      m_groundShader.setMat4("uModel", m);
      m_groundShader.setVec3("uBaseColor", color);
      m_groundShader.setFloat("uRoughness", rough);
      m_groundShader.setVec3("uCameraPos", camera.position());
      m_ground.draw();
      m_stats.drawCalls++;
      m_stats.triangleCount += static_cast<int>(m_ground.triangleCount());
    };

    const float followZ = egoPos.z + 120.0f;
    const float length = 560.0f;

    // 人行道：路缘石（|X| = 13.4）到绿化带（|X| = 21）之间，
    // 抬高 15 cm —— 这是城市道路最典型的高差
    for (int side = -1; side <= 1; side += 2)
    {
      slab(glm::vec3(static_cast<float>(side) * 17.2f, 0.075f, followZ), 7.6f,
           length, 0.15f, glm::vec3(0.42f, 0.42f, 0.41f), 0.92f);
    }

    // 路缘石：|X| ≈ 13.25 处的一条窄亮带，把沥青与人行道明确分开
    for (int side = -1; side <= 1; side += 2)
    {
      slab(glm::vec3(static_cast<float>(side) * 13.25f, 0.085f, followZ), 0.30f,
           length, 0.17f, glm::vec3(0.56f, 0.56f, 0.54f), 0.88f);
    }

    // 中央分隔带：双黄线之间的窄条（不是物理岛，只是颜色区分）
    slab(glm::vec3(0.0f, 0.018f, followZ), 0.85f, length, 0.035f,
         glm::vec3(0.26f, 0.24f, 0.12f), 0.95f);

    // 两侧绿化带基座：人行道之外、建筑退线之内
    for (int side = -1; side <= 1; side += 2)
    {
      slab(glm::vec3(static_cast<float>(side) * 30.0f, 0.045f, followZ), 18.0f,
           length, 0.09f, glm::vec3(0.22f, 0.30f, 0.20f), 0.96f);
    }
  }
}

// ============================================================
//  3. 自车
// ============================================================

void Renderer::drawEgo(const Camera &camera, const AdasWorld &world,
                       const VisualSettings &s)
{
  if (!s.showEgoBody)
    return;

  const EgoVehicle &ego = world.ego();
  const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);

  m_sceneShader.use();
  m_sceneShader.setMat4("uView", camera.viewMatrix());
  m_sceneShader.setMat4("uProjection", camera.projectionMatrix(aspect));
  setSceneCommonUniforms(camera, s);

  // 自车与感知目标共用同一套「轿车」零件装配，只是配色固定为珍珠白、
  // 并且因为自车是真实存在的车（不是检测框），所以不做置信度压暗。
  const glm::vec3 pearlWhite(0.93f, 0.94f, 0.96f);
  const float lampGlow = s.showHeadlights ? 0.95f : 0.0f;

  std::vector<InstanceBatch> batches;

  // 注意：EgoVehicle::position 是「后轴中心」，而 buildCarParts 期望车身几何中心
  buildCarParts(batches, ego.center(), ego.heading, ego.length, ego.height,
                ego.width, pearlWhite, lampGlow);

  flushBatches(batches);

  // 自车前后轮转向可视化：前轮跟随 steeringAngle 转角
  if (std::fabs(ego.steeringAngle) > 0.05f)
  {
    std::vector<InstanceBatch> steerBatches;

    const float hRad = glm::radians(ego.heading);
    const float wheelRadius = 0.34f * (ego.height / 1.5f);
    const float wheelWidth = ego.width * 0.13f;
    const float halfTrack = ego.width * 0.44f;
    const float frontAxle = -ego.wheelBase * 0.5f;
    const float wheelY = wheelRadius - ego.height * 0.5f;

    for (int sgn = -1; sgn <= 1; sgn += 2)
    {
      const glm::vec3 local(static_cast<float>(sgn) * halfTrack, wheelY,
                            frontAxle);
      const glm::vec3 worldOffset(
          local.x * std::cos(hRad) + local.z * std::sin(hRad), local.y,
          -local.x * std::sin(hRad) + local.z * std::cos(hRad));

      glm::mat4 wheel = glm::translate(glm::mat4(1.0f),
                                       ego.center() + worldOffset);
      wheel = glm::rotate(wheel, hRad, glm::vec3(0.0f, 1.0f, 0.0f));
      // 转向角：前轮额外绕 Y 轴偏转
      wheel = glm::rotate(wheel, glm::radians(ego.steeringAngle),
                          glm::vec3(0.0f, 1.0f, 0.0f));
      wheel = glm::scale(wheel, glm::vec3(wheelWidth, wheelRadius * 2.0f,
                                          wheelRadius * 2.0f));

      pushPart(steerBatches, &m_props.wheel, wheel, kTireColor, kTireMetal,
               kTireRough, kTireRim, 0.0f);
    }

    flushBatches(steerBatches);
  }
}

// ============================================================
//  4. 感知目标（按类别装配不同车型，再按「网格+材质」批次实例化）
//
//  这是本次改动的核心：不再把所有目标画成同一个立方体，
//  而是按 ObjectClass 分派到不同的零件组合：
//    Car        -> 车身三段式 + 座舱玻璃 + 4 轮 + 前后灯
//    Truck      -> 驾驶室 + 货箱 + 6 轮 + 前后灯
//    Pedestrian -> 躯干 + 头 + 双腿
//    Cyclist    -> 自行车 + 前倾骑手
//    Cone       -> 方底座 + 真圆锥
//
//  性能：部件按「网格 + 材质 + 颜色」归并成批次，
//  每批一次 drawInstanced。整场景通常只需 15~30 次 draw call。
// ============================================================

void Renderer::drawObjects(const Camera &camera, const AdasWorld &world,
                           const VisualSettings &s)
{
  const auto &objects = world.objects();
  if (objects.empty())
    return;

  const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);

  m_sceneShader.use();
  m_sceneShader.setMat4("uView", camera.viewMatrix());
  m_sceneShader.setMat4("uProjection", camera.projectionMatrix(aspect));
  setSceneCommonUniforms(camera, s);

  std::vector<InstanceBatch> batches;

  // 车灯自发光强度：真实车灯很亮，白天也不该看不见
  const float lampGlow = s.showHeadlights ? 0.85f : 0.0f;

  for (const auto &o : objects)
  {
    // 置信度影响整体明暗：低置信度目标刷成灰调，肉眼即可识别不确定
    const float dim = 0.62f + 0.38f * o.confidence;

    // 未被稳定跟踪 / 预测目标：明显压暗，符合 ADAS 界面的语义约定
    float fade = 1.0f;
    if (!o.tracked)
      fade *= 0.78f;
    if (o.predicted)
      fade *= 0.72f;

    const float shade = dim * fade;

    switch (o.cls)
    {
    case ObjectClass::Car:
    {
      const glm::vec3 paint =
          paintColorFor(o.id, false) * shade;
      buildCarParts(batches, o.position, o.yaw, o.length, o.height, o.width,
                    paint, lampGlow);
      break;
    }

    case ObjectClass::Truck:
    {
      // 驾驶室用彩色、货箱用素白/浅灰 —— 现实中绝大多数货车就是这种搭配
      const glm::vec3 cab = paintColorFor(o.id, true) * shade;
      const glm::vec3 cargo = glm::vec3(0.88f, 0.89f, 0.90f) * shade;
      buildTruckParts(batches, o.position, o.yaw, o.length, o.height, o.width,
                      cab, cargo, lampGlow);
      break;
    }

    case ObjectClass::Pedestrian:
      buildPedestrianParts(batches, o.position, o.yaw, o.height,
                           clothColorFor(o.id) * shade, o.id);
      break;

    case ObjectClass::Cyclist:
      buildCyclistParts(batches, o.position, o.yaw, o.height,
                        clothColorFor(o.id) * shade);
      break;

    case ObjectClass::Cone:
      buildConeParts(batches, o.position, o.yaw, o.height);
      break;

    default:
      // 未知类别：退化为一个朴素灰盒（保留「不确定性」的视觉表达）
      pushPart(batches, &m_props.buildingTop,
               [&o]
               {
                 glm::mat4 m = glm::translate(glm::mat4(1.0f), o.position);
                 m = glm::rotate(m, glm::radians(o.yaw),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
                 return glm::scale(m, glm::vec3(o.width, o.height, o.length));
               }(),
               glm::vec3(0.62f, 0.64f, 0.66f) * shade, 0.10f, 0.70f, 0.25f, 0.0f);
      break;
    }
  }

  flushBatches(batches);

  m_stats.instanceCount = static_cast<int>(objects.size());
}

// ============================================================
//  5. 街景（树 / 楼房 / 路灯 / 行人 / 护栏……）
//
//  街景不属于感知数据，是为了让场景看起来像真实城市街道而存在的
//  背景层。它同样走「批次实例化」管线，因此几百个道具也只需
//  几十次 draw call。
//
//  每个道具类型都由若干基础网格拼成：
//    Tree        -> 树干 + 3 层球状树冠（不同绿色，大小错落）
//    Building    -> 裙楼 + 主体 + 楼顶设备间 + 自发光窗带
//    StreetLamp  -> 灯杆 + 悬臂 + 自发光灯头
//    UtilityPole -> 细长杆 + 横担
//    TrafficLight-> 灯杆 + 横臂 + 三色灯组
//    BusStop     -> 站台顶棚 + 立柱 + 广告面板
//    GuardRail   -> 立柱 + 横梁
//    Billboard   -> 面板 + 支架
//    Bush        -> 2~3 个压扁的球
//    ParkedCar   -> 复用轿车零件
// ============================================================

namespace
{
// 树冠绿：不同变体取不同深浅，避免整排树一个色
glm::vec3 canopyColorFor(int variant, int layer)
{
  static const glm::vec3 kGreens[] = {
      {0.13f, 0.36f, 0.14f}, {0.18f, 0.45f, 0.17f}, {0.10f, 0.30f, 0.12f},
      {0.24f, 0.50f, 0.20f}, {0.16f, 0.40f, 0.22f}, {0.20f, 0.42f, 0.14f},
  };
  const int n = static_cast<int>(sizeof(kGreens) / sizeof(kGreens[0]));
  glm::vec3 c = kGreens[((variant + layer) % n + n) % n];
  // 上层略亮，模拟顶部受光
  return c * (0.92f + 0.10f * static_cast<float>(layer));
}

// 楼体外墙色：现代城市常见的几种立面配色
glm::vec3 facadeColorFor(int variant)
{
  static const glm::vec3 kFacades[] = {
      {0.56f, 0.57f, 0.59f}, // 混凝土灰
      {0.66f, 0.63f, 0.58f}, // 暖米灰
      {0.42f, 0.47f, 0.54f}, // 蓝灰玻璃幕墙
      {0.70f, 0.68f, 0.64f}, // 浅砂石
      {0.48f, 0.50f, 0.52f}, // 深灰
      {0.60f, 0.55f, 0.50f}, // 砖红调
  };
  const int n = static_cast<int>(sizeof(kFacades) / sizeof(kFacades[0]));
  return kFacades[((variant % n) + n) % n];
}
} // namespace

void Renderer::drawStreetScene(const Camera &camera, const AdasWorld &world,
                               const VisualSettings &s)
{
  if (!s.showStreetScene)
    return;

  const auto &props = world.props();
  if (props.empty())
    return;

  const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);

  m_sceneShader.use();
  m_sceneShader.setMat4("uView", camera.viewMatrix());
  m_sceneShader.setMat4("uProjection", camera.projectionMatrix(aspect));
  setSceneCommonUniforms(camera, s);

  std::vector<InstanceBatch> batches;

  // 灯类自发光强度（路灯 / 信号灯 / 窗带在雾里也应有存在感）
  const float lampGlow = s.showHeadlights ? 0.75f : 0.0f;

  for (const auto &p : props)
  {
    const PropKind kind = static_cast<PropKind>(p.kind);

    // 道具中心：p.position 记录的是底面中心，这里换算到几何中心
    const glm::vec3 center(p.position.x, p.position.y + p.height * 0.5f,
                           p.position.z);

    glm::mat4 frame = glm::translate(glm::mat4(1.0f), center);
    if (p.yaw != 0.0f)
      frame = glm::rotate(frame, glm::radians(p.yaw), glm::vec3(0.0f, 1.0f, 0.0f));

    switch (kind)
    {
    case PropKind::Tree:
    {
      // ---- 树干 ----
      pushPart(batches, &m_props.trunk,
               partInFrame(frame, glm::vec3(0.0f, -p.height * 0.30f, 0.0f),
                           glm::vec3(p.width * 0.34f, p.height * 0.62f,
                                     p.depth * 0.34f)),
               glm::vec3(0.24f, 0.17f, 0.11f), 0.02f, 0.90f, 0.15f, 0.0f);

      // ---- 3 层树冠：从下到上依次变小、变亮 ----
      const float layers[3] = {0.0f, 0.16f, 0.30f};
      const float scales[3] = {1.00f, 0.78f, 0.55f};

      for (int L = 0; L < 3; ++L)
      {
        const float crownD = p.width * scales[L];
        const float y = -p.height * 0.12f + p.height * (0.10f + layers[L]);

        pushPart(batches, &m_props.canopy,
                 partInFrame(frame, glm::vec3(0.0f, y, 0.0f),
                             glm::vec3(crownD, crownD * 0.88f, crownD)),
                 canopyColorFor(p.variant, L), 0.02f, 0.92f, 0.20f, 0.0f);
      }
      break;
    }

    case PropKind::Bush:
    {
      // 2~3 个压扁的球叠成一丛
      const int blobs = 2 + (p.variant % 2);
      for (int b = 0; b < blobs; ++b)
      {
        const float ang = static_cast<float>(b) * 2.1f;
        const float rr = p.width * 0.28f;
        const float bd = p.width * (0.62f - 0.10f * static_cast<float>(b));

        pushPart(batches, &m_props.bushBall,
                 partInFrame(frame,
                             glm::vec3(std::cos(ang) * rr * 0.6f,
                                       (static_cast<float>(b) - 0.4f) * 0.14f,
                                       std::sin(ang) * rr * 0.6f),
                             glm::vec3(bd, p.height * 0.9f, bd)),
                 canopyColorFor(p.variant, b), 0.02f, 0.94f, 0.18f, 0.0f);
      }
      break;
    }

    case PropKind::Building:
    {
      const glm::vec3 wall = facadeColorFor(p.variant);

      // ---- 裙楼：底部 18% 高，比主楼宽 12% ----
      const float baseH = p.height * 0.18f;
      pushPart(batches, &m_props.buildingBase,
               partInFrame(frame,
                           glm::vec3(0.0f, -p.height * 0.5f + baseH * 0.5f, 0.0f),
                           glm::vec3(p.width * 1.12f, baseH, p.depth * 1.12f)),
               wall * 0.92f, 0.10f, 0.72f, 0.22f, 0.0f);

      // ---- 主体：裙楼之上到楼顶 ----
      const float bodyH = p.height - baseH - p.height * 0.035f;
      pushPart(batches, &m_props.buildingBody,
               partInFrame(frame,
                           glm::vec3(0.0f, -p.height * 0.5f + baseH + bodyH * 0.5f,
                                     0.0f),
                           glm::vec3(p.width, bodyH, p.depth)),
               wall, 0.14f, 0.66f, 0.26f, 0.0f);

      // ---- 窗带：四面环绕的自发光薄片 ----
      pushPart(batches, &m_props.windowBand,
               partInFrame(frame,
                           glm::vec3(0.0f, -p.height * 0.5f + baseH + bodyH * 0.5f,
                                     0.0f),
                           glm::vec3(p.width, bodyH * 0.94f, p.depth)),
               glm::vec3(0.42f, 0.56f, 0.72f), 0.64f, 0.14f, 0.70f,
               lampGlow * 0.55f);

      // ---- 楼顶设备间 / 电梯机房 ----
      const float topH = p.height * 0.045f;
      pushPart(batches, &m_props.buildingTop,
               partInFrame(frame,
                           glm::vec3(0.0f, p.height * 0.5f + topH * 0.5f, 0.0f),
                           glm::vec3(p.width * 0.34f, topH, p.depth * 0.34f)),
               wall * 0.80f, 0.10f, 0.78f, 0.20f, 0.0f);
      break;
    }

    case PropKind::StreetLamp:
    {
      // ---- 灯杆 ----
      pushPart(batches, &m_props.lampPole,
               partInFrame(frame, glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(p.width * 1.6f, p.height, p.depth * 1.6f)),
               glm::vec3(0.30f, 0.32f, 0.34f), 0.62f, 0.42f, 0.38f, 0.0f);

      // ---- 悬臂：从杆顶伸向路面（朝道路中心）----
      // 左侧（flip=true）伸向 +X，右侧伸向 -X
      const float dir = p.flip ? 1.0f : -1.0f;
      const float armLen = 2.6f;
      const float armY = p.height * 0.5f - 0.30f;

      glm::mat4 armFrame = glm::translate(frame, glm::vec3(0.0f, armY, 0.0f));
      armFrame = glm::rotate(armFrame, glm::radians(-8.0f),
                             glm::vec3(0.0f, 0.0f, 1.0f));

      pushPart(batches, &m_props.lampArm,
               glm::scale(glm::translate(armFrame,
                                         glm::vec3(dir * armLen * 0.5f, 0.0f, 0.0f)),
                          glm::vec3(armLen, 0.11f, 0.11f)),
               glm::vec3(0.30f, 0.32f, 0.34f), 0.62f, 0.42f, 0.38f, 0.0f);

      // ---- 灯头（自发光）----
      pushPart(batches, &m_props.lampHead,
               glm::scale(glm::translate(armFrame,
                                         glm::vec3(dir * (armLen + 0.30f), -0.16f,
                                                   0.0f)),
                          glm::vec3(1.05f, 0.20f, 0.46f)),
               glm::vec3(1.00f, 0.95f, 0.80f), 0.20f, 0.24f, 0.40f,
               lampGlow * 1.30f);
      break;
    }

    case PropKind::UtilityPole:
    {
      // ---- 杆身（细长，深灰木/水泥色）----
      pushPart(batches, &m_props.lampPole,
               partInFrame(frame, glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(p.width * 1.3f, p.height, p.depth * 1.3f)),
               glm::vec3(0.34f, 0.30f, 0.27f), 0.05f, 0.88f, 0.16f, 0.0f);

      // ---- 两道横担 ----
      for (int c = 0; c < 2; ++c)
      {
        const float y = p.height * (0.34f - static_cast<float>(c) * 0.075f);
        pushPart(batches, &m_props.lampArm,
                 glm::scale(glm::translate(frame, glm::vec3(0.0f, y, 0.0f)),
                            glm::vec3(2.4f, 0.09f, 0.09f)),
                 glm::vec3(0.30f, 0.26f, 0.23f), 0.05f, 0.90f, 0.16f, 0.0f);
      }
      break;
    }

    case PropKind::TrafficLight:
    {
      // ---- 立杆 ----
      pushPart(batches, &m_props.lampPole,
               partInFrame(frame, glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(p.width * 1.5f, p.height, p.depth * 1.5f)),
               glm::vec3(0.26f, 0.28f, 0.30f), 0.58f, 0.46f, 0.36f, 0.0f);

      // ---- 横臂 ----
      const float dir = p.flip ? 1.0f : -1.0f;
      const float armLen = 3.4f;
      const float armY = p.height * 0.5f - 0.35f;

      pushPart(batches, &m_props.lampArm,
               glm::scale(glm::translate(frame,
                                         glm::vec3(dir * armLen * 0.5f, armY, 0.0f)),
                          glm::vec3(armLen, 0.14f, 0.14f)),
               glm::vec3(0.26f, 0.28f, 0.30f), 0.58f, 0.46f, 0.36f, 0.0f);

      // ---- 三色灯组：红 / 黄 / 绿 ----
      //  哪一盏亮、哪两盏灭，由所在路口的信号相位决定：
      //    相位 0.00~0.43 绿灯（通行）
      //    相位 0.43~0.57 黄灯（过渡）
      //    相位 0.57~1.00 红灯（禁行）
      //  这样做出的效果是「自车一路向前，路口红绿交替变化」，
      //  比原来「红绿常亮」真实得多。
      const float phase = world.signalPhaseAt(p.position.z);

      // 索引 0=红 1=黄 2=绿
      int active = 0;
      if (phase < 0.43f)
        active = 2;
      else if (phase < 0.57f)
        active = 1;

      const glm::vec3 lampColors[3] = {
          {0.95f, 0.14f, 0.12f}, // 红
          {0.98f, 0.76f, 0.12f}, // 黄
          {0.18f, 0.90f, 0.34f}, // 绿
      };

      for (int L = 0; L < 3; ++L)
      {
        const float zOff = (1.0f - static_cast<float>(L)) * 0.42f;
        const bool on = (L == active);

        // 亮的那盏：全亮 + 强自发光；灭的那两盏：压暗到近黑，
        //  这正是真实信号灯在白天/夜间的观感差异来源。
        const glm::vec3 c = on ? lampColors[L] : lampColors[L] * 0.16f;

        pushPart(batches, &m_props.lampHead,
                 glm::scale(glm::translate(frame,
                                           glm::vec3(dir * (armLen - 0.35f), armY,
                                                     zOff)),
                            glm::vec3(on ? 0.32f : 0.28f, on ? 0.38f : 0.32f,
                                      on ? 0.32f : 0.28f)),
                 c, 0.25f, 0.30f, 0.50f, on ? lampGlow * 1.60f : 0.0f);
      }
      break;
    }

    case PropKind::BusStop:
    {
      const float roofH = 0.16f;
      const float postH = p.height - roofH;

      // ---- 顶棚（略前倾，像真实候车亭）----
      pushPart(batches, &m_props.lampArm,
               glm::scale(glm::translate(frame,
                                         glm::vec3(0.0f, p.height * 0.5f - roofH,
                                                   0.0f)),
                          glm::vec3(p.width, roofH, p.depth * 1.25f)),
               glm::vec3(0.36f, 0.42f, 0.48f), 0.42f, 0.36f, 0.42f, 0.0f);

      // ---- 四根立柱 ----
      const float px = p.width * 0.42f;
      const float pz = p.depth * 0.48f;
      const glm::vec3 posts[4] = {{px, 0.0f, pz},
                                  {-px, 0.0f, pz},
                                  {px, 0.0f, -pz},
                                  {-px, 0.0f, -pz}};

      for (const glm::vec3 &po : posts)
      {
        pushPart(batches, &m_props.lampPole,
                 partInFrame(frame,
                             glm::vec3(po.x, -p.height * 0.5f + postH * 0.5f,
                                       po.z),
                             glm::vec3(0.12f, postH, 0.12f)),
                 glm::vec3(0.30f, 0.34f, 0.38f), 0.62f, 0.40f, 0.38f, 0.0f);
      }

      // ---- 背面广告面板（自发光，做成灯箱）----
      pushPart(batches, &m_props.signPlate,
               partInFrame(frame, glm::vec3(0.0f, -p.height * 0.06f, -p.depth * 0.52f),
                           glm::vec3(p.width * 0.86f, p.height * 0.52f,
                                     p.depth * 0.06f)),
               glm::vec3(0.72f, 0.80f, 0.90f), 0.30f, 0.30f, 0.45f,
               lampGlow * 0.50f);
      break;
    }

    case PropKind::GuardRail:
    {
      // ---- 立柱（每段两端各一根）----
      for (int e = -1; e <= 1; e += 2)
      {
        pushPart(batches, &m_props.railPost,
                 partInFrame(frame,
                             glm::vec3(0.0f, -p.height * 0.18f,
                                       static_cast<float>(e) * p.depth * 0.48f),
                             glm::vec3(p.width * 1.4f, p.height, p.depth * 0.14f)),
                 glm::vec3(0.44f, 0.46f, 0.48f), 0.70f, 0.40f, 0.42f, 0.0f);
      }

      // ---- 波形横梁 ----
      pushPart(batches, &m_props.railBeam,
               partInFrame(frame, glm::vec3(0.0f, p.height * 0.10f, 0.0f),
                           glm::vec3(p.width, p.height * 0.34f, p.depth)),
               glm::vec3(0.62f, 0.64f, 0.66f), 0.76f, 0.34f, 0.50f, 0.0f);
      break;
    }

    case PropKind::Billboard:
    {
      // ---- 两根支架 ----
      for (int e = -1; e <= 1; e += 2)
      {
        pushPart(batches, &m_props.lampPole,
                 partInFrame(frame,
                             glm::vec3(static_cast<float>(e) * p.width * 0.36f,
                                       -p.height * 0.30f, 0.0f),
                             glm::vec3(0.26f, p.height * 0.50f, 0.26f)),
                 glm::vec3(0.28f, 0.30f, 0.32f), 0.62f, 0.44f, 0.36f, 0.0f);
      }

      // ---- 面板（灯箱，自发光）----
      pushPart(batches, &m_props.signPlate,
               partInFrame(frame, glm::vec3(0.0f, p.height * 0.28f, 0.0f),
                           glm::vec3(p.width, p.height * 0.62f, p.depth)),
               glm::vec3(0.86f, 0.62f, 0.34f), 0.24f, 0.36f, 0.42f,
               lampGlow * 0.62f);
      break;
    }

    case PropKind::Pedestrian:
    {
      // 复用行人的部件装配（这里用固定的 variant 保证颜色稳定）
      buildPedestrianParts(batches, center, p.yaw, p.height,
                           clothColorFor(p.variant), p.variant);
      break;
    }

    case PropKind::Cyclist:
    {
      // 街头骑行者也复用骑行者的部件装配
      buildCyclistParts(batches, center, p.yaw, p.height,
                        clothColorFor(p.variant));
      break;
    }

    case PropKind::ParkedCar:
    {
      // 复用轿车零件，车身颜色按 variant 稳定选取
      buildCarParts(batches, center, p.yaw, p.depth, p.height, p.width,
                    paintColorFor(p.variant, false), 0.0f);
      break;
    }

    // ---------- 道路标线：斑马线 / 停止线 / 路口铺装 ----------
    case PropKind::Zebra:
    {
      // 斑马线由「若干条沿 X 方向的白条」组成。
      //  条纹数由 variant 稳定决定（6~8 条），间距均匀铺满 depth。
      const int stripes = 6 + (p.variant % 3);
      const float stripeW = p.depth / static_cast<float>(stripes * 2 - 1);

      for (int i = 0; i < stripes; ++i)
      {
        const float off =
            -p.depth * 0.5f + stripeW * (static_cast<float>(i) * 2.0f + 0.5f);
        // p.yaw == 90 时条纹方向要转 90 度（横向马路上的斑马线）
        const glm::vec3 local = (p.yaw == 90.0f)
                                    ? glm::vec3(off, 0.0f, 0.0f)
                                    : glm::vec3(0.0f, 0.0f, off);
        const glm::vec3 size = (p.yaw == 90.0f)
                                   ? glm::vec3(stripeW, p.height, p.width)
                                   : glm::vec3(p.width, p.height, stripeW);

        pushPart(batches, &m_props.roadSlab,
                 partInFrame(frame, glm::vec3(center.x + local.x,
                                              p.height * 0.5f - center.y + 0.008f,
                                              center.z + local.z),
                             size),
                 glm::vec3(0.90f, 0.90f, 0.88f), 0.02f, 0.72f, 0.10f, 0.0f);
      }
      break;
    }

    case PropKind::StopLine:
    {
      pushPart(batches, &m_props.roadSlab,
               partInFrame(frame, glm::vec3(0.0f, -center.y + p.height * 0.5f + 0.008f,
                                            0.0f),
                           glm::vec3(p.width, p.height, p.depth)),
               glm::vec3(0.92f, 0.92f, 0.90f), 0.02f, 0.70f, 0.10f, 0.0f);
      break;
    }

    case PropKind::CrossBox:
    {
      // 路口铺装：一整块横向沥青板，颜色比主路面略浅（新铺装）
      pushPart(batches, &m_props.roadSlab,
               partInFrame(frame, glm::vec3(0.0f, -center.y + p.height * 0.5f,
                                            0.0f),
                           glm::vec3(p.width, p.height, p.depth)),
               glm::vec3(0.175f, 0.185f, 0.195f), 0.0f, 0.88f, 0.0f, 0.0f);
      break;
    }

    default:
      break;
    }
  }

  flushBatches(batches);
}

// ============================================================
//  6. 感知可视化层（全部用线段绘制）
// ============================================================

void Renderer::buildDebugLines(const Camera &camera, const AdasWorld &world,
                               const VisualSettings &s)
{
  (void)camera;
  m_lines.clear();

  const EgoVehicle &ego = world.ego();
  const glm::vec3 egoPos = ego.center();

  // ---------- 5.1 地面网格（ADAS 标定参考）----------
  if (s.showGrid)
  {
    const glm::vec3 gridColor(0.30f, 0.34f, 0.38f);
    const glm::vec3 axisColor(0.55f, 0.60f, 0.68f);

    // 网格跟随自车移动，每格 5 米
    const float step = (s.gridExtent * 2.0f) / static_cast<float>(s.gridDivisions);
    for (int i = 0; i <= s.gridDivisions; ++i)
    {
      const float p = -s.gridExtent + step * static_cast<float>(i);
      const bool isAxis = std::fabs(p) < 1e-3f;

      // 沿 Z 方向的线（X = 常数）
      const float lx = egoPos.x + p;
      m_lines.addLine(glm::vec3(lx, 0.005f, egoPos.z - s.gridExtent),
                      glm::vec3(lx, 0.005f, egoPos.z + s.gridExtent),
                      isAxis ? axisColor : gridColor);

      // 沿 X 方向的线（Z = 常数）
      const float lz = egoPos.z + p;
      m_lines.addLine(glm::vec3(egoPos.x - s.gridExtent, 0.005f, lz),
                      glm::vec3(egoPos.x + s.gridExtent, 0.005f, lz),
                      isAxis ? axisColor : gridColor);
    }
  }

  // ---------- 5.2 车道线（带宽度渲染）----------
  if (s.showLaneLines)
  {
    const float halfW = s.laneLineWidth * 0.5f;

    for (const auto &lane : world.laneLines())
    {
      const glm::vec3 color = lane.color();
      const auto &pts = lane.points;
      if (pts.size() < 2)
        continue;

      for (size_t i = 0; i + 1 < pts.size(); ++i)
      {
        const glm::vec3 &a = pts[i].position;
        const glm::vec3 &b = pts[i + 1].position;

        // 线段方向与 XZ 平面内的横向法线
        glm::vec3 dir = b - a;
        dir.y = 0.0f;
        if (glm::length(dir) < 1e-5f)
          continue;
        dir = glm::normalize(dir);
        const glm::vec3 side(-dir.z, 0.0f, dir.x);

        // 左右两条边缘线，形成有宽度的「带」
        m_lines.addLine(glm::vec3(a.x, s.laneHeight, a.z) + side * halfW,
                        glm::vec3(b.x, s.laneHeight, b.z) + side * halfW, color);
        m_lines.addLine(glm::vec3(a.x, s.laneHeight, a.z) - side * halfW,
                        glm::vec3(b.x, s.laneHeight, b.z) - side * halfW, color * 0.85f);
      }

      // 虚线：每隔一段加横向端头，强化「虚线」的视觉特征
      if (lane.type == LaneType::Dashed)
      {
        for (size_t i = 0; i + 1 < pts.size(); i += 2)
        {
          const glm::vec3 dir = glm::normalize(
              glm::vec3(pts[i + 1].position.x - pts[i].position.x, 0.0f,
                        pts[i + 1].position.z - pts[i].position.z));
          if (glm::length(dir) < 1e-5f)
            continue;
          const glm::vec3 side(-dir.z, 0.0f, dir.x);
          const glm::vec3 &p = pts[i].position;
          m_lines.addLine(glm::vec3(p.x, s.laneHeight, p.z) + side * halfW,
                          glm::vec3(p.x, s.laneHeight, p.z) - side * halfW, color);
        }
      }

      m_stats.lanePointCount += static_cast<int>(pts.size());
    }
  }

  // ---------- 5.3 目标 3D 包围盒 / 速度矢量 / 预测轨迹 / 标签 / 检测点 ----------
  for (const auto &o : world.objects())
  {
    const glm::vec3 baseColor = colorOf(o.cls);
    const float yawRad = glm::radians(o.yaw);
    const float cy = std::cos(yawRad);
    const float sy = std::sin(yawRad);
    const glm::vec3 h = o.sizeVec() * 0.5f;

    if (s.showBoundingBoxes)
    {
      glm::vec3 boxColor = baseColor;

      // 未被稳定跟踪的目标用暗色，表达「不确定」
      if (!o.tracked)
        boxColor *= 0.55f;

      // TTC 危险目标显示为红色高亮（ADAS 报警的标准视觉语言）
      if (o.ttc > 0.0f && o.ttc < 3.0f)
        boxColor = glm::vec3(1.0f, 0.15f, 0.15f);

      const glm::vec3 size = o.sizeVec() * s.boxLineScale;
      m_lines.addOrientedBox(o.position, size, o.yaw, boxColor);

      // 底面再加粗描一遍：ADAS 界面常用手法，强调「接地」与占地面积
      const float bhx = size.x * 0.5f;
      const float bhz = size.z * 0.5f;
      const float bhy = size.y * 0.5f;

      const float lx[4] = {-bhx, bhx, bhx, -bhx};
      const float lz[4] = {-bhz, -bhz, bhz, bhz};

      glm::vec3 base[4];
      for (int i = 0; i < 4; ++i)
      {
        base[i] = glm::vec3(o.position.x + lx[i] * cy + lz[i] * sy,
                            o.position.y - bhy,
                            o.position.z - lx[i] * sy + lz[i] * cy);
      }
      for (int i = 0; i < 4; ++i)
        m_lines.addLine(base[i], base[(i + 1) % 4], boxColor * 1.35f);
    }

    if (s.showSpeedVector && o.speed() > 0.5f)
    {
      // 速度矢量：从目标中心沿速度方向绘制，长度正比于速度
      m_lines.addLine(o.position, o.position + o.velocity * 0.35f,
                      baseColor * 0.6f, baseColor);
    }

    if (s.showPredictedPath)
    {
      // 预测轨迹：沿速度方向外推 1.5 秒，用「虚线」表示预测值
      const float predTime = 1.5f;
      const int steps = 6;
      glm::vec3 prev = o.position;
      for (int i = 1; i <= steps; ++i)
      {
        const float t = predTime * static_cast<float>(i) / static_cast<float>(steps);
        const glm::vec3 cur = o.position + o.velocity * t;
        const glm::vec3 mid = glm::mix(prev, cur, 0.55f); // 留出间隙形成虚线
        m_lines.addLine(prev, mid, baseColor * 0.5f);
        prev = cur;
      }
    }

    if (s.showLabels)
    {
      // 标签引线：从包围盒顶部向上引一条短线，
      // ImGui 覆盖层会在这条线的顶点附近绘制文字标签
      const glm::vec3 top = o.position + glm::vec3(0.0f, h.y, 0.0f);
      m_lines.addLine(top, top + glm::vec3(0.0f, kLabelLeaderHeight, 0.0f), baseColor);
    }

    if (s.showDetectionPoints)
    {
      // 检测点云：在包围盒表面采样「小十字」，模拟雷达/激光雷达的原始回波
      for (int ix = -1; ix <= 1; ++ix)
      {
        for (int iz = -1; iz <= 1; ++iz)
        {
          if (ix == 0 && iz == 0)
            continue;

          const float lpx = h.x * static_cast<float>(ix);
          const float lpz = h.z * static_cast<float>(iz);
          const float lpy = ((ix + iz) % 2 == 0) ? h.y : -h.y;

          const glm::vec3 p(o.position.x + lpx * cy + lpz * sy,
                            o.position.y + lpy,
                            o.position.z - lpx * sy + lpz * cy);

          const float c = 0.16f;
          const glm::vec3 pc = baseColor * 0.8f;
          m_lines.addLine(p - glm::vec3(c, 0.0f, 0.0f), p + glm::vec3(c, 0.0f, 0.0f), pc);
          m_lines.addLine(p - glm::vec3(0.0f, c, 0.0f), p + glm::vec3(0.0f, c, 0.0f), pc);
          m_lines.addLine(p - glm::vec3(0.0f, 0.0f, c), p + glm::vec3(0.0f, 0.0f, c), pc);
        }
      }
    }
  }

  // ---------- 5.4 传感器 FOV 锥（ADAS 界面的标志性元素）----------
  if (s.showSensorFov)
  {
    const float hRad = glm::radians(ego.heading);

    for (const auto &fov : world.sensors())
    {
      if (!fov.enabled)
        continue;

      // 传感器安装点（车身局部坐标 -> 世界坐标）
      const glm::vec3 originWorld(
          ego.position.x + fov.origin.x * std::cos(hRad) + fov.origin.z * std::sin(hRad),
          ego.position.y + fov.origin.y,
          ego.position.z - fov.origin.x * std::sin(hRad) + fov.origin.z * std::cos(hRad));

      // 水平视场 >= 359 度视为全景（激光雷达），用完整圆周采样
      const bool isOmni = fov.hfovDeg >= 359.0f;
      const int azSteps = isOmni ? 24 : 6;
      const int elSteps = 2;
      const int ringSize = elSteps * 2 + 1;

      const float halfHfovRad = glm::radians(fov.hfovDeg * 0.5f);
      const float halfVfovRad = glm::radians(fov.vfovDeg * 0.5f);

      // 计算远端球冠上的采样点
      std::vector<glm::vec3> rim;
      rim.reserve(static_cast<size_t>((azSteps + 1) * ringSize));

      for (int ai = 0; ai <= azSteps; ++ai)
      {
        const float t = static_cast<float>(ai) / static_cast<float>(azSteps);
        const float az = isOmni
                             ? t * 2.0f * 3.14159265358979f
                             : -halfHfovRad + t * 2.0f * halfHfovRad;

        // 全景时不需要重复首尾点，这里多算一个也无妨（线会重合）
        for (int ei = -elSteps; ei <= elSteps; ++ei)
        {
          const float el = halfVfovRad * static_cast<float>(ei) /
                           static_cast<float>(elSteps);

          const float cosEl = std::cos(el);

          // 车辆坐标系下的方向：车头 +Z，右侧 +X
          const glm::vec3 localDir(std::sin(az) * cosEl, std::sin(el), std::cos(az) * cosEl);

          // 转到世界坐标（绕 Y 轴旋转 hRad）
          const glm::vec3 worldDir(
              localDir.x * std::cos(hRad) + localDir.z * std::sin(hRad),
              localDir.y,
              -localDir.x * std::sin(hRad) + localDir.z * std::cos(hRad));

          rim.push_back(originWorld + worldDir * fov.range);
        }
      }

      const glm::vec3 col = fov.color * s.fovAlpha;

      // 远端轮廓：纵向 + 横向网格线，勾勒出「锥形探测范围」
      for (int ai = 0; ai <= azSteps; ++ai)
      {
        for (int ei = 0; ei < elSteps * 2; ++ei)
        {
          const size_t idx = static_cast<size_t>(ai * ringSize + ei);
          m_lines.addLine(rim[idx], rim[idx + 1], col);
        }
      }
      for (int ei = 0; ei < ringSize; ++ei)
      {
        for (int ai = 0; ai < azSteps; ++ai)
        {
          const size_t i0 = static_cast<size_t>(ai * ringSize + ei);
          const size_t i1 = static_cast<size_t>((ai + 1) * ringSize + ei);
          m_lines.addLine(rim[i0], rim[i1], col);
        }
      }

      // 从传感器到远端轮廓的放射线（稀疏采样，避免线太多糊成一团）
      const int rayStride = isOmni ? 4 : 1;
      for (int ai = 0; ai <= azSteps; ai += rayStride)
      {
        const size_t idx = static_cast<size_t>(ai * ringSize + elSteps);
        m_lines.addLine(originWorld, rim[idx], col * 0.55f);
      }

      // 360 度传感器额外画地面投影圆环
      if (isOmni)
      {
        m_lines.addCircle(glm::vec3(originWorld.x, 0.02f, originWorld.z), fov.range,
                          64, fov.color * 0.35f);
      }
    }
  }

  // ---------- 5.5 自车规划轨迹 ----------
  if (s.showEgoPath && ego.plannedPath.size() >= 2)
  {
    const auto &path = ego.plannedPath;
    const glm::vec3 pathColor(0.35f, 0.95f, 1.00f); // 青色：规划量

    for (size_t i = 0; i + 1 < path.size(); ++i)
    {
      glm::vec3 dir = path[i + 1] - path[i];
      dir.y = 0.0f;
      if (glm::length(dir) < 1e-5f)
        continue;
      dir = glm::normalize(dir);

      // 用左右边界线表示轨迹宽度，风格与车道线统一
      const glm::vec3 side = glm::vec3(-dir.z, 0.0f, dir.x) * 0.9f;
      m_lines.addLine(path[i] + side, path[i + 1] + side, pathColor);
      m_lines.addLine(path[i] - side, path[i + 1] - side, pathColor * 0.7f);
    }

    // 终点标记
    m_lines.addCircle(path.back() + glm::vec3(0.0f, 0.05f, 0.0f), 1.2f, 16, pathColor);
  }

  // ---------- 5.6 自车行驶轨迹（历史）----------
  if (s.showTrail)
  {
    const glm::vec3 trailColor(1.00f, 0.75f, 0.30f);
    for (size_t i = 0; i + 1 < m_egoTrail.size(); ++i)
      m_lines.addLine(m_egoTrail[i], m_egoTrail[i + 1], trailColor * 0.8f);
  }

  // ---------- 5.7 车道保持安全走廊 ----------
  // 画出自车左右各半个车宽的纵向参考线，便于判断是否压线
  {
    const float hRad = glm::radians(ego.heading);
    const glm::vec3 fwd(std::sin(hRad), 0.0f, std::cos(hRad));
    const glm::vec3 side(fwd.z, 0.0f, -fwd.x);
    const float halfW = ego.width * 0.5f;
    const glm::vec3 corridor(0.60f, 0.65f, 0.72f);

    m_lines.addLine(ego.center() + side * halfW + fwd * 2.0f,
                    ego.center() + side * halfW + fwd * 40.0f, corridor * 0.7f);
    m_lines.addLine(ego.center() - side * halfW + fwd * 2.0f,
                    ego.center() - side * halfW + fwd * 40.0f, corridor * 0.7f);
  }

  m_lines.flush();
  m_stats.lineCount = static_cast<int>(m_lines.lineCount());
}

// ============================================================
//  BEV 图层（特斯拉 FSD 风格俯视可视化）
// ============================================================

// 按布局算出 BEV 的视口矩形（窗口像素坐标，左下原点）
void Renderer::bevViewportRect(int windowW, int windowH, const BevSettings &b,
                               int &vx, int &vy, int &vw, int &vh) const
{
  switch (b.layout)
  {
  case BevLayout::Fullscreen:
  case BevLayout::BevMain:
    // 铺满整个窗口
    vx = 0;
    vy = 0;
    vw = windowW;
    vh = windowH;
    break;

  case BevLayout::Split:
    // 上下分屏：下半屏给 BEV
    vx = 0;
    vy = 0;
    vw = windowW;
    vh = windowH / 2;
    break;

  case BevLayout::PictureInPicture:
  default:
  {
    // 右下角小地图
    const int m = static_cast<int>(static_cast<float>(windowH) * b.pipMarginRatio);
    vw = static_cast<int>(static_cast<float>(windowW) * b.pipWidthRatio);
    vh = static_cast<int>(static_cast<float>(vw) * 0.82f); // 略扁，贴近车机纵横比
    vx = windowW - vw - m;
    vy = m;
    break;
  }
  }
}

// 构建 BEV 正交视图下的线段：米格网 / 车道拓扑 / 目标框 / 预测轨迹 /
// 传感器覆盖 / 自车轮廓。
//
//  这些线全部在「自车坐标系」下构建，再由 BEV 的正交矩阵投影，
//  因此 lockToEgo 打开时它们会自动跟着自车一起旋转。
void Renderer::buildBevLines(const AdasWorld &world, const VisualSettings &s)
{
  m_bevLines.clear();

  const BevSettings &b = s.bev;
  const EgoVehicle &ego = world.ego();

  const float hRad = glm::radians(ego.heading);
  const float ch = std::cos(hRad);
  const float sh = std::sin(hRad);

  // 世界坐标 -> 自车坐标系（X 右、Z 前），Y 固定为 0（BEV 是二维的）
  auto toLocal = [&](const glm::vec3 &wp) -> glm::vec3
  {
    const glm::vec3 d = wp - ego.position;
    return glm::vec3(d.x * ch - d.z * sh, 0.0f, d.x * sh + d.z * ch);
  };

  // ---- 1. 米格网：每 10 米一条，50 米整倍数更亮 ----
  if (b.showGrid)
  {
    const glm::vec3 gridColor(0.22f, 0.26f, 0.30f);
    const glm::vec3 majorColor(0.34f, 0.40f, 0.46f);

    for (float x = -b.rangeSide; x <= b.rangeSide; x += 10.0f)
    {
      const bool major = std::fabs(std::fmod(x, 50.0f)) < 1e-3f;
      m_bevLines.addLine(glm::vec3(x, 0.01f, -b.rangeBack),
                         glm::vec3(x, 0.01f, b.rangeForward),
                         major ? majorColor : gridColor);
    }

    for (float z = -b.rangeBack; z <= b.rangeForward; z += 10.0f)
    {
      const bool major = std::fabs(std::fmod(z, 50.0f)) < 1e-3f;
      m_bevLines.addLine(glm::vec3(-b.rangeSide, 0.01f, z),
                         glm::vec3(b.rangeSide, 0.01f, z),
                         major ? majorColor : gridColor);
    }
  }

  // ---- 2. 车道拓扑：车道线转成自车系下的折线 ----
  if (b.showLaneTopology)
  {
    for (const auto &lane : world.laneLines())
    {
      const auto &pts = lane.points;
      if (pts.size() < 2)
        continue;

      const glm::vec3 col = lane.color() * 0.85f;

      for (size_t i = 0; i + 1 < pts.size(); ++i)
      {
        const glm::vec3 a = toLocal(pts[i].position);
        const glm::vec3 bb = toLocal(pts[i + 1].position);

        // 视野外的线段直接丢掉（BEV 范围外画了也看不见）
        if ((a.z < -b.rangeBack - 10.0f && bb.z < -b.rangeBack - 10.0f) ||
            (a.z > b.rangeForward + 10.0f && bb.z > b.rangeForward + 10.0f) ||
            (std::fabs(a.x) > b.rangeSide + 10.0f &&
             std::fabs(bb.x) > b.rangeSide + 10.0f))
          continue;

        m_bevLines.addLine(a, bb, col);
      }
    }
  }

  // ---- 3. 传感器覆盖范围：把 FOV 投影到地面（圆形或扇形）----
  if (b.showSensorCoverage && s.showSensorFov)
  {
    for (const auto &fov : world.sensors())
    {
      const glm::vec3 origin = toLocal(ego.position + fov.origin);
      const float range = std::min(fov.range, b.rangeForward * 1.6f);
      const glm::vec3 col = fov.color * 0.30f;

      if (fov.hfovDeg >= 359.0f)
      {
        // 360 度：直接画圆
        m_bevLines.addCircle(origin + glm::vec3(0.0f, 0.02f, 0.0f), range, 48,
                             col);
      }
      else
      {
        // 有限 FOV：画扇形（远端弧 + 两条边界线）
        const float half = glm::radians(fov.hfovDeg * 0.5f);
        const int steps = 16;
        glm::vec3 prev(0.0f);

        for (int i = 0; i <= steps; ++i)
        {
          // 传感器朝向与自车一致（+Z），因此直接在自车系里按 ±half 展开
          const float a = -half + 2.0f * half * static_cast<float>(i) /
                                     static_cast<float>(steps);
          const glm::vec3 p =
              origin + glm::vec3(std::sin(a) * range, 0.0f, std::cos(a) * range);

          if (i > 0)
            m_bevLines.addLine(prev, p, col);

          prev = p;
        }

        m_bevLines.addLine(origin,
                           origin + glm::vec3(std::sin(-half) * range, 0.0f,
                                              std::cos(-half) * range),
                           col * 0.7f);
        m_bevLines.addLine(origin,
                           origin + glm::vec3(std::sin(half) * range, 0.0f,
                                              std::cos(half) * range),
                           col * 0.7f);
      }
    }
  }

  // ---- 4. 目标框 + 速度矢量 + 预测轨迹 ----
  if (b.showObjects)
  {
    for (const auto &o : world.objects())
    {
      const glm::vec3 c = toLocal(o.position);

      // 视野外的目标不画
      if (c.z < -b.rangeBack - 5.0f || c.z > b.rangeForward + 5.0f ||
          std::fabs(c.x) > b.rangeSide + 5.0f)
        continue;

      const glm::vec3 col = colorOf(o.cls);
      const float localYaw = o.yaw - ego.heading;

      // ---- 4.1 目标矩形（BEV 里的「方块车」）----
      //  汽车在俯视图里就是一个带朝向的矩形，
      //  特斯拉界面里也是这么画的。
      m_bevLines.addOrientedBox(c + glm::vec3(0.0f, 0.06f, 0.0f),
                                glm::vec3(o.width, 0.0f, o.length), localYaw,
                                col);

      // ---- 4.2 速度矢量：从中心指向运动方向，长度 = speed × 系数 ----
      const float spd = o.speed();
      if (spd > 0.5f)
      {
        const float vYaw = glm::radians(o.yaw - ego.heading);
        const glm::vec3 dir(std::sin(vYaw), 0.0f, std::cos(vYaw));
        const float len = std::min(spd * 0.6f, 14.0f);

        m_bevLines.addLine(c + glm::vec3(0.0f, 0.08f, 0.0f),
                           c + glm::vec3(0.0f, 0.08f, 0.0f) + dir * len,
                           col * 1.15f);
      }

      // ---- 4.3 预测轨迹：沿速度方向外推 1.5 秒，渐隐 ----
      //  与 occupancy 栅格里的外推用同一套参数（0.3s × 5 点），
      //  保证「线画在哪」与「栅格未来占据在哪」完全一致。
      if (b.showPredictions && spd > 0.5f)
      {
        const float vYaw = glm::radians(o.yaw - ego.heading);
        const glm::vec3 dir(std::sin(vYaw), 0.0f, std::cos(vYaw));

        glm::vec3 prev = c;
        for (int i = 1; i <= 5; ++i)
        {
          const float t = static_cast<float>(i) * 0.3f;
          const glm::vec3 p = c + dir * (spd * t);

          // 越远越暗：用衰减模拟「预测不确定性递增」
          const float fade = 1.0f / (1.0f + static_cast<float>(i) * 0.45f);
          m_bevLines.addLine(prev + glm::vec3(0.0f, 0.07f, 0.0f),
                             p + glm::vec3(0.0f, 0.07f, 0.0f), col * fade);
          prev = p;
        }
      }
    }
  }

  // ---- 5. 自车轮廓：BEV 的正中央 ----
  //  高亮白色矩形 + 车头方向箭头，对应特斯拉界面里居中的那个车形图标。
  {
    const glm::vec3 egoCol(0.92f, 0.96f, 1.00f);
    m_bevLines.addOrientedBox(glm::vec3(0.0f, 0.12f, 0.0f),
                              glm::vec3(ego.width, 0.0f, ego.length), 0.0f,
                              egoCol);

    const float halfW = ego.width * 0.5f;
    const float front = ego.length * 0.5f;
    m_bevLines.addLine(glm::vec3(-halfW, 0.12f, front),
                       glm::vec3(0.0f, 0.12f, front + 1.6f), egoCol);
    m_bevLines.addLine(glm::vec3(halfW, 0.12f, front),
                       glm::vec3(0.0f, 0.12f, front + 1.6f), egoCol);
  }

  // ---- 6. 规划轨迹：青色，最上层 ----
  if (s.showEgoPath && ego.plannedPath.size() >= 2)
  {
    const glm::vec3 pathCol(0.35f, 0.95f, 1.00f);
    for (size_t i = 0; i + 1 < ego.plannedPath.size(); ++i)
    {
      m_bevLines.addLine(
          toLocal(ego.plannedPath[i]) + glm::vec3(0.0f, 0.14f, 0.0f),
          toLocal(ego.plannedPath[i + 1]) + glm::vec3(0.0f, 0.14f, 0.0f),
          pathCol);
    }
  }

  m_bevLines.flush();
}

// 绘制占据栅格 + 可行驶区域这两张 R32F 纹理
void Renderer::drawOccupancyTextures(const AdasWorld &world,
                                     const VisualSettings &s)
{
  const BevSettings &b = s.bev;
  const OccupancyGrid &grid = m_bev.grid();

  if (grid.width() <= 0 || grid.height() <= 0)
    return;

  // 栅格数据 -> GPU 纹理（只在重建过时上传，避免每帧 glTexImage2D）
  m_bev.uploadGridTextures();

  Shader &sh = m_bev.occupancyShader();
  sh.use();

  // 板的基础顶点是 ±1（正方形），需要映射到实际视野范围：
  //   X：±rangeSide
  //   Z：[-rangeBack, +rangeForward]（不对称，所以是「缩放 + 平移」）
  //  最后右乘到 uViewProj 上，等价于给板加一个模型矩阵，
  //  这样 occupancy.vert 就不需要额外声明 uModel。
  const float halfZ = (b.rangeForward + b.rangeBack) * 0.5f;
  const float midZ = (b.rangeForward - b.rangeBack) * 0.5f;

  glm::mat4 model =
      glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, midZ));
  model = glm::scale(model, glm::vec3(b.rangeSide, 1.0f, halfZ));

  sh.setMat4("uViewProj", m_bev.projectionMatrix(b) *
                              m_bev.viewMatrix(world, b) * model);
  sh.setFloat("uDepthOffset", 0.0f);

  sh.setInt("uOccupancy", 0);
  sh.setInt("uDrivable", 1);
  sh.setFloat("uOccupancyOpacity", b.occupancyOpacity);
  sh.setFloat("uDrivableOpacity", b.drivableOpacity);
  sh.setBool("uShowOccupancy", b.showOccupancy);
  sh.setBool("uShowDrivable", b.showDrivable);
  sh.setVec2("uGridSize",
             glm::vec2(static_cast<float>(grid.width()),
                       static_cast<float>(grid.height())));
  // 格子边线只在「格子足够大」时才画，否则会糊成一片网格噪声
  sh.setBool("uShowCellEdges", b.showOccupancy && b.cellSize >= 0.45f);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, grid.occupancyTexture());
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, grid.drivableTexture());

  // 栅格带透明度，必须开启混合。
  //  同时关掉背面剔除：BEV 相机从上看，板的正面朝上；
  //  但某个视角下绕序可能翻转，表现就是「板突然整块消失」。
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);

  glBindVertexArray(m_occQuadVao);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  glBindVertexArray(0);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);

  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);

  m_stats.drawCalls++;
  m_stats.triangleCount += 2;
}

// 把 BEV 图层渲染到给定的视口矩形
void Renderer::drawBevLayer(int vx, int vy, int vw, int vh, const Camera &camera,
                            const AdasWorld &world, const VisualSettings &s)
{
  (void)camera;
  if (!s.bev.enabled || vw <= 4 || vh <= 4)
    return;

  // 先构建本帧的 BEV 线段（与 GL 状态无关，放在状态切换前做）
  buildBevLines(world, s);

  glViewport(vx, vy, vw, vh);

  // ---- 1. 背景：深色底 ----
  //  BEV 的底不是「路面灰」而是接近黑的深蓝 —— 特斯拉界面也是这样，
  //  因为浅色底会把橙色/黄色的风险热力图淹没，对比度不够。
  glEnable(GL_SCISSOR_TEST);
  glScissor(vx, vy, vw, vh);
  glClearColor(0.045f, 0.055f, 0.070f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);

  // ---- 2. 占据栅格 + 可行驶区域 ----
  //  这两层是「面」，写深度；后面的线不写深度但读深度，
  //  这样才能保证线被更近的实体遮挡。
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);

  drawOccupancyTextures(world, s);

  // ---- 3. 线段层：车道拓扑 / 目标框 / 轨迹 / 自车轮廓 ----
  {
    Shader &ls = m_lineShader;
    ls.use();
    ls.setMat4("uViewProj",
               m_bev.projectionMatrix(s.bev) * m_bev.viewMatrix(world, s.bev));
    ls.setBool("uGammaCorrect", false);
    ls.setFloat("uAlpha", 1.0f);
    // BEV 里所有线都贴在 y = 0 平面附近，需要深度偏移抑制 z-fighting
    ls.setFloat("uDepthOffset", 0.0004f);

    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_bevLines.draw();

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    m_stats.drawCalls++;
  }
}

// ============================================================
//  6. 后处理
// ============================================================

void Renderer::drawPost(const VisualSettings &s)
{
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, m_width, m_height);

  glDisable(GL_DEPTH_TEST);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  m_postShader.use();
  m_postShader.setInt("uScene", 0);
  m_postShader.setFloat("uVignetteStrength", s.enableVignette ? 0.42f : 0.0f);
  m_postShader.setFloat("uVignetteRadius", 0.35f);
  m_postShader.setFloat("uContrast", 1.06f);
  m_postShader.setFloat("uSaturation", 1.10f);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_colorTex);

  glBindVertexArray(m_quadVao);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);

  glEnable(GL_DEPTH_TEST);
  m_stats.drawCalls++;
}

// ============================================================
//  render - 主渲染入口
// ============================================================

void Renderer::render(const Camera &camera, const AdasWorld &world,
                      const VisualSettings &s)
{
  // 重置每帧统计
  m_stats = RenderStats();

  // ---- 0. 先构建本帧的线段数据（开 FBO 前做，逻辑与 GL 状态无关）----
  buildDebugLines(camera, world, s);

  // ---- 1. 绑定离屏 FBO，渲染到纹理 ----
  glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

  // BEV 引擎每帧更新（占据栅格重建 + 可行驶区域生成）。
  //  放在这里是因为它只依赖世界数据，与 GL 状态无关。
  if (s.bev.enabled)
    m_bev.update(world, s.bev);

  // BEV 占据「整个画面」的两种布局下，3D 场景需要退化成右下角小窗
  //  （或完全让位），因此先算出 3D 应该渲染到哪块视口。
  const bool bevIsMain =
      s.bev.enabled && (s.bev.layout == BevLayout::Fullscreen ||
                        s.bev.layout == BevLayout::BevMain);
  const bool bevSplit =
      s.bev.enabled && (s.bev.layout == BevLayout::Split);

  int sceneVx = 0;
  int sceneVy = 0;
  int sceneVw = std::max(1, static_cast<int>(static_cast<float>(m_width) * m_renderScale));
  int sceneVh = std::max(1, static_cast<int>(static_cast<float>(m_height) * m_renderScale));

  const int rw = sceneVw;
  const int rh = sceneVh;

  if (bevIsMain)
  {
    if (s.bev.layout == BevLayout::BevMain)
    {
      // BEV 主画面：3D 退成右下角小窗（特斯拉 FSD v12 的形态）
      const float r = 0.30f;
      sceneVw = static_cast<int>(static_cast<float>(rw) * r);
      sceneVh = static_cast<int>(static_cast<float>(rh) * r);
      sceneVx = rw - sceneVw - static_cast<int>(static_cast<float>(rw) * 0.012f);
      sceneVy = static_cast<int>(static_cast<float>(rh) * 0.012f);
    }
    else
    {
      // 全屏 BEV：3D 场景完全不画（省掉全部 3D 绘制开销）
      sceneVw = 0;
      sceneVh = 0;
    }
  }
  else if (bevSplit)
  {
    // 上下分屏：3D 占上半屏
    sceneVy = rh / 2;
    sceneVh = rh - sceneVy;
  }

  glViewport(sceneVx, sceneVy, std::max(1, sceneVw), std::max(1, sceneVh));

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  // 天空色作为清屏色，避免天空盒边缘出现黑边。
  //  分屏 / 小窗模式下只需要清自己的那块区域：Scissor 比全清更快，
  //  也避免把已经画好的 BEV 区域擦掉。
  if (sceneVw < rw || sceneVh < rh || sceneVx > 0 || sceneVy > 0)
  {
    glEnable(GL_SCISSOR_TEST);
    glScissor(sceneVx, sceneVy, std::max(1, sceneVw), std::max(1, sceneVh));
  }

  glClearColor(s.skyHorizon.r, s.skyHorizon.g, s.skyHorizon.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (sceneVw != rw || sceneVh != rh || sceneVx > 0 || sceneVy > 0)
    glDisable(GL_SCISSOR_TEST);

  // ---- 2. 绘制各层（只在 3D 视口非空时）----
  const bool draw3D = (sceneVw > 0 && sceneVh > 0);
  if (draw3D)
  {
    // 3D 层使用「主视口」的比例计算投影矩阵，
    //  这样小窗里的画面不会因为视口变窄而横向拉伸。
    const float aspect =
        static_cast<float>(rw) / static_cast<float>(std::max(1, rh));

    drawSky(camera, s);                // 天空（不写深度，最先画）
    drawGround(camera, world, s);      // 路面 + 街道结构
    drawStreetScene(camera, world, s); // 街景（树 / 楼 / 路灯 / 行人）
    drawEgo(camera, world, s);         // 自车
    drawObjects(camera, world, s);     // 感知目标（按类别装配车型）

    // ---- 3. 线段层：不写深度但读深度，因此会被实体遮挡、又不会被彼此遮挡 ----
    {
      m_lineShader.use();
      m_lineShader.setMat4("uViewProj",
                           camera.projectionMatrix(aspect) * camera.viewMatrix());
      m_lineShader.setBool("uGammaCorrect", false); // 场景着色器已做 gamma
      m_lineShader.setFloat("uAlpha", 1.0f);
      // 深度微偏移：抑制车道线/网格贴在地面时的 z-fighting
      m_lineShader.setFloat("uDepthOffset", 0.00025f);

      // 线段两侧都可见（车道线从上下看都应存在）
      glDisable(GL_CULL_FACE);
      glDepthMask(GL_FALSE);

      m_lines.draw();
      m_stats.drawCalls++;

      glDepthMask(GL_TRUE);
      glEnable(GL_CULL_FACE);
    }
  }

  // ---- 3.5 BEV 图层 ----
  //  注意：BEV 用「窗口像素」坐标算视口，而 3D 用「渲染缩放后」的尺寸，
  //  两者在同一张 FBO 上，所以这里要把 BEV 的矩形也按渲染缩放换算。
  if (s.bev.enabled)
  {
    int bx, by, bw, bh;
    bevViewportRect(rw, rh, s.bev, bx, by, bw, bh);

    // 渲染缩放 < 1 时 FBO 比窗口小，视口坐标也要一起缩
    drawBevLayer(bx, by, bw, bh, camera, world, s);
  }

  // ---- 4. 后处理：离屏纹理 -> 屏幕 ----
  drawPost(s);
}

// ============================================================
//  自车行驶轨迹
// ============================================================

void Renderer::recordEgoTrail(const glm::vec3 &pos)
{
  // 世界会循环平移（无限道路），因此轨迹点也要跟着整体平移，
  // 否则世界跳变时轨迹会拉出一条横跨全场的错误连线。
  // 这里通过「与上一个点的距离过大就判定为世界跳变、清空轨迹」来处理。
  if (!m_egoTrail.empty())
  {
    const float d = glm::length(pos - m_egoTrail.back());
    if (d > 30.0f)
    {
      m_egoTrail.clear();
    }
  }

  m_egoTrail.push_back(pos);

  // 环形缓冲：最多保留 600 个点（约 90 秒 @0.15s 采样）
  constexpr size_t kMaxTrailPoints = 600;
  if (m_egoTrail.size() > kMaxTrailPoints)
    m_egoTrail.erase(m_egoTrail.begin(),
                     m_egoTrail.begin() +
                         static_cast<long>(m_egoTrail.size() - kMaxTrailPoints));
}

} // namespace vc
