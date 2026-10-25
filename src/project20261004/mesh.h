#ifndef PROJECT_MESH_H
#define PROJECT_MESH_H

// ============================================================
//  Mini Renderer - 网格与模型
//  使用 assimp 加载模型，自动解析 顶点/法线/UV/切线 与 PBR 材质
// ============================================================

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <unordered_map>

#include "shader.h"

struct aiNode;
struct aiScene;
struct aiMesh;
struct aiMaterial;

namespace mr
{

// 单个顶点属性
struct Vertex
{
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texCoords;
  glm::vec3 tangent;
  glm::vec3 bitangent;
};

// PBR 材质参数
struct Material
{
  glm::vec3 albedo = glm::vec3(1.0f);
  float metallic = 0.0f;
  float roughness = 0.5f;
  float ao = 1.0f;

  // 纹理句柄（0 表示未使用）
  unsigned int albedoMap = 0;
  unsigned int normalMap = 0;
  unsigned int metallicMap = 0;
  unsigned int roughnessMap = 0;
  unsigned int aoMap = 0;

  bool hasAlbedoMap() const { return albedoMap != 0; }
  bool hasNormalMap() const { return normalMap != 0; }
  bool hasMetallicMap() const { return metallicMap != 0; }
  bool hasRoughnessMap() const { return roughnessMap != 0; }
  bool hasAoMap() const { return aoMap != 0; }
};

// 一个网格（对应一个 draw call）
class Mesh
{
public:
  Mesh(const std::vector<Vertex> &vertices,
       const std::vector<unsigned int> &indices,
       const Material &material);

  ~Mesh();

  void draw(const Shader &shader) const;

  const Material &material() const { return m_material; }
  Material &material() { return m_material; }

  unsigned int triangleCount() const { return m_indexCount / 3; }

private:
  void setup();

  unsigned int m_vao = 0;
  unsigned int m_vbo = 0;
  unsigned int m_ebo = 0;
  unsigned int m_indexCount = 0;
  Material m_material;

  void bindMaterialTextures(const Shader &shader) const;
};

// 模型：由若干 Mesh 组成
class Model
{
public:
  Model() = default;

  // 加载模型文件（.obj / .fbx / .gltf 等）
  bool load(const std::string &path);

  void draw(const Shader &shader) const;

  unsigned int meshCount() const { return static_cast<unsigned int>(m_meshes.size()); }
  unsigned int triangleCount() const;

  // 统计信息（供 ImGui 面板展示）
  struct Stats
  {
    unsigned int meshes = 0;
    unsigned int triangles = 0;
    unsigned int textures = 0;
  };
  Stats stats() const;

private:
  std::vector<Mesh> m_meshes;
  std::string m_directory;
  // 纹理缓存：文件名 -> GL 纹理 id，避免同一张贴图重复加载
  std::unordered_map<std::string, unsigned int> m_textureCache;

  void processNode(aiNode *node, const aiScene *scene);
  Mesh processMesh(aiMesh *mesh, const aiScene *scene);

  unsigned int loadMaterialTexture(aiMaterial *mat, int type);
};

} // namespace mr

#endif // PROJECT_MESH_H
