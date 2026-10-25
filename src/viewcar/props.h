#ifndef VIEWCAR_PROPS_H
#define VIEWCAR_PROPS_H

// ============================================================
//  viewcar - 场景道具网格库（程序化生成，零外部资源）
//
//  为什么需要这个模块：
//    原始实现把所有感知目标（轿车/卡车/行人/骑行者/锥桶）都画成
//    「一个单位立方体 + 缩放」，从视觉上完全无法区分车型，也不像车。
//    ADAS 可视化虽以数据为主，但车型辨识度直接影响演示说服力。
//
//  本模块用基础几何体（盒/圆柱/圆锥/球）
//  **程序化拼装**出可辨识的复合模型：
//    · 五类感知目标：轿车 / 卡车 / 行人 / 骑行者 / 锥桶
//    · 街景道具：树 / 楼房 / 路灯 / 电线杆 / 信号灯 / 公交站台 / 护栏 / 广告牌
//
//  设计约束：
//    1. 全部网格带法线，可直接走现有的 scene.frag 光照管线
//    2. 全部网格是「单位尺度」的复合体，靠模型矩阵缩放定位
//    3. 支持同一个 VAO 的 GPU 实例化批量绘制（复用 ShapeMesh 机制）
//    4. 不引入任何外部模型文件或贴图，纯顶点数据
// ============================================================

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "debugdraw.h"

namespace vc
{

// ------------------------------ 构件级几何工具 ------------------------------
// 这些函数把「一个盒子 / 一个圆柱」按局部变换追加进顶点数组，
// 供上面各 create*() 拼装复合模型使用。

// 顶点布局：位置(3) + 法线(3)，与 ShapeMesh::build 期望的格式一致
using VertexList = std::vector<float>;
using IndexList = std::vector<unsigned int>;

// 往顶点数组里追加一个轴对齐盒子（中心 centerLocal，半尺寸 half，可带局部旋转）
void appendBox(VertexList &verts, IndexList &indices, const glm::mat4 &xform);

// 追加一个圆柱（轴向 Y，半径 radius，高 height，绕 Y 轴 radialSegments 段）
void appendCylinder(VertexList &verts, IndexList &indices,
                    const glm::mat4 &xform, int radialSegments = 16);

// 追加一个圆锥（底面半径 radius，高 height，轴向 +Y，尖朝上）
void appendCone(VertexList &verts, IndexList &indices, const glm::mat4 &xform,
                int radialSegments = 16);

// 追加一个球（半径 radius，经纬分段）
void appendSphere(VertexList &verts, IndexList &indices, const glm::mat4 &xform,
                  int rings = 8, int sectors = 12);

// ------------------------------ 模型部件 ------------------------------
// 每个部件是「复合模型的一部分」，带自己的模型矩阵与颜色。
// 这样一辆车可以用「车身+车窗+车轮+车灯」四个部件实例化绘制，
// 既保留外形辨识度，又只需要极少数 draw call。

struct PropPart
{
  const ShapeMesh *mesh = nullptr; // 用哪个网格
  glm::mat4 model = glm::mat4(1.0f); // 相对部件中心的变换
  glm::vec3 color = glm::vec3(1.0f);
  float metallic = 0.1f;
  float roughness = 0.6f;
  float rim = 0.3f;
  float emissive = 0.0f; // 自发光强度（车灯 / 信号灯 / 窗内灯光）
};

// ------------------------------ 部件 ------------------------------

// 每个部件是「复合模型的一部分」，带自己的模型矩阵与颜色。
// 这样一辆车可以用「车身+车窗+车轮+车灯」多个部件实例化绘制，
// 既保留外形辨识度，又只需要极少数 draw call。

// ------------------------------ 网格库 ------------------------------
// 所有共享网格集中在这里创建一次，之后反复实例化绘制。
// 每个网格都是「单位尺度」的，用模型矩阵缩放成实际尺寸。
//
// 注意：PropKind 枚举与 StreetProp 结构定义在 adas.h ——
// 「世界数据」层负责生成街道布局，「渲染」层负责画出来，
// 依赖方向是 renderer -> adas，不能反过来。

class PropMeshes
{
public:
  // 创建全部网格（需要 GL 上下文）
  bool init();

  void destroy();

  // ---- 感知目标部件 ----
  ShapeMesh carBody;      // 轿车车身（主舱 + 引擎盖 + 后备箱 + 保险杠）
  ShapeMesh carGlass;     // 轿车座舱玻璃带
  ShapeMesh truckCab;     // 卡车驾驶室
  ShapeMesh truckCargo;   // 卡车货箱
  ShapeMesh truckGlass;   // 卡车驾驶室风挡
  ShapeMesh wheel;        // 车轮（轮胎，轴向 X）
  ShapeMesh wheelRim;     // 轮辋（亮色，插在轮胎中心）
  ShapeMesh headLight;    // 前照灯（小圆盒，自发光）
  ShapeMesh tailLight;    // 尾灯（小圆盒，自发光）
  ShapeMesh pedBody;      // 行人躯干
  ShapeMesh pedHead;      // 行人头部
  ShapeMesh pedLegs;      // 行人双腿
  ShapeMesh cycleFrame;   // 自行车（车架 + 两轮）
  ShapeMesh riderBody;    // 骑行者躯干 + 头盔
  ShapeMesh coneBase;     // 锥桶底座
  ShapeMesh coneBody;     // 锥桶锥体

  // ---- 街景部件 ----
  ShapeMesh trunk;        // 树干
  ShapeMesh canopy;       // 树冠（球体）
  ShapeMesh buildingBody; // 楼房主体
  ShapeMesh buildingBase; // 楼房裙楼
  ShapeMesh buildingTop;  // 楼顶设备间
  ShapeMesh windowBand;   // 楼体窗带（自发光）
  ShapeMesh lampPole;     // 杆（路灯 / 电线杆共用）
  ShapeMesh lampArm;      // 路灯悬臂
  ShapeMesh lampHead;     // 灯头（自发光）
  ShapeMesh signPlate;    // 广告牌面板
  ShapeMesh railPost;     // 护栏立柱
  ShapeMesh railBeam;     // 护栏横梁
  ShapeMesh bushBall;     // 灌木球
  ShapeMesh roadSlab;     // 路面平板（斑马线 / 停止线 / 路口铺装共用）

  unsigned int partCount() const { return m_partCount; }

private:
  unsigned int m_partCount = 0;

  // 用「顶点+索引」构建一个网格，并计入部件统计
  bool make(ShapeMesh &mesh, VertexList &verts, IndexList &indices);
};

} // namespace vc

#endif // VIEWCAR_PROPS_H
