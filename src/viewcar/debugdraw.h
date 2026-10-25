#ifndef VIEWCAR_DEBUGDRAW_H
#define VIEWCAR_DEBUGDRAW_H

// ============================================================
//  viewcar - 调试/可视化几何体绘制
//
//  感知可视化的核心工具：ADAS 界面里绝大多数的「线」与「体」
//  （车道线、目标边界框、FOV 感知锥、距离刻度、地面网格）
//  都是用最基础的几何图元拼出来的。
//
//  本模块提供两类资源：
//    DebugLines  —— 动态线段批处理（每帧重建顶点，一次 draw call 画完所有线）
//    ShapeMesh   —— 静态网格（立方体/圆柱/平面），带 GPU 实例化支持
// ============================================================

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>

#include "shader.h"

namespace vc
{

// ------------------------------ 动态线段批处理 ------------------------------
// 用法：每帧 clear() -> 多次 add*() -> flush() -> draw()
// 所有线段合并到一次 draw call，适合画上千条车道线/包围盒
class DebugLines
{
public:
  DebugLines() = default;
  ~DebugLines();

  // 创建 VAO/VBO（需要 GL 上下文）
  void init();

  // 清空本帧的线段数据（不释放 GPU 缓冲）
  void clear() { m_vertices.clear(); }

  // 添加一条线段（两端点各带颜色）
  void addLine(const glm::vec3 &a, const glm::vec3 &b,
               const glm::vec3 &colorA, const glm::vec3 &colorB);

  // 添加一条单色线段
  void addLine(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &color);

  // 添加一个轴对齐包围盒（8 条棱）
  void addBox(const glm::vec3 &min, const glm::vec3 &max, const glm::vec3 &color);

  // 添加一个「朝向包围盒」：以 center 为中心、尺寸 size（长宽高），
  // 绕 Y 轴按 yawDeg 旋转。ADAS 目标框基本都是这种。
  void addOrientedBox(const glm::vec3 &center, const glm::vec3 &size,
                      float yawDeg, const glm::vec3 &color);

  // 添加一个圆（4 段折线近似），可指定朝向平面
  void addCircle(const glm::vec3 &center, float radius, int segments,
                 const glm::vec3 &color);

  // 添加一个 XZ 平面上的地面网格
  void addGrid(float halfSize, int divisions, const glm::vec3 &color,
               const glm::vec3 &axisColor);

  // 把 CPU 端累积的线段上传到 GPU
  void flush();

  // 绘制所有已 flush 的线段（需先 shader.use() 并设置 uViewProj）
  void draw() const;

  unsigned int lineCount() const { return static_cast<unsigned int>(m_vertices.size() / 2); }

  void destroy();

private:
  // 顶点格式：位置(3) + 颜色(3)
  struct LineVertex
  {
    glm::vec3 position;
    glm::vec3 color;
  };

  std::vector<LineVertex> m_vertices;

  unsigned int m_vao = 0;
  unsigned int m_vbo = 0;
  unsigned int m_capacity = 0; // VBO 已分配的顶点数
  unsigned int m_uploaded = 0; // 本次已上传的顶点数
  bool m_initialized = false;
};

// ------------------------------ 静态形状网格 ------------------------------
// 提供立方体 / 圆柱 / 平面三种基础网格，并支持 GPU 实例化。
//
// 为什么需要实例化：ADAS 场景里可能有几百个障碍物，
// 若每个都单独设置 uniform 再 draw，draw call 会爆炸；
// 实例化用一次 draw call 画出全部障碍物（把变换矩阵作为实例属性上传）。
class ShapeMesh
{
public:
  ShapeMesh() = default;
  ~ShapeMesh();

  // 生成单位立方体（边长 1，中心在原点）
  bool createBox();

  // 生成单位圆柱（半径 0.5，高 1，轴向 Y，中心在原点）
  bool createCylinder(int radialSegments = 16);

  // 生成 XZ 平面（边长 1，中心在原点，法线 +Y）
  bool createPlane();

  // 用「位置(3) + 法线(3) 顶点数组 + 索引数组」直接构建网格。
  // 这是给 props.cpp 的复合模型使用的入口：调用方自行拼装顶点，
  // 这里只负责上传 GPU 并计算三角形数。
  bool createFromData(const std::vector<float> &vertices,
                      const std::vector<unsigned int> &indices);

  void draw() const;

  // ---------- 实例化绘制 ----------
  // 为实例化准备一个专用 VAO：把网格顶点属性与实例属性（mat4 + vec3 颜色）绑定
  void setupInstancing();

  // 上传实例数据：transforms 为每个实例的模型矩阵，colors 为每个实例的颜色
  void uploadInstances(const std::vector<glm::mat4> &transforms,
                       const std::vector<glm::vec3> &colors);

  // 以实例化方式绘制（需先 setupInstancing + uploadInstances）
  void drawInstanced() const;

  unsigned int instanceCount() const { return m_instanceCount; }
  unsigned int triangleCount() const { return m_triangleCount; }

  void destroy();

private:
  unsigned int m_vao = 0;
  unsigned int m_vbo = 0;
  unsigned int m_ebo = 0;
  unsigned int m_indexCount = 0;
  unsigned int m_triangleCount = 0;

  // 实例化资源
  unsigned int m_instancedVao = 0;
  unsigned int m_instanceVbo = 0; // 交错存储 mat4 + vec3
  unsigned int m_instanceCapacity = 0;
  unsigned int m_instanceCount = 0;
  bool m_instancingReady = false;

  void build(const std::vector<float> &vertices,
             const std::vector<unsigned int> &indices,
             bool withNormal);
};

// ------------------------------ 线框渲染配置 ------------------------------
// 绘制线段时通常希望关闭深度写入或加线宽，这里集中管理默认状态
inline void applyLineState()
{
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE); // 线不写深度，避免遮挡后续透明绘制
  glLineWidth(1.0f);
}

inline void restoreLineState()
{
  glDepthMask(GL_TRUE);
}

} // namespace vc

#endif // VIEWCAR_DEBUGDRAW_H
