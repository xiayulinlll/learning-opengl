#ifndef PROJECT_LIGHT_H
#define PROJECT_LIGHT_H

// ============================================================
//  Mini Renderer - 光源定义
// ============================================================

#include <glm/glm.hpp>

namespace mr
{

// 平行光（模拟太阳）
struct DirectionalLight
{
  glm::vec3 direction = glm::vec3(-0.3f, -1.0f, -0.4f); // 光传播方向
  glm::vec3 color = glm::vec3(1.0f);
  float intensity = 3.0f;

  bool castShadow = true;
  float shadowBias = 0.003f;
};

// 点光源（带衰减）
struct PointLight
{
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 color = glm::vec3(1.0f);
  float intensity = 20.0f;
  float constant = 1.0f;
  float linear = 0.09f;
  float quadratic = 0.032f;

  bool enabled = true;

  float radius() const { return 12.0f; }
};

// 聚光灯
struct SpotLight
{
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
  glm::vec3 color = glm::vec3(1.0f);

  float intensity = 30.0f;
  float cutOff = 12.5f;      // 内圆锥角（度）
  float outerCutOff = 17.5f; // 外圆锥角（度）

  float constant = 1.0f;
  float linear = 0.09f;
  float quadratic = 0.032f;

  bool enabled = false;
};

} // namespace mr

#endif // PROJECT_LIGHT_H
