#include "adas.h"

#include <algorithm>

namespace vc
{

// ============================================================
//  道路横断面约定（全项目唯一的「真值来源」）
//
//  道路为双向 6 车道，中央以双黄线分隔（不是物理隔离带）：
//
//     X < 0  对向车流（-Z 方向）        X > 0  同向车流（+Z 方向）
//     ┌──────┬──────┬──────┬║┬──────┬──────┬──────┐
//     │ 车道3│ 车道2│ 车道1│║│ 车道1│ 车道2│ 车道3│
//     └──────┴──────┴──────┴║┴──────┴──────┴──────┘
//   -13.1  -9.4  -5.6  -1.9  1.9   5.6   9.4  13.1
//
//  自车行驶在同向中间车道（laneIdx = 4，X = +5.625）。
//  这样设计的原因：只有双向分离，才能既有「自车不断超越慢车」的
//  同向车流，又有「对向车迎面掠过」的强视觉冲击。
// ============================================================
namespace
{
constexpr float kLaneWidth = 3.75f;

// 6 条车道的中心 X 坐标：0~2 为对向（-Z），3~5 为同向（+Z）
const float kLaneCenterX[6] = {-9.375f, -5.625f, -1.875f,
                               1.875f,  5.625f,  9.375f};

// 自车所在车道（同向中间的 laneIdx）
constexpr int kEgoLane = 4;

// 路面外沿（含硬路肩）
constexpr float kRoadEdgeX = 13.1f;
constexpr float kCurbX = 13.25f;    // 路缘石中心
constexpr float kCurbWidth = 0.30f;
constexpr float kSidewalkOuterX = 21.0f; // 人行道外沿
constexpr float kWalkBandInner = 14.2f;  // 行人活动带
constexpr float kWalkBandOuter = 19.4f;
constexpr float kBikeLaneX = 13.0f;      // 自行车道（紧贴路缘外侧）

// 十字路口的间距（米）——每走这么远遇到一个路口
constexpr float kCrossRoadSpacing = 220.0f;

// 往返：把 z 对齐到「大于等于 z 的第一个路口中心」
float firstCrossZ(float z)
{
  return std::ceil((z - kCrossRoadSpacing * 0.5f) / kCrossRoadSpacing) *
         kCrossRoadSpacing;
}
} // namespace

// ============================================================
//  PerceptionObject
// ============================================================

void PerceptionObject::updateDerived(const glm::vec3 &egoPos, const glm::vec3 &egoVel,
                                     float egoHeading)
{
  // ---- 直线距离 ----
  distToEgo = glm::length(position - egoPos);

  // ---- 转到自车坐标系 ----
  // 车头方向朝向 +Z，因此以 heading 角构造旋转矩阵的逆变换
  const float hRad = glm::radians(egoHeading);
  const float c = std::cos(hRad);
  const float s = std::sin(hRad);
  const glm::vec3 d = position - egoPos;

  // 逆旋转（自车 -> 世界用 R，世界 -> 自车用 R^T）
  longitudinal = d.x * s + d.z * c; // 前方为正
  lateral = d.x * c - d.z * s;      // 右侧为正

  // ---- TTC：只考虑同向/对向碰撞风险 ----
  // 近似为「纵向相对速度 > 0（正在接近）且目标在前方」
  const float relVz = velocity.z - egoVel.z;
  const float relVx = velocity.x - egoVel.x;

  // 目标必须在前方，否则不构成追尾风险
  if (longitudinal > 0.5f)
  {
    // 相对速度在自车->目标方向上的接近分量
    const float dist = std::sqrt(longitudinal * longitudinal + lateral * lateral);
    if (dist > 1e-3f)
    {
      const float closingSpeed = -(relVx * lateral + relVz * longitudinal) / dist;
      if (closingSpeed > 0.5f)
        ttc = dist / closingSpeed; // 小于阈值即有风险
      else
        ttc = -1.0f;
    }
  }
  else
  {
    ttc = -1.0f;
  }

  // 被跟踪一段时间后视为稳定目标
  tracked = trackAge > 3;
}

// ============================================================
//  EgoVehicle
// ============================================================

void EgoVehicle::update(float dt)
{
  // 纵向速度向目标速度平滑收敛（模拟油门/刹车）
  const float accel = 3.0f; // m/s^2
  if (speed < targetSpeed)
    speed = std::min(speed + accel * dt, targetSpeed);
  else
    speed = std::max(speed - accel * dt, targetSpeed);

  // 自行车模型：航向角以 +Z 为 0，朝 +X 转向为正
  const float hRad = glm::radians(heading);
  position.x += speed * std::sin(hRad) * dt;
  position.z += speed * std::cos(hRad) * dt;

  const float steerRad = glm::radians(steeringAngle);
  if (std::fabs(speed) > 0.01f)
    heading += glm::degrees(speed / wheelBase * std::tan(steerRad) * dt);

  // 航向角归一化到 (-180, 180]
  while (heading > 180.0f)
    heading -= 360.0f;
  while (heading <= -180.0f)
    heading += 360.0f;
}

glm::vec3 EgoVehicle::velocity() const
{
  const float hRad = glm::radians(heading);
  return glm::vec3(speed * std::sin(hRad), 0.0f, speed * std::cos(hRad));
}

glm::vec3 EgoVehicle::forward() const
{
  const float hRad = glm::radians(heading);
  return glm::vec3(std::sin(hRad), 0.0f, std::cos(hRad));
}

glm::vec3 EgoVehicle::center() const
{
  return position + forward() * (length * 0.5f);
}

glm::mat4 EgoVehicle::modelMatrix() const
{
  // 单位立方体 -> 车身尺寸；注意 sizeOf 用 (length, height, width) 顺序
  glm::mat4 m = glm::translate(glm::mat4(1.0f), center());
  m = glm::rotate(m, glm::radians(heading), glm::vec3(0.0f, 1.0f, 0.0f));
  m = glm::scale(m, glm::vec3(width, height, length));
  return m;
}

// ============================================================
//  LaneKeeper
// ============================================================

float LaneKeeper::computeSteering(const EgoVehicle &ego) const
{
  if (!enabled)
    return 0.0f;

  // 预瞄点：自车前方 lookAheadDistance 处的车道中心
  const glm::vec3 ahead = ego.position + ego.forward() * lookAheadDistance;

  // 横向偏差：预瞄点的 X 与车道中心 X 之差
  const float lateralError = ahead.x - laneCenterX;

  // 速度越低转向增益越高（低速时同样转角产生的横向位移更小）
  const float speedFactor = clampf(20.0f / (ego.speed + 8.0f), 0.5f, 1.6f);

  // P 控制输出前轮转角，限幅到 ±25 度（真实车辆前轮转角上限约 30~35 度）
  float steer = -lateralError * gain * speedFactor;
  return clampf(steer, -25.0f, 25.0f);
}

void LaneKeeper::buildLaneLines(std::vector<LaneLine> &out, float length, float startZ,
                                float dashedPhase) const
{
  out.clear();

  // 车道线沿多条纵线分布：laneCount 条车道 -> laneCount+1 条分界线
  const float halfTotal = laneWidth * static_cast<float>(laneCount) * 0.5f;

  for (int i = 0; i <= laneCount; ++i)
  {
    const float x = -halfTotal + laneWidth * static_cast<float>(i);

    LaneLine line;
    line.id = i;
    // 最外侧为道路边界（实线），最内侧为实线，中间为虚线
    if (i == 0 || i == laneCount)
      line.type = LaneType::RoadEdge;
    else
      line.type = LaneType::Dashed;
    line.confidence = 0.98f;

    // 采样：沿 Z 每隔 2 米一个点
    const float step = 2.0f;
    const int samples = static_cast<int>(length / step);

    // 曲线车道：越靠外侧曲率补偿越明显，模拟道路弯道
    const float curveAmp = (x < 0.0f) ? -1.0f : 1.0f;

    for (int s = 0; s <= samples; ++s)
    {
      const float z = startZ + step * static_cast<float>(s);

      // 虚线：按相位做「亮段 / 暗段」的分段
      if (line.type == LaneType::Dashed)
      {
        float seg = std::fmod(z * 0.25f + dashedPhase, 2.0f);
        if (seg < 0.0f)
          seg += 2.0f;
        // 只有亮段才记录点；暗段通过「不连续的点」表达
        if (seg > 1.0f)
        {
          // 暗段：结束当前折线并开一段新的，实现虚线间断
          if (line.points.size() >= 2)
            out.push_back(line);
          line.points.clear();
          continue;
        }
      }

      LanePoint p;
      // 远处的线略微向弯道方向偏移
      const float t = static_cast<float>(s) / static_cast<float>(samples);
      p.position = glm::vec3(x + curveAmp * 0.6f * t * t, 0.02f, z);
      p.curvature = curveAmp;
      line.points.push_back(p);
    }

    if (line.points.size() >= 2)
      out.push_back(line);
  }
}

// ============================================================
//  AdasWorld
// ============================================================

AdasWorld::AdasWorld()
{
}

void AdasWorld::init()
{
  m_ego = EgoVehicle();
  m_ego.position = glm::vec3(0.0f, 0.0f, 0.0f);
  m_ego.speed = 0.0f;        // 从静止起步，能观察到加速过程
  m_ego.targetSpeed = 13.0f; // 约 47 km/h

  m_laneKeeper.laneCenterX = kLaneCenterX[kEgoLane]; // 同向中间车道
  m_laneKeeper.laneWidth = kLaneWidth;
  m_laneKeeper.laneCount = 6; // 双向各 3 条

  // ---- 传感器配置：模拟一套典型的 L2 感知套件 ----
  m_sensors.clear();

  SensorFov frontCam;
  frontCam.name = "前视相机";
  frontCam.origin = glm::vec3(0.0f, 1.25f, 2.20f); // 挡风玻璃上方
  frontCam.hfovDeg = 60.0f;
  frontCam.vfovDeg = 40.0f;
  frontCam.range = 80.0f;
  frontCam.color = glm::vec3(0.30f, 0.80f, 1.00f);
  m_sensors.push_back(frontCam);

  SensorFov frontRadar;
  frontRadar.name = "前向雷达";
  frontRadar.origin = glm::vec3(0.0f, 0.55f, 2.35f);
  frontRadar.hfovDeg = 20.0f;
  frontRadar.vfovDeg = 8.0f;
  frontRadar.range = 160.0f;
  frontRadar.color = glm::vec3(1.00f, 0.55f, 0.20f);
  m_sensors.push_back(frontRadar);

  SensorFov lidar;
  lidar.name = "激光雷达";
  lidar.origin = glm::vec3(0.0f, 1.90f, 0.30f); // 车顶
  lidar.hfovDeg = 360.0f;
  lidar.vfovDeg = 40.0f;
  lidar.range = 120.0f;
  lidar.color = glm::vec3(0.45f, 1.00f, 0.55f);
  m_sensors.push_back(lidar);

  buildScene();
}

void AdasWorld::buildScene()
{
  m_objects.clear();
  m_props.clear();
  m_crossRoads.clear();
  m_nextId = 1;
  m_dashedPhase = 0.0f;
  m_spawnTimer = 0.0f;
  m_worldTime = 0.0f;

  // 车道线：覆盖自车前后 250 米
  m_laneKeeper.buildLaneLines(m_lanes, 250.0f, -60.0f, m_dashedPhase);

  // ---- 道路网络：铺设前方若干十字路口 ----
  // 路口从 z = -40 之前开始铺，保证自车出生点附近就有路口可看
  {
    const int nCross = 4;
    float z = firstCrossZ(-40.0f);
    for (int i = 0; i < nCross; ++i, z += kCrossRoadSpacing)
    {
      CrossRoad cr;
      cr.centerZ = z;
      cr.halfDepth = 9.0f;
      cr.halfWidth = kSidewalkOuterX + 2.0f; // 横向马路比主路略宽
      cr.variant = (i * 7) & 0xFFFF;
      m_crossRoads.push_back(cr);
    }

    logInfo("道路网络: " + std::to_string(m_crossRoads.size()) + " 个十字路口");
  }

  // 街景：铺开自车前方 40 米到 260 米的道路两侧
  buildStreetScene(-40.0f, 260.0f);

  logInfo("街景已生成: 共 " + std::to_string(m_props.size()) + " 个道具（行道树 " +
          std::to_string(propCountOfKind(static_cast<int>(PropKind::Tree))) +
          " / 楼房 " +
          std::to_string(propCountOfKind(static_cast<int>(PropKind::Building))) +
          " / 路灯 " +
          std::to_string(propCountOfKind(static_cast<int>(PropKind::StreetLamp))) +
          " / 行人 " +
          std::to_string(propCountOfKind(static_cast<int>(PropKind::Pedestrian))) +
          "）");

  // ---- 初始交通流：同向慢车（会被自车超越）+ 对向车（迎面掠过）----
  // 同向三车道各放几辆，速度明显低于自车巡航速度，制造「不断超车」的观感
  spawnTrafficAt(3, 30.0f);
  spawnTrafficAt(4, 48.0f);
  spawnTrafficAt(4, 92.0f);
  spawnTrafficAt(5, 42.0f);
  spawnTrafficAt(5, 120.0f);

  // 对向车流：从前方远处驶来，相对速度 20 m/s 以上
  spawnTrafficAt(2, 60.0f);
  spawnTrafficAt(1, 130.0f);
  spawnTrafficAt(2, 200.0f);
  spawnTrafficAt(0, 170.0f);

  // 侧后方同向车：让自车后视镜里也有车（起步时被后车逼近）
  spawnTrafficAt(4, -35.0f);
  spawnTrafficAt(3, -60.0f);

  // 过马路的行人 / 骑行者
  spawnCrossingTraffic(150.0f);
  spawnCrossingTraffic(265.0f);

  updatePerception();
}

// ============================================================
//  街景生成
//
//  道路布局（与车道线约定一致）：
//    3 条车道，中心分别在 X = -3.75 / 0 / +3.75，车道宽 3.75
//    => 路面外沿约 |X| = 5.6（含硬路肩）
//    => 人行道 6.0 ~ 11.0
//    => 绿化带 11.0 ~ 16.0（行道树 / 路灯 / 电线杆）
//    => 建筑区 18.0 ~ 60.0
//
//  所有道具沿 Z 轴按固定间隔 + 随机抖动铺开，形成自然的街道节奏。
//  世界向前循环平移时，这些坐标也跟着减 offset，
//  再由 buildStreetScene 重新铺一遍，就得到「无限街道」。
// ============================================================

namespace
{
// 稳定的伪随机：同一个 (i, salt) 永远得到同一个 [0,1) 值。
// 用它而不是 std::rand()，是为了让街景在重建后保持完全一致 ——
// 否则每次世界循环平移，楼房和树都会「重新长一遍」，出现闪烁跳变。
float hash01(int i, int salt)
{
  unsigned int x = static_cast<unsigned int>(i) * 374761393u +
                   static_cast<unsigned int>(salt) * 668265263u;
  x = (x ^ (x >> 13)) * 1274126177u;
  x = x ^ (x >> 16);
  return static_cast<float>(x & 0xFFFFFFu) / 16777216.0f;
}

// 便捷：在 [lo, hi) 区间取一个稳定随机值
float hashRange(int i, int salt, float lo, float hi)
{
  return lo + (hi - lo) * hash01(i, salt);
}
} // namespace

int AdasWorld::propCountOfKind(int kind) const
{
  int n = 0;
  for (const auto &p : m_props)
    if (p.kind == kind)
      ++n;
  return n;
}

void AdasWorld::buildStreetScene(float zFrom, float zTo)
{
  m_props.clear();

  // 每个「跨距」生成一组街景元素；10 米兼顾密度与道具总数。
  // 密度倍数 >1 时跨距变小（更密），<1 时变大（更疏）。
  const float density = (streetDensity > 0.05f) ? streetDensity : 1.0f;
  const float kSpan = 10.0f / density;
  const int spanCount =
      static_cast<int>((zTo - zFrom) / kSpan) + 1;

  for (int s = 0; s < spanCount; ++s)
  {
    const float z = zFrom + static_cast<float>(s) * kSpan;

    for (int side = -1; side <= 1; side += 2)
    {
      const float sx = static_cast<float>(side);
      const int salt = s * 131 + (side + 1) * 17;

      // ---------- 行道树：绿化带上每跨距一棵 ----------
      //  绿化带位于人行道之外（|X| = 22~27），紧贴人行道外沿。
      {
        const float treeH = hashRange(salt, 3, 5.0f, 8.5f);

        StreetProp t;
        t.kind = static_cast<int>(PropKind::Tree);
        t.position = glm::vec3(sx * hashRange(salt, 2, 22.0f, 26.5f), 0.0f,
                               z + hashRange(salt, 1, -2.2f, 2.2f));
        t.yaw = hashRange(salt, 4, 0.0f, 360.0f);
        t.width = treeH * hashRange(salt, 5, 0.55f, 0.80f);
        t.height = treeH;
        t.depth = t.width;
        t.variant = (s * 7 + (side + 1)) & 0xFFFF;
        m_props.push_back(t);
      }

      // ---------- 灌木 / 花坛：人行道外沿的小尺度点缀 ----------
      if (hash01(salt, 6) > 0.45f)
      {
        StreetProp b;
        b.kind = static_cast<int>(PropKind::Bush);
        b.position = glm::vec3(sx * hashRange(salt, 7, 20.4f, 22.4f), 0.0f,
                               z + hashRange(salt, 8, -3.0f, 3.0f));
        b.yaw = hashRange(salt, 9, 0.0f, 360.0f);
        b.width = hashRange(salt, 10, 1.4f, 2.6f);
        b.height = hashRange(salt, 11, 0.7f, 1.3f);
        b.depth = b.width * hashRange(salt, 12, 0.8f, 1.2f);
        b.variant = (s * 13 + side) & 0xFFFF;
        m_props.push_back(b);
      }

      // ---------- 路灯：每 3 个跨距（30 米）一根，悬臂朝路面 ----------
      //  杆位于人行道靠路缘一侧（|X| ≈ 14.5），悬臂把灯头送到车道上方。
      if (s % 3 == 0)
      {
        StreetProp lamp;
        lamp.kind = static_cast<int>(PropKind::StreetLamp);
        lamp.position = glm::vec3(sx * hashRange(salt, 13, 14.2f, 15.0f), 0.0f,
                                  z + 2.0f);
        lamp.width = 0.22f;
        lamp.height = hashRange(salt, 14, 9.0f, 10.5f);
        lamp.depth = 0.22f;
        lamp.flip = (side < 0); // 悬臂指向道路中心
        lamp.variant = s;
        m_props.push_back(lamp);
      }

      // ---------- 电线杆：每 5 个跨距一根 ----------
      if (s % 5 == 2)
      {
        StreetProp pole;
        pole.kind = static_cast<int>(PropKind::UtilityPole);
        pole.position = glm::vec3(sx * hashRange(salt, 15, 18.5f, 20.0f), 0.0f,
                                  z - 1.0f);
        pole.yaw = hashRange(salt, 16, -8.0f, 8.0f);
        pole.width = 0.20f;
        pole.height = hashRange(salt, 17, 9.0f, 11.5f);
        pole.depth = 0.20f;
        pole.variant = s;
        m_props.push_back(pole);
      }

      // ---------- 楼房：每 2 个跨距一栋，越远离道路越高 ----------
      //  建筑退线（setback）从 27 米起退到 62 米 ——
      //  这样近处不会被楼填满，「街道感」与「城市纵深」同时保留。
      if (s % 2 == 0)
      {
        const float distFromRoad = hashRange(salt, 18, 27.0f, 62.0f);
        const float hMin = 10.0f + distFromRoad * 0.32f;
        const float hMax = 22.0f + distFromRoad * 0.90f;

        StreetProp b;
        b.kind = static_cast<int>(PropKind::Building);
        b.position = glm::vec3(sx * distFromRoad, 0.0f,
                               z + hashRange(salt, 19, -4.0f, 4.0f));
        b.yaw = hashRange(salt, 20, -12.0f, 12.0f);
        b.width = hashRange(salt, 21, 10.0f, 19.0f);
        b.height = hashRange(salt, 22, hMin, hMax);
        b.depth = hashRange(salt, 23, 10.0f, 19.0f);
        b.variant = (s * 3 + side) & 0xFFFF;
        m_props.push_back(b);
      }

      // ---------- 行人：人行道活动带上，会来回走动 ----------
      {
        const int pedCount = (s % 4 == 0) ? 2 : (hash01(salt, 24) > 0.72f ? 1 : 0);
        for (int p = 0; p < pedCount; ++p)
        {
          const int psalt = salt * 31 + p * 7;

          StreetProp ped;
          ped.kind = static_cast<int>(PropKind::Pedestrian);
          ped.position =
              glm::vec3(sx * hashRange(psalt, 25, kWalkBandInner + 0.6f,
                                       kWalkBandOuter - 0.6f),
                        0.0f, z + hashRange(psalt, 26, -3.5f, 3.5f));
          // 多数沿街向前走（+Z），少数逆向
          ped.yaw = hash01(psalt, 27) > 0.35f ? 0.0f : 180.0f;
          ped.height = hashRange(psalt, 28, 1.55f, 1.86f);
          ped.width = ped.height * 0.30f;
          ped.depth = ped.height * 0.22f;
          ped.variant = (s * 5 + p) & 0xFFFF;
          m_props.push_back(ped);
        }
      }

      // ---------- 骑行者：沿自行车道（紧贴路缘外侧）骑行 ----------
      //  只出现在右侧（+X），与右行交通规则一致。
      if (side > 0 && hash01(salt, 29) > 0.62f)
      {
        StreetProp cyc;
        cyc.kind = static_cast<int>(PropKind::Cyclist);
        cyc.position = glm::vec3(hashRange(salt, 30, 12.4f, 13.0f), 0.0f,
                                 z + hashRange(salt, 31, -3.0f, 3.0f));
        cyc.yaw = 0.0f; // 骑行方向与同向车流一致
        cyc.height = hashRange(salt, 32, 1.62f, 1.80f);
        cyc.width = cyc.height * 0.34f;
        cyc.depth = cyc.height * 1.05f;
        cyc.variant = (s * 11 + 5) & 0xFFFF;
        m_props.push_back(cyc);
      }

      // ---------- 路边停车：贴着右侧路缘石停放 ----------
      if (side > 0 && hash01(salt, 33) > 0.86f)
      {
        StreetProp car;
        car.kind = static_cast<int>(PropKind::ParkedCar);
        car.position = glm::vec3(hashRange(salt, 34, 11.4f, 12.2f), 0.0f,
                                 z + hashRange(salt, 35, -3.5f, 3.5f));
        car.yaw = 0.0f;
        car.height = 1.50f;
        car.width = 1.80f;
        car.depth = 4.60f;
        car.variant = (s * 17 + 3) & 0xFFFF;
        m_props.push_back(car);
      }
    }

    // ---------- 护栏：只在中央分隔带两侧排布 ----------
    //  真实城市主干道不会在路缘石处装护栏（那样行人无法上下车），
    //  护栏属于「中央分隔」，所以放在双黄线两侧。
    if (s % 2 == 0)
    {
      for (int side = -1; side <= 1; side += 2)
      {
        StreetProp rail;
        rail.kind = static_cast<int>(PropKind::GuardRail);
        rail.position = glm::vec3(static_cast<float>(side) * 0.85f, 0.0f, z);
        rail.width = 0.12f;
        rail.height = 0.80f;
        rail.depth = 10.0f;
        rail.variant = s;
        m_props.push_back(rail);
      }
    }
  }

  // ---------- 道路网络：路口铺装 ----------
  //  路口不属于「跨距」节奏，单独按 z 范围铺。
  for (const auto &cr : m_crossRoads)
  {
    if (cr.centerZ + cr.halfDepth + 8.0f < zFrom ||
        cr.centerZ - cr.halfDepth - 8.0f > zTo)
      continue;
    buildCrossRoad(cr);
  }
}

// 用稳定哈希替代 std::rand()：让场景重建后交通流完全可复现，
// 也便于调试「为什么某一帧没有车」这类问题。
// 这里的 counter 只是让同一 lane/位置的多次调用值不同。
void AdasWorld::spawnTrafficAt(int laneIdx, float z)
{
  if (laneIdx < 0)
    laneIdx = 0;
  if (laneIdx > 5)
    laneIdx = 5;

  const bool oncoming = (laneIdx < 3); // 0~2 为对向车道
  const int salt = m_nextId * 977 + laneIdx * 31;

  // 随机类别：以轿车为主，少量卡车；对向车流不放骑行者
  const float r = hash01(salt, 1);
  ObjectClass cls = ObjectClass::Car;
  if (r > 0.86f)
    cls = ObjectClass::Truck;

  const glm::vec3 size = sizeOf(cls);

  PerceptionObject o;
  o.id = m_nextId++;
  o.cls = cls;
  o.position = glm::vec3(kLaneCenterX[laneIdx], size.y * 0.5f, z);

  // 对向车头朝 -Z（yaw = 180），同向车头朝 +Z（yaw = 0）
  o.yaw = oncoming ? 180.0f : 0.0f;

  // ---- 速度策略 ----
  // 同向：6~10 m/s，明显低于自车巡航速度（13），保证自车持续超车；
  // 对向：8~16 m/s，与自车相对速度 21~29 m/s，掠过感很强。
  float v;
  if (oncoming)
    v = hashRange(salt, 2, 8.0f, 16.0f);
  else
    v = hashRange(salt, 2, 6.0f, 10.0f);

  o.velocity = glm::vec3(0.0f, 0.0f, oncoming ? -v : v);

  o.length = size.x;
  o.height = size.y;
  o.width = size.z;
  o.confidence = hashRange(salt, 3, 0.80f, 0.99f);
  o.trackAge = 10 + static_cast<int>(hash01(salt, 4) * 50.0f);
  o.tracked = true;

  m_objects.push_back(o);
}

// 生成一个「横穿马路」的目标：行人沿 +X 或 -X 走，速度 1.2~3.5 m/s。
// 它会在路口 / 路段中段出现，产生「侧面来车（人）」这种最考验
// 感知的横向冲突场景。
void AdasWorld::spawnCrossingTraffic(float z)
{
  const int salt = m_nextId * 613 + 77;

  // 六成概率行人，四成骑行者（骑行者横穿速度更快）
  const bool pedestrian = hash01(salt, 1) < 0.60f;
  ObjectClass cls = pedestrian ? ObjectClass::Pedestrian : ObjectClass::Cyclist;
  const glm::vec3 size = sizeOf(cls);

  // 从左侧或右侧人行道起步，横穿整个道路
  const bool fromLeft = hash01(salt, 2) < 0.5f;
  const float startX = fromLeft ? -kWalkBandOuter : kWalkBandOuter;

  PerceptionObject o;
  o.id = m_nextId++;
  o.cls = cls;
  o.position = glm::vec3(startX, size.y * 0.5f, z);
  // 朝 +X 走时 yaw = 90，朝 -X 走时 yaw = -90
  o.yaw = fromLeft ? 90.0f : -90.0f;

  const float v = pedestrian ? hashRange(salt, 3, 1.1f, 1.7f)
                             : hashRange(salt, 3, 2.8f, 4.2f);
  o.velocity = glm::vec3(fromLeft ? v : -v, 0.0f, 0.0f);

  o.length = size.x;
  o.height = size.y;
  o.width = size.z;
  o.confidence = hashRange(salt, 4, 0.72f, 0.94f);
  o.trackAge = 8 + static_cast<int>(hash01(salt, 5) * 30.0f);
  o.tracked = true;

  m_objects.push_back(o);
}

void AdasWorld::update(float dt)
{
  if (!simulationRunning)
    return;

  dt *= timeScale;
  if (dt <= 0.0f)
    return;

  // ---- 1. 车道保持：计算前轮转角 ----
  m_ego.steeringAngle = m_laneKeeper.computeSteering(m_ego);

  // ---- 2. 纵向控制：根据前车距离限制目标速度（简易 ACC）----
  //  关键：只统计「同向」目标（velocity.z > 0）。
  //  对向车虽然也满足 longitudinal > 2（在前方），但相对速度是接近的，
  //  若把它们算进来，自车会误判为「前车很近」而持续刹车 ——
  //  这正是之前「路上没有车擦身而过」的根因之一。
  float minLeadDist = 1e9f;
  for (const auto &o : m_objects)
  {
    if (o.velocity.z <= 0.0f)
      continue; // 对向车 / 横穿目标不参与跟车
    if (o.longitudinal > 2.0f && std::fabs(o.lateral) < 2.2f)
      minLeadDist = std::min(minLeadDist, o.longitudinal - o.length * 0.5f);
  }

  // 期望跟车距离 = 2 秒时距 + 8 米安全余量
  const float desired = 2.0f * m_ego.speed + 8.0f;
  if (minLeadDist < desired)
    m_ego.targetSpeed = std::max(0.0f, m_ego.speed - 6.0f); // 减速
  else
    m_ego.targetSpeed = 13.0f; // 恢复巡航速度

  // ---- 3. 自车运动学积分 ----
  m_ego.update(dt);

  // ---- 4. 交通目标运动 ----
  for (auto &o : m_objects)
  {
    o.position += o.velocity * dt;
    o.trackAge++;
  }

  // ---- 4.1 街景行人的走动 / 骑行者骑行 / 信号灯相位 ----
  //  行人在人行道上来回走（到端头折返），骑行者沿自行车道单向骑行。
  //  因为街景会整体跟着世界循环平移，行人速度叠加效应由平移统一处理。
  m_worldTime += dt;
  updateStreetLife(dt);

  // ---- 5. 虚线动画相位：让虚线向前滚动，产生运动感 ----
  m_dashedPhase += dt * m_ego.speed * 0.06f;

  // ---- 6. 无限道路：自车前进超过阈值后把整个世界平移回起点 ----
  // 可视化 demo 的常用技巧：不生成无限地图，而是循环利用已生成的区域
  const float recycleThreshold = 200.0f;
  if (m_ego.position.z > recycleThreshold)
  {
    // 使用局部变量避免与 std::shift 命名冲突
    const float offset = recycleThreshold;
    m_ego.position.z -= offset;
    for (auto &o : m_objects)
      o.position.z -= offset;

    // 街景也整体后移，再重新铺一遍前方的路段 ——
    //  由于 hash01() 是稳定伪随机，重建后同一位置的道具完全相同，
    //  因此不会出现「楼房凭空重排」的跳变
    for (auto &p : m_props)
      p.position.z -= offset;

    // 丢掉已经落到自车后方的道具，并向前补足。
    //  注意：这里只做「删除」，补铺交给下面的统一补铺逻辑 ——
    //  早期版本删除后立即 rebuild，会出现新旧两段道具重叠。
    m_props.erase(std::remove_if(m_props.begin(), m_props.end(),
                                 [](const StreetProp &p)
                                 { return p.position.z < -80.0f; }),
                  m_props.end());

    // 道路网络（路口）同样平移；与街景不同的是，
    //  路口必须「平移 + 端头补铺」才能保持周期连续，
    //  否则自车会看到路口突然消失。
    for (auto &cr : m_crossRoads)
      cr.centerZ -= offset;

    recycleRoadNetwork();
  }

  // ---- 7. 回收落在自车后方过远的目标 ----
  const float cullZ = m_ego.position.z - 80.0f;
  m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
                                 [cullZ](const PerceptionObject &o)
                                 { return o.position.z < cullZ; }),
                  m_objects.end());

  // 补充新目标，维持一定的交通密度。
  //  生成窗口从「仅前方」扩大到「后方 60 米 ~ 前方 200 米」，
  //  这样既能看到被自己超越的同向慢车，也能一直有对向车迎面驶来。
  m_spawnTimer += dt;
  if (m_spawnTimer > 1.2f && m_objects.size() < 26)
  {
    m_spawnTimer = 0.0f;

    const int salt = static_cast<int>(m_worldTime * 60.0f) + m_nextId * 17;

    // 随机车道：对向 0~2 与同向 3~5 各占一半概率
    int lane = static_cast<int>(hash01(salt, 1) * 6.0f);
    if (lane > 5)
      lane = 5;

    // 生成位置：对向车要出现在「前方」（它会朝自车开来），
    //  同向车可以出现在侧后方（自车会追上它）。
    float z;
    if (lane < 3)
      z = m_ego.position.z + hashRange(salt, 2, 120.0f, 200.0f);
    else
      z = m_ego.position.z + hashRange(salt, 3, -60.0f, 180.0f);

    // 避免与已有同车道目标重叠（重叠会看到「车叠车」）
    bool tooClose = false;
    for (const auto &o : m_objects)
    {
      if (std::fabs(o.position.x - kLaneCenterX[lane]) < 2.0f &&
          std::fabs(o.position.z - z) < 14.0f)
      {
        tooClose = true;
        break;
      }
    }
    if (!tooClose)
      spawnTrafficAt(lane, z);
  }

  // 横穿目标（行人 / 骑行者过马路）：较低频率出现
  m_crossTimer += dt;
  if (m_crossTimer > 6.0f)
  {
    m_crossTimer = 0.0f;
    const int salt = static_cast<int>(m_worldTime * 37.0f) + 991;
    spawnCrossingTraffic(m_ego.position.z + hashRange(salt, 1, 60.0f, 190.0f));
  }

  // 街景补充：前方 120 米以外若无道具，就补铺一段。
  // 判断「该位置是否已经有道具」用的是最靠近的 Z —— 简单且足够。
  {
    float maxPropZ = -1e9f;
    for (const auto &p : m_props)
      maxPropZ = std::max(maxPropZ, p.position.z);

    const float needed = m_ego.position.z + 240.0f;
    if (maxPropZ < needed)
    {
      // 从「现有最远处」接着铺到目标远处；
      // 起始位置对齐到 10 米网格，保证与已有道具的节奏一致
      const float from = std::ceil((maxPropZ - 2.0f) / 10.0f) * 10.0f;
      std::vector<StreetProp> keep;
      keep.swap(m_props);
      buildStreetScene(from, needed);
      // 合并：保留旧道具，再接上新铺的一段
      std::vector<StreetProp> merged = keep;
      merged.insert(merged.end(), m_props.begin(), m_props.end());
      m_props.swap(merged);
    }
  }

  // ---- 8. 重建车道线（跟随自车位置，实现「无限道路」）----
  m_laneKeeper.buildLaneLines(m_lanes, 250.0f, m_ego.position.z - 60.0f, m_dashedPhase);

  // ---- 9. 生成规划轨迹：沿当前航向与转向做圆弧外推 ----
  m_ego.plannedPath.clear();
  const glm::vec3 start = m_ego.center();
  const float hRad = glm::radians(m_ego.heading);
  const float yawRateDeg = glm::degrees(m_ego.speed / m_ego.wheelBase *
                                        std::tan(glm::radians(m_ego.steeringAngle)));

  for (int i = 1; i <= 25; ++i)
  {
    const float t = static_cast<float>(i) * 0.2f; // 每 0.2 秒一个采样点
    const float ang = hRad + glm::radians(yawRateDeg) * t;
    const float arc = m_ego.speed * t; // 弧长近似
    m_ego.plannedPath.push_back(start + glm::vec3(std::sin(ang) * arc, 0.0f,
                                                  std::cos(ang) * arc));
  }

  updatePerception();
}

void AdasWorld::updatePerception()
{
  const glm::vec3 egoPos = m_ego.position;
  const glm::vec3 egoVel = m_ego.velocity();

  m_riskyCount = 0;
  m_nearestDist = 0.0f;
  bool first = true;

  for (auto &o : m_objects)
  {
    o.updateDerived(egoPos, egoVel, m_ego.heading);
    if (o.ttc > 0.0f && o.ttc < 4.0f)
      ++m_riskyCount;

    if (first || o.distToEgo < m_nearestDist)
    {
      m_nearestDist = o.distToEgo;
      first = false;
    }
  }
}

int AdasWorld::objectCountOfClass(ObjectClass c) const
{
  int n = 0;
  for (const auto &o : m_objects)
    if (o.cls == c)
      ++n;
  return n;
}

// ============================================================
//  道路网络：十字路口 / 高架桥
// ============================================================

const CrossRoad *AdasWorld::crossRoadAt(float z) const
{
  for (const auto &cr : m_crossRoads)
  {
    if (z > cr.centerZ - cr.halfDepth && z < cr.centerZ + cr.halfDepth)
      return &cr;
  }
  return nullptr;
}

float AdasWorld::signalPhaseAt(float z) const
{
  // 找最近的路口（信号灯通常就装在路口边上）
  const CrossRoad *best = nullptr;
  float bestDist = 1e9f;

  for (const auto &cr : m_crossRoads)
  {
    const float d = std::fabs(cr.centerZ - z);
    if (d < bestDist)
    {
      bestDist = d;
      best = &cr;
    }
  }

  // 附近 40 米内没有路口就当作「无信号控制」，返回红灯相位
  if (!best || bestDist > 40.0f)
    return 0.5f;

  return best->lightPhase;
}

// 铺一个十字路口：横向路面 + 四条斑马线 + 停止线 + 四个角的信号灯。
//
//  坐标回顾（见文件顶部横断面约定）：
//    主路沿 Z 方向，|X| <= kRoadEdgeX
//    横向马路沿 X 方向，|Z - centerZ| <= halfDepth
void AdasWorld::buildCrossRoad(const CrossRoad &cr)
{
  const int salt = cr.variant * 211;

  // ---------- 横向路面：铺满整个路口的大平板 ----------
  //  用 CrossBox 道具表示，渲染时画成水平大板；
  //  Y 方向略微抬高以避开与主路面的 z-fighting。
  {
    StreetProp p;
    p.kind = static_cast<int>(PropKind::CrossBox);
    p.position = glm::vec3(0.0f, 0.0f, cr.centerZ);
    p.width = cr.halfWidth * 2.0f; // X 方向长度
    p.height = 0.02f;              // 铺装厚度
    p.depth = cr.halfDepth * 2.0f; // Z 方向宽度
    p.variant = cr.variant;
    m_props.push_back(p);
  }

  // ---------- 斑马线：横跨主路（路口前后各一条）----------
  for (int end = -1; end <= 1; end += 2)
  {
    const float z = cr.centerZ + static_cast<float>(end) * (cr.halfDepth + 2.4f);
    StreetProp p;
    p.kind = static_cast<int>(PropKind::Zebra);
    p.position = glm::vec3(0.0f, 0.0f, z);
    p.width = kRoadEdgeX * 2.0f + 0.4f;
    p.height = 0.012f;
    p.depth = 4.6f;
    p.variant = (cr.variant + end * 3) & 0xFFFF;
    m_props.push_back(p);
  }

  // ---------- 斑马线：横跨横向马路（路口左右各一条）----------
  //  这样从任何角度看路口，都能看到「四边都有斑马线」的真实布局。
  for (int side = -1; side <= 1; side += 2)
  {
    StreetProp p;
    p.kind = static_cast<int>(PropKind::Zebra);
    p.position = glm::vec3(static_cast<float>(side) * (kRoadEdgeX + 2.4f), 0.0f,
                           cr.centerZ);
    p.yaw = 90.0f; // 旋转 90 度，条纹方向随之转成沿 Z
    p.width = cr.halfDepth * 2.0f + 0.4f;
    p.height = 0.012f;
    p.depth = 4.6f;
    p.variant = (cr.variant + side * 5) & 0xFFFF;
    m_props.push_back(p);
  }

  // ---------- 停止线：同向车流在路口前的白色实线 ----------
  for (int lane = 3; lane <= 5; ++lane)
  {
    StreetProp p;
    p.kind = static_cast<int>(PropKind::StopLine);
    p.position =
        glm::vec3(kLaneCenterX[lane], 0.0f, cr.centerZ - cr.halfDepth - 5.2f);
    p.width = kLaneWidth * 0.86f;
    p.height = 0.011f;
    p.depth = 0.45f;
    p.variant = lane;
    m_props.push_back(p);
  }

  // 对向车流的停止线在路口另一侧
  for (int lane = 0; lane <= 2; ++lane)
  {
    StreetProp p;
    p.kind = static_cast<int>(PropKind::StopLine);
    p.position =
        glm::vec3(kLaneCenterX[lane], 0.0f, cr.centerZ + cr.halfDepth + 5.2f);
    p.width = kLaneWidth * 0.86f;
    p.height = 0.011f;
    p.depth = 0.45f;
    p.variant = lane;
    m_props.push_back(p);
  }

  // ---------- 信号灯：路口四个角，悬臂伸向路口内 ----------
  //  自车方向（同向）的灯装在右侧人行道上（X > 0），
  //  对向的灯装在左侧；每侧前后各一根，形成典型的路口灯组。
  for (int side = -1; side <= 1; side += 2)
  {
    for (int end = -1; end <= 1; end += 2)
    {
      StreetProp p;
      p.kind = static_cast<int>(PropKind::TrafficLight);
      p.position =
          glm::vec3(static_cast<float>(side) * (kRoadEdgeX + 1.3f), 0.0f,
                    cr.centerZ + static_cast<float>(end) * (cr.halfDepth + 3.4f));
      // 悬臂朝向路口中心：右侧灯（side=+1）伸向 -X，左侧灯伸向 +X
      p.flip = (side < 0);
      p.height = hashRange(salt + end * 7, 1, 6.2f, 7.0f);
      p.width = 0.20f;
      p.depth = 0.20f;
      p.variant = (cr.variant + end * 11 + side * 3) & 0xFFFF;
      m_props.push_back(p);
    }
  }
}

// 世界循环平移后回收已经跑到自车身后的路口，
//  并在前方按固定间距补铺，保持「无限道路」不会走到尽头。
void AdasWorld::recycleRoadNetwork()
{
  const float backZ = m_ego.position.z - 120.0f;
  const float frontZ = m_ego.position.z + 320.0f;

  // ---- 路口：删掉身后的，按 kCrossRoadSpacing 周期往前补 ----
  m_crossRoads.erase(std::remove_if(m_crossRoads.begin(), m_crossRoads.end(),
                                    [backZ](const CrossRoad &c)
                                    { return c.centerZ < backZ; }),
                     m_crossRoads.end());

  // 取「最靠前的路口」作为补铺基准；列表为空时直接用 frontZ
  float newest = backZ;
  for (const auto &c : m_crossRoads)
    newest = std::max(newest, c.centerZ);

  float z = newest;
  while (z < frontZ)
  {
    z += kCrossRoadSpacing;

    CrossRoad cr;
    cr.centerZ = z;
    cr.halfDepth = 9.0f;
    cr.halfWidth = kSidewalkOuterX + 2.0f;
    // 用位置派生的稳定种子，保证同一位置的路口每次生成都一致
    cr.variant = static_cast<int>(std::fabs(z) * 0.1f) & 0xFFFF;
    m_crossRoads.push_back(cr);
  }
}

// 推进街景中的「活物」：
//   · 行人在人行道上来回走（走到活动范围端头就转身）
//   · 骑行者沿自行车道单向骑行，骑出前方范围后从后方绕回
//   · 信号灯相位推进
void AdasWorld::updateStreetLife(float dt)
{
  // ---- 信号灯相位：周期 21 秒（绿 9s / 黄 3s / 红 9s）----
  const float cycle = 21.0f;
  for (auto &cr : m_crossRoads)
  {
    float ph = cr.lightPhase + dt / cycle;
    while (ph > 1.0f)
      ph -= 1.0f;
    cr.lightPhase = ph;
  }

  const float backLimit = m_ego.position.z - 90.0f;
  const float frontLimit = m_ego.position.z + 210.0f;

  for (auto &p : m_props)
  {
    const PropKind kind = static_cast<PropKind>(p.kind);

    if (kind == PropKind::Pedestrian)
    {
      // 朝向决定行走方向：yaw = 0 走向 +Z，yaw = 180 走向 -Z
      const float speed = 1.25f; // 步行速度 m/s
      const bool towardPlusZ = (p.yaw == 0.0f);
      p.position.z += (towardPlusZ ? speed : -speed) * dt;

      // 走到范围端头就转身，形成「人行道上来回散步」的循环
      if (p.position.z > frontLimit && towardPlusZ)
        p.yaw = 180.0f;
      else if (p.position.z < backLimit && !towardPlusZ)
        p.yaw = 0.0f;
    }
    else if (kind == PropKind::Cyclist)
    {
      const float speed = 4.5f; // 骑行速度 m/s
      p.position.z += speed * dt;

      // 骑到前方尽头就从身后 300 米处重新出现（世界是循环的）
      if (p.position.z > m_ego.position.z + 215.0f)
        p.position.z -= 300.0f;
    }
  }
}

} // namespace vc
