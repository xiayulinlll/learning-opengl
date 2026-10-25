#ifndef PROJECT_SCENE_H
#define PROJECT_SCENE_H

// ============================================================
//  Mini Renderer - 场景管理
//  管理一组渲染对象（模型 + 变换），并提供显式生命周期控制
//  注意：Model 持有 GL 资源，需保证在 GL 上下文有效期间析构
// ============================================================

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mesh.h"

namespace mr
{

// 场景中的一个实体
struct SceneObject
{
  std::string name;
  std::shared_ptr<Model> model;
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 rotation = glm::vec3(0.0f); // 欧拉角（度）
  glm::vec3 scale = glm::vec3(1.0f);
  bool visible = true;

  glm::mat4 transform() const
  {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, position);
    m = glm::rotate(m, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::scale(m, scale);
    return m;
  }
};

class Scene
{
public:
  // 添加一个对象；模型会被缓存复用（同一路径只加载一次）
  SceneObject &addObject(const std::string &name,
                         const std::string &modelPath,
                         const glm::vec3 &position = glm::vec3(0.0f));

  std::vector<SceneObject> &objects() { return m_objects; }
  const std::vector<SceneObject> &objects() const { return m_objects; }

  // 收集当前可见对象（用于渲染）
  void collectVisible(std::vector<const Model *> &models,
                      std::vector<glm::mat4> &matrices) const;

  unsigned int objectCount() const { return static_cast<unsigned int>(m_objects.size()); }

private:
  std::vector<SceneObject> m_objects;
  // 模型缓存：路径 -> 模型，避免同一模型重复加载
  std::map<std::string, std::shared_ptr<Model>> m_modelCache;
};

} // namespace mr

#endif // PROJECT_SCENE_H
