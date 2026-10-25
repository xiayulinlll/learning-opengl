#include "debugdraw.h"

#include <cmath>

namespace vc
{

// ============================================================
//  DebugLines
// ============================================================

DebugLines::~DebugLines()
{
  // 注意：GL 资源应在上下文有效时通过 destroy() 显式释放，
  // 析构函数里只做「尽力而为」，避免退出顺序问题导致崩溃。
}

void DebugLines::init()
{
  if (m_initialized)
    return;

  glGenVertexArrays(1, &m_vao);
  glGenBuffers(1, &m_vbo);

  glBindVertexArray(m_vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

  // 位置
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                        reinterpret_cast<void *>(offsetof(LineVertex, position)));
  // 颜色
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                        reinterpret_cast<void *>(offsetof(LineVertex, color)));

  glBindVertexArray(0);
  m_initialized = true;
}

void DebugLines::addLine(const glm::vec3 &a, const glm::vec3 &b,
                         const glm::vec3 &colorA, const glm::vec3 &colorB)
{
  m_vertices.push_back({a, colorA});
  m_vertices.push_back({b, colorB});
}

void DebugLines::addLine(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &color)
{
  addLine(a, b, color, color);
}

void DebugLines::addBox(const glm::vec3 &min, const glm::vec3 &max, const glm::vec3 &color)
{
  // 8 个角点
  const glm::vec3 c[8] = {
      {min.x, min.y, min.z}, {max.x, min.y, min.z},
      {max.x, max.y, min.z}, {min.x, max.y, min.z},
      {min.x, min.y, max.z}, {max.x, min.y, max.z},
      {max.x, max.y, max.z}, {min.x, max.y, max.z}};

  // 12 条棱
  const int edges[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0}, // 前面
      {4, 5}, {5, 6}, {6, 7}, {7, 4}, // 后面
      {0, 4}, {1, 5}, {2, 6}, {3, 7}  // 连接棱
  };

  for (auto &e : edges)
    addLine(c[e[0]], c[e[1]], color);
}

void DebugLines::addOrientedBox(const glm::vec3 &center, const glm::vec3 &size,
                                float yawDeg, const glm::vec3 &color)
{
  // 在局部空间构造半尺寸盒子的 8 个角点，再旋转 + 平移到世界空间
  glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
  model = glm::rotate(model, glm::radians(yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));

  const glm::vec3 h = size * 0.5f;
  const glm::vec3 local[8] = {
      {-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z},
      {h.x, h.y, -h.z}, {-h.x, h.y, -h.z},
      {-h.x, -h.y, h.z}, {h.x, -h.y, h.z},
      {h.x, h.y, h.z}, {-h.x, h.y, h.z}};

  glm::vec3 world[8];
  for (int i = 0; i < 8; ++i)
    world[i] = glm::vec3(model * glm::vec4(local[i], 1.0f));

  const int edges[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0},
      {4, 5}, {5, 6}, {6, 7}, {7, 4},
      {0, 4}, {1, 5}, {2, 6}, {3, 7}};

  for (auto &e : edges)
    addLine(world[e[0]], world[e[1]], color);
}

void DebugLines::addCircle(const glm::vec3 &center, float radius, int segments,
                           const glm::vec3 &color)
{
  if (segments < 3)
    segments = 3;

  const float step = 2.0f * 3.14159265358979f / static_cast<float>(segments);
  glm::vec3 prev(center.x + radius, center.y, center.z);
  for (int i = 1; i <= segments; ++i)
  {
    float a = step * static_cast<float>(i);
    glm::vec3 cur(center.x + radius * std::cos(a), center.y,
                  center.z + radius * std::sin(a));
    addLine(prev, cur, color);
    prev = cur;
  }
}

void DebugLines::addGrid(float halfSize, int divisions, const glm::vec3 &color,
                         const glm::vec3 &axisColor)
{
  if (divisions < 1)
    divisions = 1;

  const float step = (halfSize * 2.0f) / static_cast<float>(divisions);
  for (int i = 0; i <= divisions; ++i)
  {
    float p = -halfSize + step * static_cast<float>(i);
    // 与坐标轴重合的线用高亮色，便于定位原点
    const bool isAxis = std::fabs(p) < 1e-4f;
    addLine(glm::vec3(p, 0.0f, -halfSize), glm::vec3(p, 0.0f, halfSize),
            isAxis ? axisColor : color);
    addLine(glm::vec3(-halfSize, 0.0f, p), glm::vec3(halfSize, 0.0f, p),
            isAxis ? axisColor : color);
  }
}

void DebugLines::flush()
{
  if (!m_initialized || m_vertices.empty())
  {
    m_uploaded = 0;
    return;
  }

  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

  const unsigned int needed = static_cast<unsigned int>(m_vertices.size());
  const size_t bytes = m_vertices.size() * sizeof(LineVertex);

  if (needed > m_capacity)
  {
    // 容量不足时重新分配，并留出 2 倍余量减少后续 realloc
    unsigned int newCapacity = needed * 2;
    glBufferData(GL_ARRAY_BUFFER, newCapacity * sizeof(LineVertex), nullptr, GL_DYNAMIC_DRAW);
    m_capacity = newCapacity;
  }

  // 每帧整块覆盖上传，比逐条更新更简单也更快
  glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, m_vertices.data());
  m_uploaded = needed;
}

void DebugLines::draw() const
{
  if (!m_initialized || m_uploaded == 0)
    return;

  glBindVertexArray(m_vao);
  glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_uploaded));
  glBindVertexArray(0);
}

void DebugLines::destroy()
{
  if (m_vbo)
    glDeleteBuffers(1, &m_vbo);
  if (m_vao)
    glDeleteVertexArrays(1, &m_vao);
  m_vbo = m_vao = 0;
  m_capacity = m_uploaded = 0;
  m_initialized = false;
}

// ============================================================
//  ShapeMesh
// ============================================================

ShapeMesh::~ShapeMesh()
{
}

bool ShapeMesh::createBox()
{
  // 单位立方体：每面 4 顶点（保证法线是硬边），边长 1，中心在原点
  const float h = 0.5f;
  const float vertices[] = {
      // +X 面
      h, -h, -h, 1, 0, 0, h, -h, h, 1, 0, 0, h, h, h, 1, 0, 0, h, h, -h, 1, 0, 0,
      // -X 面
      -h, -h, h, -1, 0, 0, -h, -h, -h, -1, 0, 0, -h, h, -h, -1, 0, 0, -h, h, h, -1, 0, 0,
      // +Y 面
      -h, h, h, 0, 1, 0, h, h, h, 0, 1, 0, h, h, -h, 0, 1, 0, -h, h, -h, 0, 1, 0,
      // -Y 面
      -h, -h, -h, 0, -1, 0, h, -h, -h, 0, -1, 0, h, -h, h, 0, -1, 0, -h, -h, h, 0, -1, 0,
      // +Z 面
      -h, -h, h, 0, 0, 1, h, -h, h, 0, 0, 1, h, h, h, 0, 0, 1, -h, h, h, 0, 0, 1,
      // -Z 面
      h, -h, -h, 0, 0, -1, -h, -h, -h, 0, 0, -1, -h, h, -h, 0, 0, -1, h, h, -h, 0, 0, -1};

  // 6 个面各自的索引（相对该面 4 个顶点）
  std::vector<unsigned int> indices;
  for (unsigned int f = 0; f < 6; ++f)
  {
    unsigned int base = f * 4;
    indices.insert(indices.end(),
                   {base, base + 1, base + 2, base + 2, base + 3, base});
  }

  build(std::vector<float>(vertices, vertices + sizeof(vertices) / sizeof(float)),
        indices, true);
  return true;
}

bool ShapeMesh::createCylinder(int radialSegments)
{
  if (radialSegments < 3)
    radialSegments = 3;

  const float radius = 0.5f;
  const float halfH = 0.5f;
  const float PI = 3.14159265358979f;

  std::vector<float> vertices;
  std::vector<unsigned int> indices;

  // ---- 侧面：每个角度放上下两个顶点，法线沿径向 ----
  for (int i = 0; i <= radialSegments; ++i)
  {
    float a = 2.0f * PI * static_cast<float>(i) / static_cast<float>(radialSegments);
    float x = radius * std::cos(a);
    float z = radius * std::sin(a);
    vertices.insert(vertices.end(), {x, -halfH, z, x / radius, 0.0f, z / radius});
    vertices.insert(vertices.end(), {x, halfH, z, x / radius, 0.0f, z / radius});
  }
  for (int i = 0; i < radialSegments; ++i)
  {
    unsigned int b = static_cast<unsigned int>(i) * 2;
    indices.insert(indices.end(), {b, b + 2, b + 1, b + 1, b + 2, b + 3});
  }

  // ---- 顶盖：中心点 + 扇形三角 ----
  unsigned int topCenter = static_cast<unsigned int>(vertices.size() / 6);
  vertices.insert(vertices.end(), {0.0f, halfH, 0.0f, 0.0f, 1.0f, 0.0f});
  unsigned int topStart = static_cast<unsigned int>(vertices.size() / 6);
  for (int i = 0; i <= radialSegments; ++i)
  {
    float a = 2.0f * PI * static_cast<float>(i) / static_cast<float>(radialSegments);
    vertices.insert(vertices.end(),
                    {radius * std::cos(a), halfH, radius * std::sin(a), 0.0f, 1.0f, 0.0f});
  }
  for (int i = 0; i < radialSegments; ++i)
    indices.insert(indices.end(), {topCenter, topStart + i, topStart + i + 1});

  // ---- 底盖：绕序与顶盖相反 ----
  unsigned int botCenter = static_cast<unsigned int>(vertices.size() / 6);
  vertices.insert(vertices.end(), {0.0f, -halfH, 0.0f, 0.0f, -1.0f, 0.0f});
  unsigned int botStart = static_cast<unsigned int>(vertices.size() / 6);
  for (int i = 0; i <= radialSegments; ++i)
  {
    float a = 2.0f * PI * static_cast<float>(i) / static_cast<float>(radialSegments);
    vertices.insert(vertices.end(),
                    {radius * std::cos(a), -halfH, radius * std::sin(a), 0.0f, -1.0f, 0.0f});
  }
  for (int i = 0; i < radialSegments; ++i)
    indices.insert(indices.end(), {botCenter, botStart + i + 1, botStart + i});

  build(vertices, indices, true);
  return true;
}

bool ShapeMesh::createPlane()
{
  // 单位平面：位于 XZ 平面，边长 1，中心在原点，法线 +Y
  const float h = 0.5f;
  const float vertices[] = {
      -h, 0.0f, -h, 0.0f, 1.0f, 0.0f,
      h, 0.0f, -h, 0.0f, 1.0f, 0.0f,
      h, 0.0f, h, 0.0f, 1.0f, 0.0f,
      -h, 0.0f, h, 0.0f, 1.0f, 0.0f};
  const std::vector<unsigned int> indices = {0, 1, 2, 2, 3, 0};

  build(std::vector<float>(vertices, vertices + sizeof(vertices) / sizeof(float)),
        indices, true);
  return true;
}

bool ShapeMesh::createFromData(const std::vector<float> &vertices,
                               const std::vector<unsigned int> &indices)
{
  if (vertices.empty() || indices.empty())
    return false;

  // build() 内部会自行生成 VAO/VBO/EBO，因此这里不需要预检 m_vao
  build(vertices, indices, true);
  return true;
}

void ShapeMesh::build(const std::vector<float> &vertices,
                      const std::vector<unsigned int> &indices,
                      bool withNormal)
{
  (void)withNormal;

  m_indexCount = static_cast<unsigned int>(indices.size());
  m_triangleCount = m_indexCount / 3;

  glGenVertexArrays(1, &m_vao);
  glGenBuffers(1, &m_vbo);
  glGenBuffers(1, &m_ebo);

  glBindVertexArray(m_vao);

  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // 顶点布局：位置(3) + 法线(3)，共 6 float
  const GLsizei stride = 6 * sizeof(float);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void *>(3 * sizeof(float)));

  glBindVertexArray(0);
}

void ShapeMesh::draw() const
{
  if (m_indexCount == 0)
    return;
  glBindVertexArray(m_vao);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

void ShapeMesh::setupInstancing()
{
  if (m_instancingReady || m_vao == 0)
    return;

  glGenVertexArrays(1, &m_instancedVao);
  glGenBuffers(1, &m_instanceVbo);

  glBindVertexArray(m_instancedVao);

  // 复用网格自身的 VBO / EBO（顶点与索引数据是共享的）
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  const GLsizei stride = 6 * sizeof(float);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void *>(3 * sizeof(float)));

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);

  // 实例属性：mat4（占 location 2..5）+ vec3 颜色（location 6）
  glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
  const GLsizei instStride = sizeof(glm::mat4) + sizeof(glm::vec3);

  for (int i = 0; i < 4; ++i)
  {
    glEnableVertexAttribArray(2 + i);
    glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, instStride,
                          reinterpret_cast<void *>(sizeof(glm::vec4) * i));
    glVertexAttribDivisor(2 + i, 1); // 每个实例前进一次
  }

  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, instStride,
                        reinterpret_cast<void *>(sizeof(glm::mat4)));
  glVertexAttribDivisor(6, 1);

  glBindVertexArray(0);
  m_instancingReady = true;
}

void ShapeMesh::uploadInstances(const std::vector<glm::mat4> &transforms,
                                const std::vector<glm::vec3> &colors)
{
  if (!m_instancingReady)
    return;

  const size_t count = (transforms.size() < colors.size()) ? transforms.size() : colors.size();
  if (count == 0)
  {
    m_instanceCount = 0;
    return;
  }

  // 交错打包：mat4 + vec3，与 setupInstancing() 的 stride 布局严格对应
  std::vector<float> packed;
  packed.reserve(count * 19);
  for (size_t i = 0; i < count; ++i)
  {
    const float *m = glm::value_ptr(transforms[i]);
    packed.insert(packed.end(), m, m + 16);
    packed.push_back(colors[i].x);
    packed.push_back(colors[i].y);
    packed.push_back(colors[i].z);
  }

  glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
  if (count > m_instanceCapacity)
  {
    glBufferData(GL_ARRAY_BUFFER, packed.size() * sizeof(float), packed.data(), GL_DYNAMIC_DRAW);
    m_instanceCapacity = static_cast<unsigned int>(count);
  }
  else
  {
    // 容量够就走子更新，避免每帧重新分配显存
    glBufferSubData(GL_ARRAY_BUFFER, 0, packed.size() * sizeof(float), packed.data());
  }

  m_instanceCount = static_cast<unsigned int>(count);
}

void ShapeMesh::drawInstanced() const
{
  if (!m_instancingReady || m_instanceCount == 0 || m_indexCount == 0)
    return;

  glBindVertexArray(m_instancedVao);
  glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount),
                          GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(m_instanceCount));
  glBindVertexArray(0);
}

void ShapeMesh::destroy()
{
  if (m_ebo)
    glDeleteBuffers(1, &m_ebo);
  if (m_vbo)
    glDeleteBuffers(1, &m_vbo);
  if (m_vao)
    glDeleteVertexArrays(1, &m_vao);
  if (m_instanceVbo)
    glDeleteBuffers(1, &m_instanceVbo);
  if (m_instancedVao)
    glDeleteVertexArrays(1, &m_instancedVao);

  m_vao = m_vbo = m_ebo = 0;
  m_instancedVao = m_instanceVbo = 0;
  m_indexCount = m_triangleCount = 0;
  m_instanceCount = m_instanceCapacity = 0;
  m_instancingReady = false;
}

} // namespace vc
