#ifndef VIEWCAR_BEV_H
#define VIEWCAR_BEV_H

// ============================================================
//  viewcar - BEV（Bird's Eye View）+ Occupancy Grid 可视化引擎
//
//  为什么需要这个模块：
//    前向 3D 场景（drawStreetScene / drawObjects）回答的是
//    「驾驶者看到什么」；而自动驾驶真正做决策的空间是
//    「以自车为中心的俯视栅格」，也就是 BEV。
//    特斯拉车机上那块「俯视小地图 + 彩色方块车流」就是 BEV 可视化。
//
//  本模块复刻特斯拉 FSD 车机可视化的四类形态：
//    Fullscreen 全屏 BEV      —— 停车 / 低速，占据栅格铺满屏幕
//    PictureInPicture 画中画  —— 行驶中，右下角常驻小地图
//    Split 上下分屏           —— 调试用，3D 与 BEV 同时可见
//    BevMain 全屏 BEV + 小 3D —— FSD v12 风格，BEV 为主画面
//
//  三个核心数据产物（全部由 CPU 每帧从 AdasWorld 采样生成）：
//    1. OccupancyGrid  占据栅格：每个格子被障碍占据的概率 0~1
//    2. DrivableMask   可行驶区域：每个格子是否在车道 / 路口内
//    3. 投影后的目标框 / 轨迹 / 车道拓扑（直接由渲染层读取 AdasWorld）
//
//  坐标约定：
//    BEV 正交相机从上往下看（-Y 方向），视线覆盖
//      X ∈ [-rangeSide, +rangeSide]（横向，米）
//      Z ∈ [-rangeBack, +rangeForward]（纵向，米，+Z 是车头方向）
//    「自车锁定」模式下整个栅格随自车航向旋转，自车永远朝屏幕上方。
// ============================================================

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

#include "adas.h"
#include "debugdraw.h"
#include "shader.h"

namespace vc
{

// ------------------------------ BEV 布局 ------------------------------
// 对应特斯拉车机的四种视觉形态
enum class BevLayout
{
  Fullscreen = 0,   // 全屏 BEV
  PictureInPicture, // 3D 主画面 + 右下角 BEV 小地图
  Split,            // 上下分屏
  BevMain,          // 全屏 BEV + 右下角小 3D
  Count
};

inline const char *nameOfBevLayout(BevLayout l)
{
  switch (l)
  {
  case BevLayout::Fullscreen:
    return "全屏 BEV";
  case BevLayout::PictureInPicture:
    return "画中画 BEV";
  case BevLayout::Split:
    return "上下分屏";
  case BevLayout::BevMain:
    return "BEV 主画面";
  default:
    return "未知";
  }
}

// ------------------------------ BEV 参数 ------------------------------
struct BevSettings
{
  bool enabled = false; // BEV 总开关（F4 切换）
  BevLayout layout = BevLayout::PictureInPicture;

  // ---- 视野范围（米）----
  float rangeForward = 120.0f; // 车头方向可视距离
  float rangeBack = 40.0f;     // 车尾方向可视距离
  float rangeSide = 60.0f;     // 左右各可视距离

  // ---- 占据栅格分辨率（米 / 格）----
  float cellSize = 0.5f;
  int updateInterval = 1; // 每 N 帧重建一次栅格（1 = 每帧）

  // ---- 图层开关 ----
  bool showOccupancy = true;      // 占据栅格热力图
  bool showDrivable = true;       // 可行驶区域填充
  bool showLaneTopology = true;   // 车道拓扑线
  bool showObjects = true;        // 目标框
  bool showPredictions = true;    // 目标预测轨迹
  bool showSensorCoverage = true; // 传感器覆盖范围
  bool showGrid = true;           // 米格网

  bool lockToEgo = true; // 自车朝向锁定（自车永远朝屏幕上方）
  float occupancyOpacity = 0.62f;
  float drivableOpacity = 0.34f;

  // ---- 画中画尺寸（占主视口比例）----
  float pipWidthRatio = 0.26f;
  float pipMarginRatio = 0.02f;
};

// ------------------------------ 占据栅格 ------------------------------
//  栅格的「空间语义」：
//    value = 0   完全空闲（可通行）
//    value → 1   被障碍占据
//  另有一路「可行驶」通道 drivable[]，标记该格是否在道路范围内。
//
//  实现上是两块等大的 float 数组，上传为 GL_R32F 纹理，
//  在 occupancy.frag 里用自研 colormap 渲染成热力图。
class OccupancyGrid
{
public:
  // 按当前分辨率重新分配缓冲，并把世界范围记录下来
  void resize(const BevSettings &s);

  void clear(); // 把所有格子清零

  // 把「自车坐标系」下的一个朝向矩形区域标记为占据。
  //  center/halfSize 单位：米；yawDeg 绕 Y 轴旋转（与目标 yaw 一致）
  void markOrientedBox(const glm::vec3 &center, const glm::vec3 &halfSize,
                       float yawDeg, float value);

  // 与上同理，但把同一个值累加/取最大（可行驶区域用）
  void markDrivableBox(const glm::vec3 &center, const glm::vec3 &halfSize,
                       float yawDeg, float value);

  // ---- 访问器 ----
  int width() const { return m_width; }   // 横向格数
  int height() const { return m_height; } // 纵向格数
  float cellSize() const { return m_cellSize; }
  float minX() const { return m_minX; } // 栅格最左列（自车系 X）
  float minZ() const { return m_minZ; } // 栅格最后行（自车系 Z）

  const std::vector<float> &occupancy() const { return m_occupancy; }
  const std::vector<float> &drivable() const { return m_drivable; }

  std::vector<float> &occupancy() { return m_occupancy; }
  std::vector<float> &drivable() { return m_drivable; }

  // 栅格坐标 -> 世界（自车系）坐标
  glm::vec2 cellCenter(int cx, int cy) const
  {
    return glm::vec2(m_minX + (static_cast<float>(cx) + 0.5f) * m_cellSize,
                     m_minZ + (static_cast<float>(cy) + 0.5f) * m_cellSize);
  }

  // 上传到 GPU 纹理（GL_R32F，单通道浮点，无需归一化到 0~255）
  void upload();

  unsigned int occupancyTexture() const { return m_occTex; }
  unsigned int drivableTexture() const { return m_drivableTex; }

  void destroy();

private:
  int m_width = 0;
  int m_height = 0;
  float m_cellSize = 0.5f;
  float m_minX = 0.0f;
  float m_minZ = 0.0f;

  std::vector<float> m_occupancy;
  std::vector<float> m_drivable;

  unsigned int m_occTex = 0;
  unsigned int m_drivableTex = 0;
};

// ------------------------------ BEV 统计 ------------------------------
struct BevStats
{
  int gridWidth = 0;
  int gridHeight = 0;
  int occupiedCells = 0;      // 被占据的格子数
  int drivableCells = 0;      // 可行驶格子数
  float occupancyRatio = 0.0f; // 占据比例（相对可行驶区域）
  double buildMs = 0.0;        // 本帧栅格重建耗时（毫秒）
  int objectsInView = 0;       // 视野内目标数
};

// ------------------------------ BEV 引擎 ------------------------------
//  职责：
//    1. 从 AdasWorld 采样，生成占据栅格与可行驶区域
//    2. 提供 BEV 正交相机矩阵（支持自车锁定 / 世界锁定）
//    3. 提供世界坐标 -> BEV 视口坐标的投影（供 ImGui 叠加标注用）
class BevEngine
{
public:
  BevEngine() = default;

  bool init();
  void destroy();

  // 每帧调用：按 updateInterval 决定是否重建栅格
  void update(const AdasWorld &world, const BevSettings &s);

  // 强制立即重建（参数改变后调用）
  void rebuild(const AdasWorld &world, const BevSettings &s);

  // ---- 相机 ----
  //  BEV 视图矩阵：正交俯视。lockToEgo 为真时随自车航向旋转
  glm::mat4 viewMatrix(const AdasWorld &world, const BevSettings &s) const;
  glm::mat4 projectionMatrix(const BevSettings &s) const;

  // 世界坐标 -> BEV 视图归一化坐标 [0,1]²（左下为原点），
  //  供 ImGui 在 BEV 画面上叠加文字标注
  glm::vec2 worldToBev(const AdasWorld &world, const BevSettings &s,
                       const glm::vec3 &worldPos) const;

  const OccupancyGrid &grid() const { return m_grid; }
  const BevStats &stats() const { return m_stats; }

  // 把栅格数据上传到 GPU 纹理。
  //  这是「渲染动作」而不是「数据查询」，所以由引擎暴露接口，
  //  而不是让调用方拿到非 const 的 OccupancyGrid 自己去改。
  void uploadGridTextures()
  {
    if (m_textureDirty)
    {
      m_grid.upload();
      m_textureDirty = false;
    }
  }

  Shader &occupancyShader() { return m_occShader; }

  bool textureDirty() const { return m_textureDirty; }

private:
  OccupancyGrid m_grid;
  BevStats m_stats;

  Shader m_occShader;

  int m_frameCounter = 0;
  bool m_textureDirty = true;

  // 单个目标写入占据栅格
  void rasterizeObject(const PerceptionObject &o, const BevSettings &s,
                       const AdasWorld &world);

  // 道路结构写入「可行驶」通道
  void rasterizeRoad(const AdasWorld &world, const BevSettings &s);
};

} // namespace vc

#endif // VIEWCAR_BEV_H
