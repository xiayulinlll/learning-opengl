#include "props.h"

#include <algorithm>

namespace vc
{

namespace
{
constexpr float kPi = 3.14159265358979f;

// 把一个顶点(位置+法线)按变换矩阵写入数组。
// 法线用 xform 的 3x3 部分变换后归一化；由于我们的变换只含
// 平移/旋转/非均匀缩放，非均匀缩放会让法线略微偏斜，
// 但场景里缩放比例接近各向同性，视觉上无影响。
void pushVertex(VertexList &v, const glm::mat4 &xform, const glm::vec3 &pos,
                const glm::vec3 &normal)
{
  const glm::vec4 wp = xform * glm::vec4(pos, 1.0f);
  const glm::vec3 wn = glm::normalize(glm::mat3(xform) * normal);
  v.insert(v.end(), {wp.x, wp.y, wp.z, wn.x, wn.y, wn.z});
}
} // namespace

// ============================================================
//  基础图元
// ============================================================

void appendBox(VertexList &verts, IndexList &indices, const glm::mat4 &xform)
{
  // 单位立方体（边长 1，中心在原点），6 个面各自 4 顶点 —— 保证法线硬边
  const float h = 0.5f;

  struct Face
  {
    glm::vec3 n;
    glm::vec3 c[4];
  };

  const Face faces[6] = {
      // +X
      {{1, 0, 0}, {{h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h}}},
      // -X
      {{-1, 0, 0}, {{-h, -h, h}, {-h, -h, -h}, {-h, h, -h}, {-h, h, h}}},
      // +Y
      {{0, 1, 0}, {{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}},
      // -Y
      {{0, -1, 0}, {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}},
      // +Z
      {{0, 0, 1}, {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}},
      // -Z
      {{0, 0, -1}, {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}},
  };

  for (const Face &f : faces)
  {
    const unsigned int base = static_cast<unsigned int>(verts.size() / 6);
    for (int i = 0; i < 4; ++i)
      pushVertex(verts, xform, f.c[i], f.n);

    indices.insert(indices.end(),
                   {base, base + 1, base + 2, base + 2, base + 3, base});
  }
}

void appendCylinder(VertexList &verts, IndexList &indices,
                    const glm::mat4 &xform, int radialSegments)
{
  if (radialSegments < 3)
    radialSegments = 3;

  const float r = 0.5f;
  const float h = 0.5f;

  // ---- 侧面 ----
  for (int i = 0; i <= radialSegments; ++i)
  {
    const float a = 2.0f * kPi * static_cast<float>(i) /
                    static_cast<float>(radialSegments);
    const glm::vec3 n(std::cos(a), 0.0f, std::sin(a));

    const unsigned int base = static_cast<unsigned int>(verts.size() / 6);

    pushVertex(verts, xform, glm::vec3(n.x * r, -h, n.z * r), n);
    pushVertex(verts, xform, glm::vec3(n.x * r, h, n.z * r), n);

    if (i < radialSegments)
    {
      indices.insert(indices.end(),
                     {base, base + 2, base + 1, base + 1, base + 2, base + 3});
    }
  }

  // ---- 顶盖 ----
  {
    const unsigned int center = static_cast<unsigned int>(verts.size() / 6);
    pushVertex(verts, xform, glm::vec3(0.0f, h, 0.0f), glm::vec3(0, 1, 0));

    const unsigned int start = static_cast<unsigned int>(verts.size() / 6);
    for (int i = 0; i <= radialSegments; ++i)
    {
      const float a = 2.0f * kPi * static_cast<float>(i) /
                      static_cast<float>(radialSegments);
      pushVertex(verts, xform, glm::vec3(r * std::cos(a), h, r * std::sin(a)),
                 glm::vec3(0, 1, 0));
    }
    for (int i = 0; i < radialSegments; ++i)
      indices.insert(indices.end(),
                     {center, start + static_cast<unsigned int>(i),
                      start + static_cast<unsigned int>(i) + 1});
  }

  // ---- 底盖（绕序反向）----
  {
    const unsigned int center = static_cast<unsigned int>(verts.size() / 6);
    pushVertex(verts, xform, glm::vec3(0.0f, -h, 0.0f), glm::vec3(0, -1, 0));

    const unsigned int start = static_cast<unsigned int>(verts.size() / 6);
    for (int i = 0; i <= radialSegments; ++i)
    {
      const float a = 2.0f * kPi * static_cast<float>(i) /
                      static_cast<float>(radialSegments);
      pushVertex(verts, xform, glm::vec3(r * std::cos(a), -h, r * std::sin(a)),
                 glm::vec3(0, -1, 0));
    }
    for (int i = 0; i < radialSegments; ++i)
      indices.insert(indices.end(),
                     {center, start + static_cast<unsigned int>(i) + 1,
                      start + static_cast<unsigned int>(i)});
  }
}

void appendCone(VertexList &verts, IndexList &indices, const glm::mat4 &xform,
                int radialSegments)
{
  if (radialSegments < 3)
    radialSegments = 3;

  const float r = 0.5f;
  const float h = 0.5f;

  // ---- 侧面：每个分段一个三角形（底面两点 + 顶点）----
  for (int i = 0; i < radialSegments; ++i)
  {
    const float a0 = 2.0f * kPi * static_cast<float>(i) /
                     static_cast<float>(radialSegments);
    const float a1 = 2.0f * kPi * static_cast<float>(i + 1) /
                     static_cast<float>(radialSegments);

    const glm::vec3 p0(r * std::cos(a0), -h, r * std::sin(a0));
    const glm::vec3 p1(r * std::cos(a1), -h, r * std::sin(a1));
    const glm::vec3 tip(0.0f, h, 0.0f);

    // 侧面法线：取径向近似（斜面无硬边需求）
    const float am = (a0 + a1) * 0.5f;
    const glm::vec3 n0(std::cos(a0), 0.0f, std::sin(a0));
    const glm::vec3 n1(std::cos(a1), 0.0f, std::sin(a1));
    const glm::vec3 nm = glm::normalize(
        glm::vec3(std::cos(am), 0.35f, std::sin(am)));

    const unsigned int base = static_cast<unsigned int>(verts.size() / 6);
    pushVertex(verts, xform, p0, n0);
    pushVertex(verts, xform, p1, n1);
    pushVertex(verts, xform, tip, nm);

    indices.insert(indices.end(), {base, base + 1, base + 2});
  }

  // ---- 底盖 ----
  {
    const unsigned int center = static_cast<unsigned int>(verts.size() / 6);
    pushVertex(verts, xform, glm::vec3(0.0f, -h, 0.0f), glm::vec3(0, -1, 0));

    const unsigned int start = static_cast<unsigned int>(verts.size() / 6);
    for (int i = 0; i <= radialSegments; ++i)
    {
      const float a = 2.0f * kPi * static_cast<float>(i) /
                      static_cast<float>(radialSegments);
      pushVertex(verts, xform, glm::vec3(r * std::cos(a), -h, r * std::sin(a)),
                 glm::vec3(0, -1, 0));
    }
    for (int i = 0; i < radialSegments; ++i)
      indices.insert(indices.end(),
                     {center, start + static_cast<unsigned int>(i) + 1,
                      start + static_cast<unsigned int>(i)});
  }
}

void appendSphere(VertexList &verts, IndexList &indices, const glm::mat4 &xform,
                  int rings, int sectors)
{
  if (rings < 2)
    rings = 2;
  if (sectors < 3)
    sectors = 3;

  const float r = 0.5f;

  for (int i = 0; i <= rings; ++i)
  {
    const float phi = kPi * static_cast<float>(i) / static_cast<float>(rings);

    for (int j = 0; j <= sectors; ++j)
    {
      const float theta = 2.0f * kPi * static_cast<float>(j) /
                          static_cast<float>(sectors);
      const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi),
                        std::sin(phi) * std::sin(theta));
      pushVertex(verts, xform, n * r, n);
    }
  }

  for (int i = 0; i < rings; ++i)
  {
    for (int j = 0; j < sectors; ++j)
    {
      const unsigned int a = static_cast<unsigned int>(i * (sectors + 1) + j);
      const unsigned int b = a + static_cast<unsigned int>(sectors) + 1;

      indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
  }
}

// ============================================================
//  网格库构建
// ============================================================

bool PropMeshes::make(ShapeMesh &mesh, VertexList &verts, IndexList &indices)
{
  if (!mesh.createFromData(verts, indices))
    return false;

  // 实例化 VAO：所有部件都要走实例化绘制
  mesh.setupInstancing();
  ++m_partCount;
  return true;
}

// ---- 便捷局部变换：平移 * 绕Y旋转 * 缩放 ----
namespace
{
glm::mat4 partXform(const glm::vec3 &pos, const glm::vec3 &size,
                    float yawDeg = 0.0f, float pitchDeg = 0.0f,
                    float rollDeg = 0.0f)
{
  glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
  if (yawDeg != 0.0f)
    m = glm::rotate(m, glm::radians(yawDeg), glm::vec3(0, 1, 0));
  if (pitchDeg != 0.0f)
    m = glm::rotate(m, glm::radians(pitchDeg), glm::vec3(1, 0, 0));
  if (rollDeg != 0.0f)
    m = glm::rotate(m, glm::radians(rollDeg), glm::vec3(0, 0, 1));
  return glm::scale(m, size);
}
} // namespace

bool PropMeshes::init()
{
  m_partCount = 0;

  // ---------------------------------------------------------
  //  轿车车身（局部坐标：车头朝 +Z，长 1 / 宽 1 / 高 1 归一化）
  //  真实轿车是三段式：前悬（引擎盖）— 中段（座舱）— 后悬（后备箱）
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;

    // 底盘主舱：略窄，占全长的 62%
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {0.98f, 0.46f, 0.62f}));
    // 前引擎盖：更低更窄，向前延伸
    appendBox(v, i, partXform({0.0f, -0.10f, 0.34f}, {0.92f, 0.26f, 0.34f}));
    // 后行李箱：略高一点，向后延伸
    appendBox(v, i, partXform({0.0f, -0.08f, -0.34f}, {0.94f, 0.30f, 0.32f}));
    // 前保险杠
    appendBox(v, i, partXform({0.0f, -0.16f, 0.50f}, {0.94f, 0.20f, 0.06f}));
    // 后保险杠
    appendBox(v, i, partXform({0.0f, -0.16f, -0.50f}, {0.94f, 0.20f, 0.06f}));
    // 侧裙（车身下沿饰条），让侧视更立体
    appendBox(v, i, partXform({0.49f, -0.22f, 0.0f}, {0.04f, 0.14f, 0.86f}));
    appendBox(v, i, partXform({-0.49f, -0.22f, 0.0f}, {0.04f, 0.14f, 0.86f}));
    // 车顶（较窄，形成梯形收腰感）
    appendBox(v, i, partXform({0.0f, 0.24f, -0.04f}, {0.80f, 0.05f, 0.40f}));
    // 左右后视镜
    appendBox(v, i, partXform({0.52f, 0.02f, 0.22f}, {0.10f, 0.06f, 0.05f}));
    appendBox(v, i, partXform({-0.52f, 0.02f, 0.22f}, {0.10f, 0.06f, 0.05f}));

    if (!make(carBody, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  轿车座舱玻璃：深色玻璃带，比车顶略小，形成「车窗」层次
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;

    // 前风挡（带前倾角）
    appendBox(v, i, partXform({0.0f, 0.12f, 0.20f}, {0.78f, 0.22f, 0.04f},
                              0.0f, -14.0f));
    // 后风挡
    appendBox(v, i, partXform({0.0f, 0.12f, -0.26f}, {0.78f, 0.22f, 0.04f},
                              0.0f, 16.0f));
    // 左右侧窗
    appendBox(v, i, partXform({0.40f, 0.12f, -0.02f}, {0.02f, 0.20f, 0.44f}));
    appendBox(v, i, partXform({-0.40f, 0.12f, -0.02f}, {0.02f, 0.20f, 0.44f}));

    if (!make(carGlass, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  卡车驾驶室：方正、带大面积风挡，符合重卡造型
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;

    // 驾驶室主体（局部：车头 +Z，长度为 1 单位）
    appendBox(v, i, partXform({0.0f, 0.0f, -0.06f}, {0.96f, 0.72f, 0.72f}));
    // 车顶导流罩（重卡特征，减少风阻）
    appendBox(v, i, partXform({0.0f, 0.40f, -0.16f}, {0.88f, 0.14f, 0.52f}));
    // 前面板 / 保险杠
    appendBox(v, i, partXform({0.0f, -0.28f, 0.34f}, {0.98f, 0.24f, 0.10f}));
    // 侧脚踏板
    appendBox(v, i, partXform({0.50f, -0.34f, 0.06f}, {0.08f, 0.06f, 0.44f}));
    appendBox(v, i, partXform({-0.50f, -0.34f, 0.06f}, {0.08f, 0.06f, 0.44f}));
    // 后视镜（重卡长支架）
    appendBox(v, i, partXform({0.56f, 0.12f, 0.24f}, {0.16f, 0.06f, 0.05f}));
    appendBox(v, i, partXform({-0.56f, 0.12f, 0.24f}, {0.16f, 0.06f, 0.05f}));

    if (!make(truckCab, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  卡车货箱：长条封闭厢体（厢式货车造型）
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;

    // 主厢体
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.98f}));
    // 顶部导流条
    appendBox(v, i, partXform({0.0f, 0.52f, -0.30f}, {0.94f, 0.06f, 0.30f}));
    // 侧厢门缝隙（细条，增加细节）
    appendBox(v, i, partXform({0.51f, -0.10f, -0.34f}, {0.02f, 0.62f, 0.04f}));
    appendBox(v, i, partXform({-0.51f, -0.10f, -0.34f}, {0.02f, 0.62f, 0.04f}));
    // 后门
    appendBox(v, i, partXform({0.0f, -0.10f, -0.51f}, {0.9f, 0.62f, 0.03f}));
    // 底盘大梁
    appendBox(v, i, partXform({0.0f, -0.52f, 0.0f}, {0.86f, 0.12f, 0.94f}));

    if (!make(truckCargo, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  卡车风挡玻璃
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;

    appendBox(v, i, partXform({0.0f, 0.16f, 0.30f}, {0.84f, 0.34f, 0.04f},
                              0.0f, -8.0f));
    appendBox(v, i, partXform({0.49f, 0.16f, -0.02f}, {0.02f, 0.30f, 0.40f}));
    appendBox(v, i, partXform({-0.49f, 0.16f, -0.02f}, {0.02f, 0.30f, 0.40f}));

    if (!make(truckGlass, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  车轮：轮胎（轴向 X 的圆柱）+ 轮辋
  //  圆柱默认轴向 Y，绕 Z 轴转 90° 后轴向变为 X（车辆横向）
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    appendCylinder(v, i,
                   partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0.0f, 0.0f,
                             90.0f),
                   18);
    if (!make(wheel, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    // 轮辋：略宽的扁圆柱，插在轮胎中心，突出后银亮轮毂清晰可见
    appendCylinder(v, i,
                   partXform({0.0f, 0.0f, 0.0f}, {1.06f, 0.62f, 0.62f}, 0.0f, 0.0f,
                             90.0f),
                   12);
    if (!make(wheelRim, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  车灯：小扁盒，自发光由 uEmissive 控制
  //  局部坐标与车身一致（车头 +Z），车灯固定在车头/车尾两侧
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    for (int s = -1; s <= 1; s += 2)
    {
      appendBox(v, i, partXform({static_cast<float>(s) * 0.30f, -0.10f, 0.50f},
                                {0.30f, 0.16f, 0.05f}));
    }
    if (!make(headLight, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    for (int s = -1; s <= 1; s += 2)
    {
      appendBox(v, i, partXform({static_cast<float>(s) * 0.30f, -0.06f, -0.50f},
                                {0.28f, 0.16f, 0.05f}));
    }
    if (!make(tailLight, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  行人：躯干（含肩、双臂）/ 头（含颈）/ 双腿（含鞋）
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    appendBox(v, i, partXform({0.0f, 0.06f, 0.0f}, {0.42f, 0.44f, 0.24f}));
    appendBox(v, i, partXform({0.0f, 0.26f, 0.0f}, {0.50f, 0.10f, 0.24f}));
    appendBox(v, i, partXform({0.26f, 0.02f, 0.0f}, {0.12f, 0.42f, 0.14f}));
    appendBox(v, i, partXform({-0.26f, 0.02f, 0.0f}, {0.12f, 0.42f, 0.14f}));
    if (!make(pedBody, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    appendSphere(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.15f, 1.0f}), 8, 12);
    appendBox(v, i, partXform({0.0f, -0.46f, 0.0f}, {0.18f, 0.14f, 0.18f}));
    if (!make(pedHead, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    appendBox(v, i, partXform({0.11f, 0.0f, 0.0f}, {0.16f, 0.52f, 0.16f}));
    appendBox(v, i, partXform({-0.11f, 0.0f, 0.0f}, {0.16f, 0.52f, 0.16f}));
    appendBox(v, i, partXform({0.11f, -0.48f, 0.03f}, {0.18f, 0.10f, 0.24f}));
    appendBox(v, i, partXform({-0.11f, -0.48f, 0.03f}, {0.18f, 0.10f, 0.24f}));
    if (!make(pedLegs, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  自行车：前后轮 + 车架 + 车把 + 车座（车头朝 +Z）
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    appendCylinder(v, i,
                   partXform({0.0f, -0.16f, 0.42f}, {0.72f, 0.10f, 0.72f}, 0.0f,
                             0.0f, 90.0f),
                   16);
    appendCylinder(v, i,
                   partXform({0.0f, -0.16f, -0.42f}, {0.72f, 0.10f, 0.72f}, 0.0f,
                             0.0f, 90.0f),
                   16);
    // 车架主梁（略前倾）
    appendBox(v, i, partXform({0.0f, 0.02f, 0.04f}, {0.06f, 0.06f, 0.74f}, 0.0f,
                              8.0f));
    // 座管
    appendBox(v, i, partXform({0.0f, 0.14f, -0.16f}, {0.05f, 0.34f, 0.05f}));
    // 车把立管
    appendBox(v, i, partXform({0.0f, 0.18f, 0.34f}, {0.05f, 0.36f, 0.05f}));
    // 车把（横向）
    appendBox(v, i, partXform({0.0f, 0.34f, 0.34f}, {0.46f, 0.05f, 0.05f}));
    // 车座
    appendBox(v, i, partXform({0.0f, 0.30f, -0.18f}, {0.16f, 0.06f, 0.30f}));
    if (!make(cycleFrame, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  骑行者：前倾躯干 + 伸向车把的手臂 + 踩踏双腿 + 头盔
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    appendBox(v, i, partXform({0.0f, 0.52f, -0.06f}, {0.40f, 0.50f, 0.34f}, 0.0f,
                              34.0f));
    appendBox(v, i, partXform({0.20f, 0.42f, 0.16f}, {0.11f, 0.36f, 0.12f}, 0.0f,
                              55.0f));
    appendBox(v, i, partXform({-0.20f, 0.42f, 0.16f}, {0.11f, 0.36f, 0.12f}, 0.0f,
                              55.0f));
    appendBox(v, i, partXform({0.14f, 0.22f, -0.14f}, {0.15f, 0.34f, 0.16f}, 0.0f,
                              -25.0f));
    appendBox(v, i, partXform({-0.14f, 0.22f, -0.14f}, {0.15f, 0.34f, 0.16f}, 0.0f,
                              -25.0f));
    // 头盔
    appendSphere(v, i, partXform({0.0f, 0.76f, 0.14f}, {0.30f, 0.26f, 0.32f}), 6,
                 10);
    if (!make(riderBody, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  锥桶：方形底座 + 圆锥主体 + 反光带
  //  这里终于用上真正的圆锥（原来是一个方盒子）
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 0.18f, 1.0f}));
    if (!make(coneBase, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    appendCone(v, i, partXform({0.0f, 0.0f, 0.0f}, {0.78f, 1.0f, 0.78f}), 16);
    // 反光带：扁平圆柱环
    appendCylinder(v, i,
                   partXform({0.0f, -0.18f, 0.0f}, {0.56f, 0.10f, 0.56f}), 16);
    if (!make(coneBody, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  街景 · 树干与树冠
  //  树冠用球体，渲染时每棵树叠 3 层不同大小的球形成蓬松感
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    // 树干（下粗上细，用两段圆柱叠出）
    appendCylinder(v, i, partXform({0.0f, -0.22f, 0.0f}, {0.16f, 0.56f, 0.16f}), 10);
    appendCylinder(v, i, partXform({0.0f, 0.22f, 0.0f}, {0.11f, 0.44f, 0.11f}), 10);
    // 根部外扩
    appendCylinder(v, i, partXform({0.0f, -0.47f, 0.0f}, {0.24f, 0.10f, 0.24f}), 10);
    if (!make(trunk, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    // 单位球（低多边形，靠多层叠加营造树叶团块感）
    appendSphere(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}), 7, 11);
    if (!make(canopy, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  街景 · 楼房
  //  主楼体 + 裙楼 + 楼顶设备间 + 窗带（自发光）
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    // 主楼体（单位：宽 1 / 深 1 / 高 1）
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    // 顶部女儿墙（略宽的压顶，勾勒楼顶轮廓）
    appendBox(v, i, partXform({0.0f, 0.50f, 0.0f}, {1.05f, 0.035f, 1.05f}));
    // 竖向壁柱（立面分格，让楼不像纯盒子）
    for (int s = -1; s <= 1; s += 2)
    {
      appendBox(v, i, partXform({static_cast<float>(s) * 0.5f, 0.0f, 0.0f},
                                {0.04f, 1.0f, 0.06f}));
      appendBox(v, i, partXform({0.0f, 0.0f, static_cast<float>(s) * 0.5f},
                                {0.06f, 1.0f, 0.04f}));
    }
    if (!make(buildingBody, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    // 裙楼（底部基座，比主楼宽）
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    // 入口雨棚
    appendBox(v, i, partXform({0.0f, 0.62f, 0.52f}, {0.44f, 0.06f, 0.22f}));
    if (!make(buildingBase, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    // 楼顶设备间 / 电梯机房
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    if (!make(buildingTop, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    // 窗带：四面环绕的薄片，自发光模拟夜间灯窗 / 白天玻璃反光
    for (int s = -1; s <= 1; s += 2)
    {
      appendBox(v, i, partXform({0.0f, 0.0f, static_cast<float>(s) * 0.5f},
                                {0.92f, 1.0f, 0.02f}));
      appendBox(v, i, partXform({static_cast<float>(s) * 0.5f, 0.0f, 0.0f},
                                {0.02f, 1.0f, 0.92f}));
    }
    if (!make(windowBand, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  街景 · 杆（路灯 / 电线杆共用）
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    appendCylinder(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}), 10);
    // 底座
    appendCylinder(v, i, partXform({0.0f, -0.48f, 0.0f}, {2.0f, 0.10f, 2.0f}), 10);
    if (!make(lampPole, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    // 悬臂：局部沿 +X 伸出，渲染时按朝向旋转
    appendBox(v, i, partXform({0.5f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    if (!make(lampArm, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    // 灯头：扁盒（自发光）
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    if (!make(lampHead, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  街景 · 广告牌 / 护栏 / 灌木
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    // 广告牌面板
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.06f}));
    // 上下边框
    appendBox(v, i, partXform({0.0f, 0.52f, 0.0f}, {1.06f, 0.05f, 0.10f}));
    appendBox(v, i, partXform({0.0f, -0.52f, 0.0f}, {1.06f, 0.05f, 0.10f}));
    if (!make(signPlate, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    // 护栏立柱
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    if (!make(railPost, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    // 护栏横梁（波形梁用两块错位薄板近似）
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    appendBox(v, i, partXform({0.0f, 0.0f, 0.35f}, {1.0f, 0.55f, 0.30f}));
    if (!make(railBeam, v, i))
      return false;
  }
  {
    VertexList v;
    IndexList i;
    // 灌木球（低多边形，渲染时随机缩放成丛）
    appendSphere(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 0.72f, 1.0f}), 6, 9);
    if (!make(bushBall, v, i))
      return false;
  }

  // ---------------------------------------------------------
  //  道路铺装 · 斑马线条纹板
  //  单位尺寸（X=1, Y=1 厚, Z=1）。渲染时沿 X 缩放成整条横道宽度，
  //  条纹间隔通过沿 Z 方向复制多块细板实现，
  //  这样「条纹方向」与真实斑马线一致。
  // ---------------------------------------------------------
  {
    VertexList v;
    IndexList i;
    appendBox(v, i, partXform({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    if (!make(roadSlab, v, i))
      return false;
  }

  return true;
}

void PropMeshes::destroy()
{
  ShapeMesh *meshes[] = {
      &carBody,     &carGlass,    &truckCab,    &truckCargo,  &truckGlass,
      &wheel,       &wheelRim,    &headLight,   &tailLight,   &pedBody,
      &pedHead,     &pedLegs,     &cycleFrame,  &riderBody,   &coneBase,
      &coneBody,    &trunk,       &canopy,      &buildingBody, &buildingBase,
      &buildingTop, &windowBand,  &lampPole,    &lampArm,     &lampHead,
      &signPlate,   &railPost,    &railBeam,    &bushBall,    &roadSlab};

  for (ShapeMesh *m : meshes)
    m->destroy();

  m_partCount = 0;
}

} // namespace vc
