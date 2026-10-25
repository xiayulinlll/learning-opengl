#ifndef PROJECT_RENDERER_H
#define PROJECT_RENDERER_H

// ============================================================
//  Mini Renderer - 渲染管线
//  职责：
//    1. 管理阴影贴图（方向光的深度 FBO）
//    2. 上传光照 / 材质 uniform 到 PBR 着色器
//    3. 绘制天空盒
//    4. 统计渲染开销（draw call / 三角面）
// ============================================================

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>

#include "light.h"
#include "mesh.h"
#include "shader.h"
#include "texture.h"

namespace mr
{

class Renderer
{
public:
  Renderer() = default;

  // 初始化阴影贴图与天空盒资源
  bool init(unsigned int shadowWidth = 2048, unsigned int shadowHeight = 2048);

  void destroy();

  // ---------- 光照配置 ----------
  DirectionalLight &dirLight() { return m_dirLight; }
  PointLight &pointLight(unsigned int index) { return m_pointLights[index]; }
  unsigned int pointLightCount() const { return m_pointLightCount; }
  void setPointLightCount(unsigned int count);
  SpotLight &spotLight() { return m_spotLight; }

  // ---------- 天空盒 ----------
  bool loadSkybox(const std::vector<std::string> &faces);
  void setSkyboxEnabled(bool enabled) { m_skyboxEnabled = enabled; }
  bool skyboxEnabled() const { return m_skyboxEnabled; }
  Cubemap &skybox() { return m_skybox; }

  // ---------- 渲染流程 ----------
  // 第一步：从光源视角渲染深度到阴影贴图
  void renderShadowPass(const std::vector<const Model *> &models,
                        const std::vector<glm::mat4> &modelMatrices);

  // 第二步：渲染主场景
  void renderScene(const std::vector<const Model *> &models,
                   const std::vector<glm::mat4> &modelMatrices,
                   const glm::mat4 &view,
                   const glm::mat4 &projection,
                   const glm::vec3 &cameraPos);

  // 第三步：绘制天空盒（需在主场景之后，深度函数为 LEQUAL）
  void renderSkybox(const glm::mat4 &view, const glm::mat4 &projection);

  // ---------- 访问器 ----------
  Shader &pbrShader() { return m_pbrShader; }
  glm::mat4 lightSpaceMatrix() const { return m_lightSpaceMatrix; }
  bool showShadowMap() const { return m_showShadowMap; }
  void setShowShadowMap(bool v) { m_showShadowMap = v; }

  // 渲染统计
  struct RenderStats
  {
    unsigned int drawCalls = 0;
    unsigned int triangles = 0;
  };
  const RenderStats &stats() const { return m_stats; }
  void resetStats() { m_stats = RenderStats(); }

private:
  Shader m_pbrShader;
  Shader m_depthShader;
  Shader m_skyboxShader;

  DirectionalLight m_dirLight;
  PointLight m_pointLights[4];
  unsigned int m_pointLightCount = 1;
  SpotLight m_spotLight;

  Cubemap m_skybox;
  bool m_skyboxEnabled = true;
  unsigned int m_skyboxVao = 0;

  // 阴影资源
  unsigned int m_shadowFbo = 0;
  unsigned int m_shadowMap = 0;
  unsigned int m_shadowWidth = 0;
  unsigned int m_shadowHeight = 0;
  glm::mat4 m_lightSpaceMatrix = glm::mat4(1.0f);
  bool m_showShadowMap = false;

  RenderStats m_stats;

  void uploadLights(const glm::vec3 &cameraPos);
  void computeLightSpaceMatrix();
  void createSkyboxGeometry();
};

} // namespace mr

#endif // PROJECT_RENDERER_H
