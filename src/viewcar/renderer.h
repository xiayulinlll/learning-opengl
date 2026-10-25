#ifndef VIEWCAR_RENDERER_H
#define VIEWCAR_RENDERER_H

// ============================================================
//  viewcar - 渲染器
//
//  渲染管线（前向渲染，无延迟着色——车载可视化更看重低延迟与简单性）：
//    1. 天空盒        （程序化渐变，跟随相机）
//    2. 路面          （跟随自车的大平面）
//    3. 街景          （行道树 / 楼房 / 路灯 / 行人 / 护栏……程序化生成）
//    4. 自车模型      （轿车零件装配：车身 + 玻璃 + 车轮 + 车灯）
//    5. 感知目标      （按类别装配车型，批次实例化绘制）
//    6. 感知可视化层  （车道线 / 3D 包围盒 / FOV 锥 / 规划轨迹 / 地面网格）
//    7. 后处理        （暗角 + 轻微色调映射）
//
//  绘制顺序有讲究：先画不透明体并写深度，再画线框（不写深度）叠加在上层，
//  这样线框不会被自身遮挡，同时仍被前方实体遮挡，符合 ADAS 界面的观感。
// ============================================================

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "adas.h"
#include "bev.h"
#include "camera.h"
#include "debugdraw.h"
#include "props.h"
#include "shader.h"

namespace vc
{

// 可视化开关（SimHUD 面板直接控制这些字段）
struct VisualSettings
{
  bool showLaneLines = true;     // 车道线
  bool showBoundingBoxes = true; // 目标 3D 包围盒
  bool showLabels = true;        // 目标标签连线（引线）
  bool showSpeedVector = true;   // 目标速度矢量
  bool showPredictedPath = true; // 目标预测轨迹
  bool showEgoPath = true;       // 自车规划轨迹
  bool showSensorFov = true;     // 传感器 FOV
  bool showDetectionPoints = true; // 目标检测点云（包围盒上的采样点）
  bool showGrid = true;          // 地面网格
  bool showEgoBody = true;       // 自车车身
  bool showTrail = true;         // 自车行驶轨迹
  bool showStreetScene = true;   // 街景（树 / 楼 / 路灯 / 行人等）
  bool showHeadlights = true;    // 车灯自发光（夜间观感更强）

  BevSettings bev;               // BEV + 占据栅格可视化（特斯拉 FSD 风格）

  float streetDensity = 1.0f;    // 街景密度倍数（0.2 ~ 2.0）

  float laneLineWidth = 1.6f;  // 车道线宽度（米）
  float laneHeight = 0.03f;    // 车道线离地高度
  float boxLineScale = 1.0f;   // 包围盒放大系数（1.0 = 紧贴目标）
  float fovAlpha = 0.85f;      // FOV 棱边透明度
  int gridDivisions = 40;      // 网格密度
  float gridExtent = 120.0f;   // 网格半径

  bool enableFog = true;
  float fogDensity = 0.0055f;
  bool enableVignette = true;
  bool enableShadows = true;   // 自车/目标的地面投影（伪阴影）

  // 光照
  glm::vec3 lightDir = glm::vec3(-0.45f, 0.78f, 0.44f);
  glm::vec3 lightColor = glm::vec3(1.0f, 0.96f, 0.90f);
  float lightIntensity = 1.15f;
  glm::vec3 ambientSky = glm::vec3(0.55f, 0.68f, 0.85f);
  glm::vec3 ambientGround = glm::vec3(0.24f, 0.26f, 0.24f);
  float ambientIntensity = 0.75f;
  glm::vec3 fogColor = glm::vec3(0.68f, 0.76f, 0.85f);

  // 天空
  glm::vec3 skyTop = glm::vec3(0.18f, 0.38f, 0.72f);
  glm::vec3 skyHorizon = glm::vec3(0.78f, 0.85f, 0.92f);
  glm::vec3 skyBottom = glm::vec3(0.22f, 0.24f, 0.26f);
};

// 每帧统计（ImGui 显示用）
struct RenderStats
{
  int instanceCount = 0;   // 实例化绘制的目标数
  int lineCount = 0;       // 线段数
  int drawCalls = 0;       // draw call 估算
  int triangleCount = 0;   // 三角形数
  int lanePointCount = 0;  // 车道线采样点数
};

class Renderer
{
public:
  Renderer() = default;
  ~Renderer();

  // 初始化着色器与几何体
  bool init();

  // 窗口尺寸变化时重建后处理帧缓冲
  void resize(int width, int height);

  // 主渲染入口
  void render(const Camera &camera, const AdasWorld &world,
              const VisualSettings &settings);

  const RenderStats &stats() const { return m_stats; }
  int windowWidth() const { return m_width; }
  int windowHeight() const { return m_height; }

  // BEV 栅格统计（ImGui 面板显示构建耗时与占据比例）
  const BevStats &stats_bev() const { return m_bev.stats(); }

  // BEV 引擎（供主循环在参数改变后请求立刻重建栅格）
  BevEngine &bevEngine() { return m_bev; }

  // 读取当前帧缓冲的像素尺寸（用于 ImGui 显示的渲染分辨率）
  float renderScale() const { return m_renderScale; }
  void setRenderScale(float s) { m_renderScale = clampf(s, 0.5f, 1.5f); }

  // 自车行驶轨迹：主循环按固定间隔调用 recordEgoTrail() 采样，Renderer 内部维护环形缓冲
  void recordEgoTrail(const glm::vec3 &pos);
  void clearEgoTrail() { m_egoTrail.clear(); }
  int trailPointCount() const { return static_cast<int>(m_egoTrail.size()); }

  void destroy();

private:
  int m_width = 1280;
  int m_height = 720;
  float m_renderScale = 1.0f;

  // ---- 着色器 ----
  Shader m_sceneShader;   // 实体（自车 / 目标）
  Shader m_lineShader;    // 线段（车道线 / 包围盒 / FOV / 轨迹）
  Shader m_groundShader;  // 路面
  Shader m_skyShader;     // 天空盒

  // ---- 几何体 ----
  ShapeMesh m_cube;      // 单位立方体（自车 / 目标）
  ShapeMesh m_ground;    // 单位平面（路面）
  ShapeMesh m_skyCube;   // 天空盒立方体
  ShapeMesh m_wheel;     // 车轮圆柱
  DebugLines m_lines;    // 所有线段

  PropMeshes m_props;    // 车型化部件 + 街景道具网格库

  BevEngine m_bev;       // BEV 引擎（占据栅格 + 可行驶区域）

  // BEV 场景层的专用线段与板面
  DebugLines m_bevLines; // BEV 正交视图下的线（车道拓扑 / 目标框 / 轨迹）

  // 占据栅格纹理用的全屏板。
  //  不能复用 ShapeMesh：它的顶点布局被固定为「位置(3) + 法线(3)」，
  //  而这里需要「位置(3) + 纹理坐标(2)」，强行复用会让 occupancy.vert
  //  的 aTexCoord 读到法线的后两个分量（视觉上就是纹理完全错乱）。
  unsigned int m_occQuadVao = 0;
  unsigned int m_occQuadVbo = 0;

  // ---- 后处理 ----
  unsigned int m_fbo = 0;
  unsigned int m_colorTex = 0;
  unsigned int m_rbo = 0;
  unsigned int m_quadVao = 0;
  unsigned int m_quadVbo = 0;
  Shader m_postShader;

  // ---- 自车轨迹历史 ----
  std::vector<glm::vec3> m_egoTrail;
  float m_trailTimer = 0.0f;

  RenderStats m_stats;

  // ---- 内部绘制阶段 ----
  bool initShaders();
  bool initGeometry();
  bool initPostProcess();
  void createFramebuffer(int width, int height);

  void drawSky(const Camera &camera, const VisualSettings &s);
  void drawGround(const Camera &camera, const AdasWorld &world, const VisualSettings &s);
  void drawEgo(const Camera &camera, const AdasWorld &world, const VisualSettings &s);
  void drawObjects(const Camera &camera, const AdasWorld &world, const VisualSettings &s);
  void drawStreetScene(const Camera &camera, const AdasWorld &world,
                       const VisualSettings &s);
  void buildDebugLines(const Camera &camera, const AdasWorld &world,
                       const VisualSettings &s);
  void drawPost(const VisualSettings &s);

  // 通用：设置场景着色器的公共 uniform
  void setSceneCommonUniforms(const Camera &camera, const VisualSettings &s);

  // 绘制一个可实例化的实体（不入统计）
  void drawSolid(const Camera &camera, const VisualSettings &s,
                 const glm::mat4 &model, const glm::vec3 &color,
                 float metallic, float roughness, float rim);

  // ---- BEV / 占据栅格图层 ----
  //  drawBevLayer：把 BEV 场景渲染到指定的视口区域（屏幕像素坐标）。
  //    内部会接管 GL 状态（深度测试关闭 / 混合开启），返回前恢复。
  //  bevViewportRect：按布局算出 BEV 应该占据的视口矩形
  //    （x, y, w, h，窗口像素坐标，左下原点）。
  void drawBevLayer(int vx, int vy, int vw, int vh, const Camera &camera,
                    const AdasWorld &world, const VisualSettings &s);

  // 在 BEV 正交视图下构建线段（车道拓扑 / 目标框 / 轨迹 / 米格网）
  void buildBevLines(const AdasWorld &world, const VisualSettings &s);

  // 绘制占据栅格 + 可行驶区域这两张 R32F 纹理（全屏 quad，正交投影）
  void drawOccupancyTextures(const AdasWorld &world, const VisualSettings &s);

  void bevViewportRect(int windowW, int windowH, const BevSettings &b,
                       int &vx, int &vy, int &vw, int &vh) const;

  // ---- 复合模型实例化绘制 ----
  // 收集一批「同一个网格 + 各自模型矩阵 + 各自颜色 + 材质」的实例，
  // 合并成一次 drawInstanced 调用。
  struct InstanceBatch
  {
    ShapeMesh *mesh = nullptr;
    glm::vec3 color = glm::vec3(1.0f);
    float metallic = 0.1f;
    float roughness = 0.6f;
    float rim = 0.3f;
    float emissive = 0.0f;
    std::vector<glm::mat4> transforms;
  };

  // 把一批部件推入批次列表（相同网格+相同材质自动合并）
  void pushPart(std::vector<InstanceBatch> &batches, ShapeMesh *mesh,
                const glm::mat4 &model, const glm::vec3 &color, float metallic,
                float roughness, float rim, float emissive);

  // 提交所有批次（需已 use() 场景着色器并设好公共 uniform）
  void flushBatches(const std::vector<InstanceBatch> &batches);

  // ---- 五类感知目标的部件化装配 ----
  // 把一辆车/一个人拆成若干「网格 + 模型矩阵 + 颜色」部件推入批次
  void buildCarParts(std::vector<InstanceBatch> &out, const glm::vec3 &pos,
                     float yawDeg, float length, float height, float width,
                     const glm::vec3 &paint, float emissive);
  void buildTruckParts(std::vector<InstanceBatch> &out, const glm::vec3 &pos,
                       float yawDeg, float length, float height, float width,
                       const glm::vec3 &cabColor, const glm::vec3 &cargoColor,
                       float emissive);
  void buildPedestrianParts(std::vector<InstanceBatch> &out,
                            const glm::vec3 &pos, float yawDeg, float height,
                            const glm::vec3 &cloth, int variant);
  void buildCyclistParts(std::vector<InstanceBatch> &out, const glm::vec3 &pos,
                         float yawDeg, float height, const glm::vec3 &cloth);
  void buildConeParts(std::vector<InstanceBatch> &out, const glm::vec3 &pos,
                      float yawDeg, float height);
};

} // namespace vc

#endif // VIEWCAR_RENDERER_H
