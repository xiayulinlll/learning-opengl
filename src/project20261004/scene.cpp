#include "scene.h"

#include "core.h"

#include <map>

namespace mr
{

SceneObject &Scene::addObject(const std::string &name,
                              const std::string &modelPath,
                              const glm::vec3 &position)
{
  std::shared_ptr<Model> model;

  auto it = m_modelCache.find(modelPath);
  if (it != m_modelCache.end())
  {
    // 命中缓存，直接复用
    model = it->second;
    logInfo("复用已加载模型: " + modelPath);
  }
  else
  {
    model = std::make_shared<Model>();
    if (!model->load(modelPath))
    {
      logWarn("模型加载失败，使用空模型占位: " + modelPath);
    }
    m_modelCache[modelPath] = model;
  }

  SceneObject obj;
  obj.name = name;
  obj.model = model;
  obj.position = position;
  m_objects.push_back(obj);
  return m_objects.back();
}

void Scene::collectVisible(std::vector<const Model *> &models,
                           std::vector<glm::mat4> &matrices) const
{
  models.clear();
  matrices.clear();
  models.reserve(m_objects.size());
  matrices.reserve(m_objects.size());

  for (const SceneObject &obj : m_objects)
  {
    if (!obj.visible || !obj.model)
      continue;
    models.push_back(obj.model.get());
    matrices.push_back(obj.transform());
  }
}

} // namespace mr
