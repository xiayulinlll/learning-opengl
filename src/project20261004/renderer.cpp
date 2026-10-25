#include "renderer.h"

#include "core.h"

#include <glm/gtc/matrix_transform.hpp>

namespace mr
{

bool Renderer::init(unsigned int shadowWidth, unsigned int shadowHeight)
{
  m_shadowWidth = shadowWidth;
  m_shadowHeight = shadowHeight;

  // ---------------- 加载着色器 ----------------
  // 着色器位于本项目自己的 shader/ 子目录（相对于工程根目录）
  const std::string shaderDir = "src/project20261004/shader/";
  if (!m_pbrShader.loadFromFiles(shaderDir + "pbr.vert", shaderDir + "pbr.frag"))
    return false;
  if (!m_depthShader.loadFromFiles(shaderDir + "depth.vert", shaderDir + "depth.frag"))
    return false;
  if (!m_skyboxShader.loadFromFiles(shaderDir + "skybox.vert", shaderDir + "skybox.frag"))
    return false;

  // ---------------- 阴影贴图 FBO ----------------
  glGenFramebuffers(1, &m_shadowFbo);
  glGenTextures(1, &m_shadowMap);

  glBindTexture(GL_TEXTURE_2D, m_shadowMap);
  // 深度纹理：使用 DEPTH_COMPONENT24 提高精度，减少阴影锯齿
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowWidth, shadowHeight,
               0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  // 边缘使用 CLAMP_TO_BORDER + 白色，使阴影贴图外的区域不产生阴影
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

  glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowMap, 0);
  // 只需要深度信息，显式声明不写颜色
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
  {
    logError("阴影 FBO 创建失败");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return false;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  createSkyboxGeometry();
  computeLightSpaceMatrix();

  logInfo("渲染器初始化完成");
  return true;
}

void Renderer::destroy()
{
  if (m_shadowFbo)
    glDeleteFramebuffers(1, &m_shadowFbo);
  if (m_shadowMap)
    glDeleteTextures(1, &m_shadowMap);
  if (m_skyboxVao)
    glDeleteVertexArrays(1, &m_skyboxVao);

  m_skybox.destroy();
  m_pbrShader.destroy();
  m_depthShader.destroy();
  m_skyboxShader.destroy();
}

void Renderer::setPointLightCount(unsigned int count)
{
  m_pointLightCount = (count > 4) ? 4 : count;
}

// 创建立方体几何（用于天空盒）
void Renderer::createSkyboxGeometry()
{
  float vertices[] = {
      // 后
      -1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f,
      1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
      // 前
      -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
      -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f,
      // 左
      1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
      1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f,
      // 右
      -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
      1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f,
      // 上
      -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f,
      1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f,
      // 下
      -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f,
      1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};

  unsigned int vbo;
  glGenVertexArrays(1, &m_skyboxVao);
  glGenBuffers(1, &vbo);

  glBindVertexArray(m_skyboxVao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
  glBindVertexArray(0);
}

bool Renderer::loadSkybox(const std::vector<std::string> &faces)
{
  return m_skybox.load(faces);
}

// 由方向光包围盒计算光源空间矩阵（正交投影，覆盖场景范围）
void Renderer::computeLightSpaceMatrix()
{
  // 场景大致在半径 20 的范围内
  const float extent = 20.0f;
  glm::vec3 lightDir = glm::normalize(m_dirLight.direction);

  // 从场景中心沿光线反向平移，得到光源位置
  glm::vec3 center = glm::vec3(0.0f);
  glm::vec3 lightPos = center - lightDir * extent;

  glm::mat4 lightView = glm::lookAt(lightPos, center, glm::vec3(0.0f, 1.0f, 0.0f));
  glm::mat4 lightProj = glm::ortho(-extent, extent, -extent, extent, 0.1f, extent * 3.0f);

  m_lightSpaceMatrix = lightProj * lightView;
}

void Renderer::renderShadowPass(const std::vector<const Model *> &models,
                                const std::vector<glm::mat4> &modelMatrices)
{
  computeLightSpaceMatrix();

  glViewport(0, 0, m_shadowWidth, m_shadowHeight);
  glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
  glClear(GL_DEPTH_BUFFER_BIT);

  // 阴影渲染时开启正面剔除，可进一步缓解阴影痤疮
  glEnable(GL_DEPTH_TEST);
  glCullFace(GL_FRONT);

  m_depthShader.use();
  m_depthShader.setMat4("uLightSpaceMatrix", m_lightSpaceMatrix);

  for (size_t i = 0; i < models.size(); i++)
  {
    m_depthShader.setMat4("uModel", modelMatrices[i]);
    models[i]->draw(m_depthShader);
  }

  // 恢复默认剔除设置
  glCullFace(GL_BACK);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::uploadLights(const glm::vec3 &cameraPos)
{
  m_pbrShader.use();

  // 方向光
  m_pbrShader.setVec3("uDirLight.direction", m_dirLight.direction);
  m_pbrShader.setVec3("uDirLight.color", m_dirLight.color);
  m_pbrShader.setFloat("uDirLight.intensity", m_dirLight.intensity);
  m_pbrShader.setInt("uDirLight.castShadow", m_dirLight.castShadow ? 1 : 0);
  m_pbrShader.setFloat("uDirLight.shadowBias", m_dirLight.shadowBias);

  // 点光源
  m_pbrShader.setInt("uPointLightCount", static_cast<int>(m_pointLightCount));
  for (unsigned int i = 0; i < m_pointLightCount; i++)
  {
    std::string base = "uPointLights[" + std::to_string(i) + "]";
    m_pbrShader.setVec3(base + ".position", m_pointLights[i].position);
    m_pbrShader.setVec3(base + ".color", m_pointLights[i].color);
    m_pbrShader.setFloat(base + ".intensity", m_pointLights[i].intensity);
    m_pbrShader.setFloat(base + ".constant", m_pointLights[i].constant);
    m_pbrShader.setFloat(base + ".linear", m_pointLights[i].linear);
    m_pbrShader.setFloat(base + ".quadratic", m_pointLights[i].quadratic);
    m_pbrShader.setInt(base + ".enabled", m_pointLights[i].enabled ? 1 : 0);
  }

  // 聚光灯（内外角以 cos 值传入）
  m_pbrShader.setVec3("uSpotLight.position", m_spotLight.position);
  m_pbrShader.setVec3("uSpotLight.direction", m_spotLight.direction);
  m_pbrShader.setVec3("uSpotLight.color", m_spotLight.color);
  m_pbrShader.setFloat("uSpotLight.intensity", m_spotLight.intensity);
  m_pbrShader.setFloat("uSpotLight.cutOff", glm::cos(glm::radians(m_spotLight.cutOff)));
  m_pbrShader.setFloat("uSpotLight.outerCutOff", glm::cos(glm::radians(m_spotLight.outerCutOff)));
  m_pbrShader.setFloat("uSpotLight.constant", m_spotLight.constant);
  m_pbrShader.setFloat("uSpotLight.linear", m_spotLight.linear);
  m_pbrShader.setFloat("uSpotLight.quadratic", m_spotLight.quadratic);
  m_pbrShader.setInt("uSpotLight.enabled", m_spotLight.enabled ? 1 : 0);

  // 相机位置与光照空间矩阵
  m_pbrShader.setVec3("uCameraPos", cameraPos);
  m_pbrShader.setMat4("uLightSpaceMatrix", m_lightSpaceMatrix);

  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, m_shadowMap);
  m_pbrShader.setInt("uShadowMap", 5);
}


void Renderer::renderScene(const std::vector<const Model *> &models,
                           const std::vector<glm::mat4> &modelMatrices,
                           const glm::mat4 &view,
                           const glm::mat4 &projection,
                           const glm::vec3 &cameraPos)
{
  m_stats = RenderStats();

  m_pbrShader.use();
  m_pbrShader.setMat4("uView", view);
  m_pbrShader.setMat4("uProjection", projection);

  uploadLights(cameraPos);

  for (size_t i = 0; i < models.size(); i++)
  {
    const Model *model = models[i];
    const glm::mat4 &modelMatrix = modelMatrices[i];

    m_pbrShader.setMat4("uModel", modelMatrix);
    // 法线矩阵：模型矩阵的逆转置，用于正确处理非等比缩放
    m_pbrShader.setMat3("uNormalMatrix", glm::mat3(glm::transpose(glm::inverse(modelMatrix))));

    model->draw(m_pbrShader);

    m_stats.drawCalls += model->meshCount();
    m_stats.triangles += model->triangleCount();
  }
}

void Renderer::renderSkybox(const glm::mat4 &view, const glm::mat4 &projection)
{
  if (!m_skyboxEnabled)
    return;

  // 天空盒位于最远处，用 LEQUAL 让其在 z = 1.0 处通过深度测试
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);

  m_skyboxShader.use();
  m_skyboxShader.setMat4("uView", view);
  m_skyboxShader.setMat4("uProjection", projection);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, m_skybox.id());
  m_skyboxShader.setInt("uSkybox", 0);

  glBindVertexArray(m_skyboxVao);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);

  glDepthFunc(GL_LESS);
  glEnable(GL_CULL_FACE);
  m_stats.drawCalls++;
}

} // namespace mr
