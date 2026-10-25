#ifndef VIEWCAR_ADAS_H
#define VIEWCAR_ADAS_H

// ============================================================
//  viewcar - ADAS 感知数据模型
//
//  模拟一套「自动驾驶感知 + 规划」的输出数据，供可视化模块绘制：
//    · EgoVehicle    自车状态（位置/朝向/速度）与运动学更新
//    · PerceptionObject 感知目标（车/卡车/行人/骑行者/锥桶）
//    · LaneLine      车道线（多段折线，含类型与置信度）
//    · SensorFov     传感器视场（相机/雷达/激光雷达）
//
//  坐标约定（右手系，Y 轴向上）：
//    +X = 车辆右侧
//    +Y = 上
//    +Z = 车头前进方向
//  自车初始位于世界原点附近，所有感知目标以世界坐标存储。
// ============================================================

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "core.h"

namespace vc
{

// ------------------------------ 目标类别 ------------------------------
enum class ObjectClass
{
  Car = 0,      // 轿车
  Truck,        // 卡车
  Pedestrian,   // 行人
  Cyclist,      // 骑行者
  Cone,         // 锥桶 / 静态障碍
  Unknown
};

// 每类目标的可视化颜色（ADAS 界面常用约定）
inline glm::vec3 colorOf(ObjectClass c)
{
  switch (c)
  {
  case ObjectClass::Car:
    return glm::vec3(0.20f, 0.95f, 0.45f); // 绿
  case ObjectClass::Truck:
    return glm::vec3(0.25f, 0.70f, 1.00f); // 蓝
  case ObjectClass::Pedestrian:
    return glm::vec3(1.00f, 0.45f, 0.15f); // 橙
  case ObjectClass::Cyclist:
    return glm::vec3(1.00f, 0.90f, 0.20f); // 黄
  case ObjectClass::Cone:
    return glm::vec3(1.00f, 0.30f, 0.30f); // 红
  default:
    return glm::vec3(0.70f, 0.70f, 0.70f);
  }
}

inline const char *nameOf(ObjectClass c)
{
  switch (c)
  {
  case ObjectClass::Car:
    return "轿车";
  case ObjectClass::Truck:
    return "卡车";
  case ObjectClass::Pedestrian:
    return "行人";
  case ObjectClass::Cyclist:
    return "骑行者";
  case ObjectClass::Cone:
    return "锥桶";
  default:
    return "未知";
  }
}

// 各类目标的典型尺寸（长度, 高度, 宽度，单位：米）
inline glm::vec3 sizeOf(ObjectClass c)
{
  switch (c)
  {
  case ObjectClass::Car:
    return glm::vec3(4.60f, 1.50f, 1.80f);
  case ObjectClass::Truck:
    return glm::vec3(9.00f, 3.20f, 2.50f);
  case ObjectClass::Pedestrian:
    return glm::vec3(0.60f, 1.75f, 0.60f);
  case ObjectClass::Cyclist:
    return glm::vec3(1.80f, 1.70f, 0.70f);
  case ObjectClass::Cone:
    return glm::vec3(0.40f, 0.70f, 0.40f);
  default:
    return glm::vec3(1.00f, 1.00f, 1.00f);
  }
}

// ------------------------------ 感知目标 ------------------------------
struct PerceptionObject
{
  int id = 0;
  ObjectClass cls = ObjectClass::Unknown;

  glm::vec3 position = glm::vec3(0.0f); // 包围盒中心（世界坐标）
  glm::vec3 velocity = glm::vec3(0.0f); // 速度向量（m/s）
  float yaw = 0.0f;                     // 朝向（度，0 = 指向 +Z）

  float length = 4.6f; // 沿车头方向
  float height = 1.5f;
  float width = 1.8f;

  float confidence = 0.9f; // 感知置信度 0~1
  bool tracked = true;     // 是否被跟踪器稳定关联
  int trackAge = 0;        // 跟踪持续帧数
  bool predicted = false;  // 是否为预测（非量测）目标，界面常以暗色区分

  // 由 updateDerived 填充
  float distToEgo = 0.0f; // 到自车的直线距离
  float ttc = -1.0f;      // Time To Collision（秒），-1 表示无碰撞风险

  // 在「车辆坐标系」中的纵向/横向分量（自车为参考系）
  float longitudinal = 0.0f; // 前方为正
  float lateral = 0.0f;      // 右侧为正

  glm::vec3 sizeVec() const { return glm::vec3(length, height, width); }

  float speed() const { return glm::length(velocity); }

  void updateDerived(const glm::vec3 &egoPos, const glm::vec3 &egoVel,
                     float egoHeading);
};

// ------------------------------ 车道线 ------------------------------
struct LanePoint
{
  glm::vec3 position; // 世界坐标
  float curvature = 0.0f;
};

enum class LaneType
{
  Solid,   // 实线（不可跨越）
  Dashed,  // 虚线（可跨越）
  RoadEdge // 道路边界
};

struct LaneLine
{
  int id = 0;
  LaneType type = LaneType::Solid;
  float confidence = 0.95f;
  std::vector<LanePoint> points; // 按纵向顺序排列的采样点

  glm::vec3 color() const;
};

inline glm::vec3 LaneLine::color() const
{
  switch (type)
  {
  case LaneType::Solid:
    return glm::vec3(0.95f, 0.95f, 0.98f); // 白
  case LaneType::Dashed:
    return glm::vec3(1.00f, 0.85f, 0.25f); // 黄
  case LaneType::RoadEdge:
    return glm::vec3(0.55f, 0.60f, 0.70f); // 灰蓝
  }
  return glm::vec3(1.0f);
}

// ------------------------------ 传感器视场 ------------------------------
// 用于可视化「感知范围」，是 ADAS 界面的标志性元素
struct SensorFov
{
  std::string name = "Camera";
  glm::vec3 origin = glm::vec3(0.0f); // 传感器安装位置（相对自车后轴中心）
  float yaw = 0.0f;                   // 朝向偏移（相对车头，度）
  float hfovDeg = 60.0f;              // 水平视场角
  float vfovDeg = 40.0f;              // 垂直视场角
  float range = 60.0f;                // 最大探测距离（米）
  glm::vec3 color = glm::vec3(0.30f, 0.75f, 1.00f);
  bool enabled = true;
};

// ------------------------------ 自车运动学 ------------------------------
// 用「自行车模型」近似自车运动，这是车载算法里最常用的简化模型：
//   x'     = x + v * sin(theta) * dt
//   z'     = z + v * cos(theta) * dt
//   theta' = theta + v / L * tan(delta) * dt
// 其中 L 为轴距，delta 为前轮转角。
// 本项目坐标里车头指向 +Z，因此航向角以 +Z 为 0 度、转向 +X 为正。
class EgoVehicle
{
public:
  // ---- 状态 ----
  glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f); // 后轴中心
  float heading = 0.0f;                              // 航向角（度，0 = +Z）
  float speed = 12.0f;                               // 纵向速度 m/s（约 43 km/h）
  float steeringAngle = 0.0f;                        // 前轮转角（度）
  float wheelBase = 2.70f;                           // 轴距 m

  // ---- 车身尺寸 ----
  float length = 4.80f;
  float width = 1.90f;
  float height = 1.45f;

  // ---- 规划输出（供可视化层使用）----
  float targetSpeed = 12.0f;          // 期望速度
  std::vector<glm::vec3> plannedPath; // 规划轨迹（世界坐标点串）

  // 按自行车模型积分一步
  void update(float dt);

  // 世界坐标下的速度向量
  glm::vec3 velocity() const;

  // 车头方向向量
  glm::vec3 forward() const;

  // 车身几何中心（后轴中心沿车头方向偏移 length/2）
  glm::vec3 center() const;

  glm::mat4 modelMatrix() const;
};

// ------------------------------ 街景道具 ------------------------------
// 背景环境物：不属于感知目标，纯装饰，让画面从「空旷跑道」变成「城市街道」。
// 这些物体不做感知计算，只参与渲染。
//
// 枚举定义在这里而不是 props.h，是为了让「世界数据」层不依赖「渲染」层：
// adas.h 负责生成街道布局，props.h / renderer.cpp 负责把它画出来。
enum class PropKind
{
  Tree = 0,     // 行道树
  Building,     // 楼房
  StreetLamp,   // 路灯
  UtilityPole,  // 电线杆
  TrafficLight, // 信号灯
  BusStop,      // 公交站台
  GuardRail,    // 护栏
  Billboard,    // 广告牌
  Bush,         // 灌木 / 花坛
  ParkedCar,    // 路边停放车辆
  Pedestrian,   // 路边行人（街景，会走动）
  Cyclist,      // 路边骑行者（街景，沿自行车道骑行）
  Zebra,        // 斑马线（路口人行横道）
  StopLine,     // 停止线
  CrossBox,     // 十字路口铺装（横向路面）
};

inline const char *nameOfProp(PropKind k)
{
  switch (k)
  {
  case PropKind::Tree:
    return "行道树";
  case PropKind::Building:
    return "楼房";
  case PropKind::StreetLamp:
    return "路灯";
  case PropKind::UtilityPole:
    return "电线杆";
  case PropKind::TrafficLight:
    return "信号灯";
  case PropKind::BusStop:
    return "公交站台";
  case PropKind::GuardRail:
    return "护栏";
  case PropKind::Billboard:
    return "广告牌";
  case PropKind::Bush:
    return "灌木";
  case PropKind::ParkedCar:
    return "路边停车";
  case PropKind::Pedestrian:
    return "行人";
  case PropKind::Cyclist:
    return "骑行者";
  case PropKind::Zebra:
    return "斑马线";
  case PropKind::StopLine:
    return "停止线";
  case PropKind::CrossBox:
    return "路口铺装";
  default:
    return "道具";
  }
}

struct StreetProp
{
  int kind = 0;                  // 对应 PropKind 的整数值
  glm::vec3 position = glm::vec3(0.0f); // 底面中心（世界坐标）
  float yaw = 0.0f;              // 绕 Y 轴朝向（度）
  float width = 1.0f;            // X 方向尺寸
  float height = 1.0f;           // Y 方向尺寸
  float depth = 1.0f;            // Z 方向尺寸
  int variant = 0;               // 变体种子：决定颜色 / 层数 / 造型细节
  bool flip = false;             // 左右镜像（用于路灯悬臂朝向）
};

// ------------------------------ 道路网络 ------------------------------
//  道路不再是「一条笔直无限的路」，而是由若干横向路口切分成路段：
//
//      Z ↑              十字路口 (横向马路)
//        │        ┌──────────────┐
//        │        │   ▓▓▓▓▓▓▓▓   │  ← 斑马线
//   ─────┼────────┤   路口区     ├─────────→ X
//        │        │   ▓▓▓▓▓▓▓▓   │
//        │        └──────────────┘
//
//  自车沿 +Z 行驶，每隔一段会遇到一个横向马路（十字路口），
//  路口处有斑马线与信号灯。

// 十字路口：以 centerZ 为中心、沿 X 方向延伸的横向道路
struct CrossRoad
{
  float centerZ = 0.0f;  // 路口中心的世界 Z
  float halfDepth = 9.0f; // 沿 Z 的一半宽度（路面宽度）
  float halfWidth = 14.0f; // 沿 X 的一半长度
  int variant = 0;        // 变体种子（决定路缘 / 标线细节）
  float lightPhase = 0.0f; // 信号灯相位 0~1，由世界时间推进
};

// ------------------------------ 车道保持控制器 ------------------------------
// 极简「横向控制器」：根据自车与车道中心线的横向偏差，
// 用比例控制算出前轮转角。真实系统用 MPC / LQR，这里用 P 控制演示同样的数据流。
class LaneKeeper
{
public:
  float laneCenterX = 0.0f;      // 目标车道中心线的 X 坐标
  float lookAheadDistance = 12.0f;
  float gain = 0.90f;            // 横向 P 增益（度 / 米）
  bool enabled = true;

  float laneWidth = 3.75f;       // 车道宽度（m）
  int laneCount = 6;             // 总车道数（双向各 3 条）

  // 根据自车状态计算前轮转角（度）
  float computeSteering(const EgoVehicle &ego) const;

  // 生成车道线（世界坐标）
  void buildLaneLines(std::vector<LaneLine> &out, float length, float startZ,
                      float dashedPhase) const;
};

// ------------------------------ 世界 / 场景管理 ------------------------------
class AdasWorld
{
public:
  AdasWorld();

  // 初始化所有仿真实体
  void init();

  // 推进仿真，dt 单位为秒
  void update(float dt);

  // ---- 数据访问 ----
  EgoVehicle &ego() { return m_ego; }
  const EgoVehicle &ego() const { return m_ego; }

  const std::vector<PerceptionObject> &objects() const { return m_objects; }
  std::vector<PerceptionObject> &objects() { return m_objects; }

  const std::vector<LaneLine> &laneLines() const { return m_lanes; }
  std::vector<SensorFov> &sensors() { return m_sensors; }
  const std::vector<SensorFov> &sensors() const { return m_sensors; }

  // 街景道具（树 / 楼 / 路灯 / 行人等），纯渲染装饰
  const std::vector<StreetProp> &props() const { return m_props; }
  std::vector<StreetProp> &props() { return m_props; }
  int propCountOfKind(int kind) const;

  // 道路网络：十字路口
  const std::vector<CrossRoad> &crossRoads() const { return m_crossRoads; }

  // 自车当前位置是否落在某个路口范围内（用于可视化提示 / 信号灯
  // 判断）。返回 nullptr 表示不在路口内。
  const CrossRoad *crossRoadAt(float z) const;

  // 给定世界 Z 处的信号灯相位（0~1）。没有路口时返回 0.5（相当于黄灯后的稳定红）。
  //  渲染层用它决定红/黄/绿哪一盏亮。
  float signalPhaseAt(float z) const;

  // 世界时间（秒），用于信号灯相位等周期性动画
  float worldTime() const { return m_worldTime; }

  // 立即重建整条街道（密度改变后调用，让远近道具一起改变疏密）
  void rebuildStreetNow() { buildStreetScene(-60.0f, m_ego.position.z + 260.0f); }

  LaneKeeper &laneKeeper() { return m_laneKeeper; }
  const LaneKeeper &laneKeeper() const { return m_laneKeeper; }

  // 统计
  int objectCountOfClass(ObjectClass c) const;
  int riskyObjectCount() const { return m_riskyCount; }
  float nearestObjectDistance() const { return m_nearestDist; }

  // ---- 仿真参数（ImGui 面板可调）----
  float timeScale = 1.0f;
  bool simulationRunning = true;
  float streetDensity = 1.0f; // 街景密度倍数（由 VisualSettings 同步过来）

  int m_riskyCount = 0;
  float m_nearestDist = 0.0f;

private:
  EgoVehicle m_ego;
  std::vector<PerceptionObject> m_objects;
  std::vector<LaneLine> m_lanes;
  std::vector<SensorFov> m_sensors;
  std::vector<StreetProp> m_props; // 街景道具
  LaneKeeper m_laneKeeper;

  std::vector<CrossRoad> m_crossRoads; // 十字路口

  int m_nextId = 1;
  float m_dashedPhase = 0.0f;  // 虚线的动画相位
  float m_spawnTimer = 0.0f;
  float m_crossTimer = 0.0f;   // 横穿目标生成计时
  float m_worldTime = 0.0f;    // 世界时间（信号灯相位等）

  // 生成一个交通目标。
  // laneIdx: 车道索引（0..5，见 kLaneCenterX 约定）
  // z:       生成位置的世界 Z
  // 车道方向由 laneIdx 决定：0~2 为对向（-Z），3~5 为同向（+Z）
  void spawnTrafficAt(int laneIdx, float z);

  // 生成一个「过马路」的行人 / 骑行者目标（横穿道路）
  void spawnCrossingTraffic(float z);

  // 生成街景（道路两侧 60~200 米范围内一次性铺开，随世界循环平移）
  void buildStreetScene(float zFrom, float zTo);

  // 铺一段「横向马路」（十字路口）以及其斑马线 / 停止线铺装
  void buildCrossRoad(const CrossRoad &cr);

  // 世界循环平移后，回收远处路口并在端头补铺，
  //  保持「每 kCrossRoadSpacing 米一个路口」的周期连续性
  void recycleRoadNetwork();

  // 推进街景中的「活物」：行人走动、骑行者骑行、信号灯相位
  void updateStreetLife(float dt);

  // 重建所有街景道具（世界循环平移后调用，保持视觉连续）
  void rebuildStreet(float zFrom, float zTo);

  // 重算所有目标的 distToEgo / ttc / 纵向横向分量
  void updatePerception();

  // 建立初始场景（车道线 + 传感器 + 初始交通流）
  void buildScene();
};

} // namespace vc

#endif // VIEWCAR_ADAS_H
