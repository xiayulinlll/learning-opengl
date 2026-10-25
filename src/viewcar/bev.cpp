#include "bev.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "core.h"

namespace vc
{

namespace
{
const char *kBevShaderDir = "src/viewcar/shader/";

// 目标类别的「占据膨胀半径」：真实感知里目标框要略微膨胀，
// 因为车辆有实际体积、且定位有误差。这里按类别给出不同的
// 安全裕度（行人小、卡车大）。
float inflationOf(ObjectClass c)
{
  switch (c)
  {
  case ObjectClass::Car:
    return 0.30f;
  case ObjectClass::Truck:
    return 0.45f;
  case ObjectClass::Pedestrian:
    return 0.25f;
  case ObjectClass::Cyclist:
    return 0.28f;
  case ObjectClass::Cone:
    return 0.10f;
  default:
    return 0.20f;
  }
}

// 占据值：越危险的目标给越高的权重，栅格热力图就能直接反映风险等级
float severityOf(ObjectClass c)
{
  switch (c)
  {
  case ObjectClass::Truck:
    return 1.00f;
  case ObjectClass::Car:
    return 0.92f;
  case ObjectClass::Cyclist:
    return 0.80f;
  case ObjectClass::Pedestrian:
    return 0.74f;
  case ObjectClass::Cone:
    return 0.55f;
  default:
    return 0.60f;
  }
}
} // namespace

// ============================================================
//  OccupancyGrid
// ============================================================

void OccupancyGrid::resize(const BevSettings &s)
{
  const float cell = (s.cellSize > 0.05f) ? s.cellSize : 0.5f;

  // 栅格尺寸取整：宽 = 左右总跨度，高 = 前后总跨度
  const int w = std::max(4, static_cast<int>((s.rangeSide * 2.0f) / cell));
  const int h =
      std::max(4, static_cast<int>((s.rangeForward + s.rangeBack) / cell));

  // 只有尺寸真的变了才重新分配（避免每帧 vector 重分配）
  if (w == m_width && h == m_height && std::fabs(cell - m_cellSize) < 1e-4f)
    return;

  m_width = w;
  m_height = h;
  m_cellSize = cell;
  m_minX = -s.rangeSide;
  m_minZ = -s.rangeBack;

  m_occupancy.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
  m_drivable.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
}

void OccupancyGrid::clear()
{
  std::fill(m_occupancy.begin(), m_occupancy.end(), 0.0f);
  std::fill(m_drivable.begin(), m_drivable.end(), 0.0f);
}

// 把一个朝向矩形光栅化进栅格。
//
//  实现方式：不做「扫描线多边形填充」，而是取矩形的 AABB 范围逐格
//  判断「格心是否在矩形内」。矩形边长最大约 12 米、格子 0.5 米，
//  单目标最多检查 24 × 24 = 576 格，几十个目标的总体开销可忽略；
//  代码却比通用多边形填充简单得多，且天然支持旋转。
void OccupancyGrid::markOrientedBox(const glm::vec3 &center,
                                    const glm::vec3 &halfSize, float yawDeg,
                                    float value)
{
  if (m_width <= 0 || m_height <= 0)
    return;

  // 自车系坐标：X 横向，Z 纵向（对应栅格的 cx / cy）
  const float cx = center.x;
  const float cz = center.z;

  const float yaw = glm::radians(yawDeg);
  const float c = std::cos(yaw);
  const float s = std::sin(yaw);

  const float hx = halfSize.x;
  const float hz = halfSize.z;

  // 矩形四个角在自车系下的偏移，用来求 AABB
  const glm::vec2 corners[4] = {
      {hx * c + hz * s, -hx * s + hz * c},
      {-hx * c + hz * s, hx * s + hz * c},
      {-hx * c - hz * s, hx * s - hz * c},
      {hx * c - hz * s, -hx * s - hz * c},
  };

  float minDx = corners[0].x, maxDx = corners[0].x;
  float minDz = corners[0].y, maxDz = corners[0].y;
  for (int i = 1; i < 4; ++i)
  {
    minDx = std::min(minDx, corners[i].x);
    maxDx = std::max(maxDx, corners[i].x);
    minDz = std::min(minDz, corners[i].y);
    maxDz = std::max(maxDz, corners[i].y);
  }

  // AABB 转格子范围，并夹到栅格内
  int x0 = static_cast<int>(std::floor((cx + minDx - m_minX) / m_cellSize));
  int x1 = static_cast<int>(std::ceil((cx + maxDx - m_minX) / m_cellSize));
  int y0 = static_cast<int>(std::floor((cz + minDz - m_minZ) / m_cellSize));
  int y1 = static_cast<int>(std::ceil((cz + maxDz - m_minZ) / m_cellSize));

  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(m_width - 1, x1);
  y1 = std::min(m_height - 1, y1);

  // 逐格判断格心是否落在旋转矩形内（把格心转到矩形局部系再比较）
  for (int gy = y0; gy <= y1; ++gy)
  {
    const float pz = m_minZ + (static_cast<float>(gy) + 0.5f) * m_cellSize;

    for (int gx = x0; gx <= x1; ++gx)
    {
      const float px = m_minX + (static_cast<float>(gx) + 0.5f) * m_cellSize;

      const float dx = px - cx;
      const float dz = pz - cz;

      // 自车系 -> 矩形局部系（逆旋转）
      const float lx = dx * c - dz * s;
      const float lz = dx * s + dz * c;

      if (std::fabs(lx) <= hx && std::fabs(lz) <= hz)
      {
        const size_t idx = static_cast<size_t>(gy) * m_width + gx;
        m_occupancy[idx] = std::max(m_occupancy[idx], value);
      }
    }
  }
}

void OccupancyGrid::markDrivableBox(const glm::vec3 &center,
                                    const glm::vec3 &halfSize, float yawDeg,
                                    float value)
{
  if (m_width <= 0 || m_height <= 0)
    return;

  const float cx = center.x;
  const float cz = center.z;
  const float yaw = glm::radians(yawDeg);
  const float c = std::cos(yaw);
  const float s = std::sin(yaw);
  const float hx = halfSize.x;
  const float hz = halfSize.z;

  // 用「外接圆半径」做保守的 AABB（x/z 半尺寸之和），比逐角计算便宜，
  // 对可行驶区域这种「宁大勿小」的语义也更安全。
  const float r = hx + hz;

  int x0 = static_cast<int>(std::floor((cx - r - m_minX) / m_cellSize));
  int x1 = static_cast<int>(std::ceil((cx + r - m_minX) / m_cellSize));
  int y0 = static_cast<int>(std::floor((cz - r - m_minZ) / m_cellSize));
  int y1 = static_cast<int>(std::ceil((cz + r - m_minZ) / m_cellSize));

  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(m_width - 1, x1);
  y1 = std::min(m_height - 1, y1);

  for (int gy = y0; gy <= y1; ++gy)
  {
    const float pz = m_minZ + (static_cast<float>(gy) + 0.5f) * m_cellSize;

    for (int gx = x0; gx <= x1; ++gx)
    {
      const float px = m_minX + (static_cast<float>(gx) + 0.5f) * m_cellSize;

      const float dx = px - cx;
      const float dz = pz - cz;
      const float lx = dx * c - dz * s;
      const float lz = dx * s + dz * c;

      if (std::fabs(lx) <= hx && std::fabs(lz) <= hz)
      {
        const size_t idx = static_cast<size_t>(gy) * m_width + gx;
        m_drivable[idx] = std::max(m_drivable[idx], value);
      }
    }
  }
}

void OccupancyGrid::upload()
{
  if (m_width <= 0 || m_height <= 0)
    return;

  // GL_R32F：单通道 32 位浮点，直接存「占据概率」这种连续量。
  //  不用 GL_R8 是因为要把 0~1 量化到 0~255，热力图的色阶会出现
  //  明显台阶，且低占据值（0.01~0.05 的弱信号）会被截断成 0。
  auto uploadOne = [&](unsigned int &tex, const std::vector<float> &data)
  {
    if (tex == 0)
    {
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
    {
      glBindTexture(GL_TEXTURE_2D, tex);
    }

    // 每行按 4 字节对齐；GL_R32F 每像素 4 字节，天然满足，
    // 但显式设置可以避免别处改动 GL_UNPACK_ALIGNMENT 后出现行错位。
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, m_width, m_height, 0, GL_RED,
                 GL_FLOAT, data.data());
  };

  uploadOne(m_occTex, m_occupancy);
  uploadOne(m_drivableTex, m_drivable);

  glBindTexture(GL_TEXTURE_2D, 0);
}

void OccupancyGrid::destroy()
{
  if (m_occTex)
  {
    glDeleteTextures(1, &m_occTex);
    m_occTex = 0;
  }
  if (m_drivableTex)
  {
    glDeleteTextures(1, &m_drivableTex);
    m_drivableTex = 0;
  }
  m_width = 0;
  m_height = 0;
  m_occupancy.clear();
  m_drivable.clear();
}

// ============================================================
//  BevEngine
// ============================================================

bool BevEngine::init()
{
  if (!m_occShader.loadFromFiles(std::string(kBevShaderDir) + "occupancy.vert",
                                 std::string(kBevShaderDir) + "occupancy.frag"))
    return false;

  logInfo("BEV 引擎初始化完成（占据栅格 + 可行驶区域）");
  return true;
}

void BevEngine::destroy()
{
  m_grid.destroy();
  m_occShader.destroy();
}

void BevEngine::update(const AdasWorld &world, const BevSettings &s)
{
  // 更新率控制：占据栅格重建是 CPU 端逐格计算，
  //  高分辨率 + 每帧重建会成为瓶颈。默认每帧（interval = 1），
  //  interval > 1 时用「每 N 帧更新一次」，视觉上几乎无差别。
  const int interval = std::max(1, s.updateInterval);
  if ((m_frameCounter % interval) != 0)
  {
    ++m_frameCounter;
    return;
  }
  ++m_frameCounter;

  rebuild(world, s);
}

void BevEngine::rebuild(const AdasWorld &world, const BevSettings &s)
{
  const auto t0 = std::chrono::high_resolution_clock::now();

  m_grid.resize(s);
  m_grid.clear();

  // ---- 1. 可行驶区域：先把道路结构铺进去 ----
  rasterizeRoad(world, s);

  // ---- 2. 占据栅格：把所有感知目标投影进栅格 ----
  const EgoVehicle &ego = world.ego();
  const float hRad = glm::radians(ego.heading);
  const float ch = std::cos(hRad);
  const float sh = std::sin(hRad);

  m_stats.objectsInView = 0;

  for (const auto &o : world.objects())
  {
    // 世界坐标 -> 自车坐标系（X 右、Z 前）
    const glm::vec3 d = o.position - ego.position;
    const glm::vec3 local(d.x * ch - d.z * sh, 0.0f, d.x * sh + d.z * ch);

    // 视野外的目标不写入栅格（省算力，也避免栅格边缘出现截断的假目标）
    if (local.z < -s.rangeBack - 5.0f || local.z > s.rangeForward + 5.0f ||
        std::fabs(local.x) > s.rangeSide + 5.0f)
      continue;

    ++m_stats.objectsInView;
    rasterizeObject(o, s, world);
  }

  // ---- 3. 统计 ----
  int occCount = 0;
  int drvCount = 0;
  const auto &occ = m_grid.occupancy();
  const auto &drv = m_grid.drivable();

  for (size_t i = 0; i < occ.size(); ++i)
  {
    if (occ[i] > 0.05f)
      ++occCount;
    if (drv[i] > 0.5f)
      ++drvCount;
  }

  m_stats.gridWidth = m_grid.width();
  m_stats.gridHeight = m_grid.height();
  m_stats.occupiedCells = occCount;
  m_stats.drivableCells = drvCount;
  m_stats.occupancyRatio =
      (drvCount > 0)
          ? (static_cast<float>(occCount) / static_cast<float>(drvCount))
          : 0.0f;

  const auto t1 = std::chrono::high_resolution_clock::now();
  m_stats.buildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

  m_textureDirty = true;
}

// 单个目标 -> 占据栅格
void BevEngine::rasterizeObject(const PerceptionObject &o,
                                const BevSettings &s, const AdasWorld &world)
{
  (void)s;
  const EgoVehicle &ego = world.ego();
  const float hRad = glm::radians(ego.heading);
  const float ch = std::cos(hRad);
  const float sh = std::sin(hRad);

  const glm::vec3 d = o.position - ego.position;
  const glm::vec3 local(d.x * ch - d.z * sh, 0.0f, d.x * sh + d.z * ch);

  // 目标在「自车系」下的朝向 = 目标航向 - 自车航向
  const float localYaw = o.yaw - ego.heading;

  // 膨胀：真实感知里障碍框要往外放一圈安全裕度
  const float inf = inflationOf(o.cls);

  // sizeVec() = (length, height, width)，length 沿车头方向 -> 局部 Z
  const glm::vec3 half(o.width * 0.5f + inf, 0.0f, o.length * 0.5f + inf);

  m_grid.markOrientedBox(local, half, localYaw, severityOf(o.cls));

  // 预测轨迹也加一点低强度的占据（体现「未来会占到这里」）。
  //  这里不依赖目标是否自带轨迹：直接沿速度方向外推 1.5 秒，
  //  与 3D 视图里画预测轨迹的算法保持一致 —— 两处用同一套外推，
  //  才能保证「BEV 里看到的未来位置」和「3D 里看到的虚线」对得上。
  const float speed = o.speed();
  if (speed > 0.5f)
  {
    const float vYaw = glm::radians(o.yaw - ego.heading);
    const glm::vec3 dir(std::sin(vYaw), 0.0f, std::cos(vYaw));

    // 每 0.3 秒一个采样点，共 5 个 -> 覆盖 1.5 秒
    for (int i = 1; i <= 5; ++i)
    {
      const float t = static_cast<float>(i) * 0.3f;
      const glm::vec3 pl = local + dir * (speed * t);

      // 越远的预测越不确定 -> 占据值衰减
      const float decay = 0.34f / (1.0f + static_cast<float>(i) * 0.35f);

      m_grid.markOrientedBox(pl, glm::vec3(1.1f, 0.0f, 1.6f), localYaw, decay);
    }
  }
}

// 道路结构 -> 可行驶区域
//
//  可行驶区域的语义是「自车可以合法行驶的空间」，因此包含：
//    · 所有车道（含对向车道，因为它们也是路面）
//    · 路口铺装区
//  但不包含：人行道、绿化带、建筑占地。
void BevEngine::rasterizeRoad(const AdasWorld &world, const BevSettings &s)
{
  const EgoVehicle &ego = world.ego();

  // 与 adas.cpp 的道路横断面约定保持一致：|X| <= 13.1 为沥青路面
  constexpr float kHalfRoadWidth = 13.1f;

  const float hRad = glm::radians(ego.heading);
  const float ch = std::cos(hRad);
  const float sh = std::sin(hRad);

  // 「世界坐标 -> 自车系」的便捷 lambda
  auto toLocal = [&](const glm::vec3 &wp, float &lx, float &lz)
  {
    const glm::vec3 d = wp - ego.position;
    lx = d.x * ch - d.z * sh;
    lz = d.x * sh + d.z * ch;
  };

  // ---- 1. 主路：沿自车前进方向分段铺（每段 4 米）----
  const float zSpan = s.rangeForward + s.rangeBack;
  const int segs = static_cast<int>(zSpan / 4.0f) + 1;

  for (int i = 0; i < segs; ++i)
  {
    const float worldZ =
        ego.position.z - s.rangeBack + 4.0f * (static_cast<float>(i) + 0.5f);

    float lx = 0.0f, lz = 0.0f;
    toLocal(glm::vec3(0.0f, 0.0f, worldZ), lx, lz);

    m_grid.markDrivableBox(glm::vec3(lx, 0.0f, lz),
                           glm::vec3(kHalfRoadWidth, 0.0f, 2.6f), -ego.heading,
                           1.0f);
  }

  // ---- 2. 横向马路：把路口铺装区也标成可行驶 ----
  for (const auto &cr : world.crossRoads())
  {
    float lx = 0.0f, lz = 0.0f;
    toLocal(glm::vec3(0.0f, 0.0f, cr.centerZ), lx, lz);

    m_grid.markDrivableBox(glm::vec3(lx, 0.0f, lz),
                           glm::vec3(cr.halfWidth, 0.0f, cr.halfDepth),
                           -ego.heading, 1.0f);
  }
}

// ---- BEV 相机 ----
//  正交投影，从上往下看。两种模式：
//    lockToEgo = true   相机跟随自车位置与航向（自车永远朝屏幕上方）
//    lockToEgo = false  相机只跟随自车位置、不跟随航向（世界朝向固定）
glm::mat4 BevEngine::viewMatrix(const AdasWorld &world,
                                const BevSettings &s) const
{
  const glm::vec3 target = world.ego().position;

  // 相机位置：自车正上方 80 米。正交投影下高度不影响成像大小，
  //  只要大于场景最高物体（这里最高的是 ~60 米的楼）即可。
  glm::vec3 eye = target + glm::vec3(0.0f, 80.0f, 0.0f);
  glm::vec3 up(0.0f, 0.0f, 1.0f); // 默认 +Z 朝屏幕上方

  if (s.lockToEgo)
  {
    // 相机随自车航向绕 Y 轴反向旋转，效果是「自车永远朝上」。
    //  注意用 -hRad：相机转了 -h，世界看起来就转了 +h，
    //  也就是自车朝向被"转正"到屏幕上方。
    const float hRad = glm::radians(world.ego().heading);
    const glm::mat4 rot =
        glm::rotate(glm::mat4(1.0f), -hRad, glm::vec3(0.0f, 1.0f, 0.0f));

    eye = target + glm::vec3(rot * glm::vec4(0.0f, 80.0f, 0.0f, 0.0f));
    up = glm::vec3(rot * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
  }

  return glm::lookAt(eye, target, up);
}

glm::mat4 BevEngine::projectionMatrix(const BevSettings &s) const
{
  // 纵向范围不对称：车头方向看得更远（-rangeBack ~ +rangeForward）
  return glm::ortho(-s.rangeSide, s.rangeSide, -s.rangeBack, s.rangeForward,
                    0.1f, 200.0f);
}

glm::vec2 BevEngine::worldToBev(const AdasWorld &world, const BevSettings &s,
                                const glm::vec3 &worldPos) const
{
  const EgoVehicle &ego = world.ego();
  const glm::vec3 d = worldPos - ego.position;

  float lx = d.x;
  float lz = d.z;

  if (s.lockToEgo)
  {
    const float hRad = glm::radians(ego.heading);
    const float c = std::cos(hRad);
    const float sn = std::sin(hRad);
    lx = d.x * c - d.z * sn;
    lz = d.x * sn + d.z * c;
  }

  // 映射到 [0,1]²（左下原点）
  return glm::vec2((lx + s.rangeSide) / (s.rangeSide * 2.0f),
                   (lz + s.rangeBack) / (s.rangeForward + s.rangeBack));
}

} // namespace vc
