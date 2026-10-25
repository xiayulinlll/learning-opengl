#include "mesh.h"

#include "core.h"
#include "texture.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cmath>
#include <cstddef>

namespace mr
{

// ============================== Mesh ==============================

Mesh::Mesh(const std::vector<Vertex> &vertices,
           const std::vector<unsigned int> &indices,
           const Material &material)
    : m_indexCount(static_cast<unsigned int>(indices.size())), m_material(material)
{
  glGenVertexArrays(1, &m_vao);
  glGenBuffers(1, &m_vbo);
  glGenBuffers(1, &m_ebo);

  glBindVertexArray(m_vao);

  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

  // 顶点属性布局，与 shader 中的 layout(location=N) 一一对应
  glEnableVertexAttribArray(0); // position
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));

  glEnableVertexAttribArray(1); // normal
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));

  glEnableVertexAttribArray(2); // texCoords
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoords));

  glEnableVertexAttribArray(3); // tangent
  glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, tangent));

  glEnableVertexAttribArray(4); // bitangent
  glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, bitangent));

  glBindVertexArray(0);
}

Mesh::~Mesh()
{
  if (m_ebo)
    glDeleteBuffers(1, &m_ebo);
  if (m_vbo)
    glDeleteBuffers(1, &m_vbo);
  if (m_vao)
    glDeleteVertexArrays(1, &m_vao);
}

void Mesh::bindMaterialTextures(const Shader &shader) const
{
  // 约定纹理单元分配：
  //   0 albedo  1 normal  2 metallic  3 roughness  4 ao
  //   5 shadowMap  6 irradiance  7 prefilter  8 brdfLUT
  if (m_material.hasAlbedoMap())
  {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_material.albedoMap);
    shader.setInt("uMaterial.albedoMap", 0);
  }
  shader.setInt("uMaterial.hasAlbedoMap", m_material.hasAlbedoMap() ? 1 : 0);

  if (m_material.hasNormalMap())
  {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_material.normalMap);
    shader.setInt("uMaterial.normalMap", 1);
  }
  shader.setInt("uMaterial.hasNormalMap", m_material.hasNormalMap() ? 1 : 0);

  if (m_material.hasMetallicMap())
  {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_material.metallicMap);
    shader.setInt("uMaterial.metallicMap", 2);
  }
  shader.setInt("uMaterial.hasMetallicMap", m_material.hasMetallicMap() ? 1 : 0);

  if (m_material.hasRoughnessMap())
  {
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_material.roughnessMap);
    shader.setInt("uMaterial.roughnessMap", 3);
  }
  shader.setInt("uMaterial.hasRoughnessMap", m_material.hasRoughnessMap() ? 1 : 0);

  if (m_material.hasAoMap())
  {
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_material.aoMap);
    shader.setInt("uMaterial.aoMap", 4);
  }
  shader.setInt("uMaterial.hasAoMap", m_material.hasAoMap() ? 1 : 0);

  // 回到默认纹理单元，避免影响后续绘制
  glActiveTexture(GL_TEXTURE0);
}

void Mesh::draw(const Shader &shader) const
{
  bindMaterialTextures(shader);

  glBindVertexArray(m_vao);
  glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
}

// ============================== Model ==============================

bool Model::load(const std::string &path)
{
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(
      path,
      aiProcess_Triangulate |
          aiProcess_GenSmoothNormals |
          aiProcess_FlipUVs |
          aiProcess_CalcTangentSpace |
          aiProcess_JoinIdenticalVertices);

  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
  {
    logError("Assimp 加载失败: " + path + " -> " + importer.GetErrorString());
    return false;
  }

  // 记录模型所在目录，用于解析相对纹理路径
  size_t pos = path.find_last_of("/\\");
  m_directory = (pos == std::string::npos) ? "." : path.substr(0, pos);

  processNode(scene->mRootNode, scene);
  logInfo("模型加载成功: " + path +
          " (mesh 数 = " + std::to_string(m_meshes.size()) + ")");
  return true;
}

void Model::processNode(aiNode *node, const aiScene *scene)
{
  for (unsigned int i = 0; i < node->mNumMeshes; i++)
  {
    aiMesh *aiM = scene->mMeshes[node->mMeshes[i]];
    m_meshes.push_back(processMesh(aiM, scene));
  }

  for (unsigned int i = 0; i < node->mNumChildren; i++)
    processNode(node->mChildren[i], scene);
}

Mesh Model::processMesh(aiMesh *aiM, const aiScene *scene)
{
  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;
  Material material;

  vertices.reserve(aiM->mNumVertices);

  for (unsigned int i = 0; i < aiM->mNumVertices; i++)
  {
    Vertex v;
    v.position = glm::vec3(aiM->mVertices[i].x, aiM->mVertices[i].y, aiM->mVertices[i].z);

    if (aiM->HasNormals())
      v.normal = glm::vec3(aiM->mNormals[i].x, aiM->mNormals[i].y, aiM->mNormals[i].z);
    else
      v.normal = glm::vec3(0.0f);

    // 只取第一套 UV
    if (aiM->mTextureCoords[0])
      v.texCoords = glm::vec2(aiM->mTextureCoords[0][i].x, aiM->mTextureCoords[0][i].y);
    else
      v.texCoords = glm::vec2(0.0f);

    if (aiM->HasTangentsAndBitangents())
    {
      v.tangent = glm::vec3(aiM->mTangents[i].x, aiM->mTangents[i].y, aiM->mTangents[i].z);
      v.bitangent = glm::vec3(aiM->mBitangents[i].x, aiM->mBitangents[i].y, aiM->mBitangents[i].z);
    }
    else
    {
      v.tangent = glm::vec3(0.0f);
      v.bitangent = glm::vec3(0.0f);
    }

    vertices.push_back(v);
  }

  for (unsigned int i = 0; i < aiM->mNumFaces; i++)
  {
    aiFace face = aiM->mFaces[i];
    for (unsigned int j = 0; j < face.mNumIndices; j++)
      indices.push_back(face.mIndices[j]);
  }

  // ------- 材质 -------
  if (aiM->mMaterialIndex >= 0)
  {
    aiMaterial *mat = scene->mMaterials[aiM->mMaterialIndex];

    aiColor3D diffuse(1.0f, 1.0f, 1.0f);
    if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
      material.albedo = glm::vec3(diffuse.r, diffuse.g, diffuse.b);

    // 说明：当前 assimp 版本仅提供传统 Blinn-Phong 材质属性，
    // 这里用镜面指数 shininess 反推一个近似的粗糙度：
    //   shininess 越大 -> 高光越锐利 -> 粗糙度越小
    // 公式：roughness = sqrt(2 / (shininess + 2))，为 Disney 重映射的简化形式
    float shininess = 32.0f;
    mat->Get(AI_MATKEY_SHININESS, shininess);
    if (shininess <= 0.0f)
      shininess = 1.0f;
    material.roughness = glm::clamp(std::sqrt(2.0f / (shininess + 2.0f)), 0.05f, 1.0f);

    // 用高光颜色强度粗略推断金属度：金属物体通常有较强的镜面反射色
    aiColor3D specular(0.0f, 0.0f, 0.0f);
    if (mat->Get(AI_MATKEY_COLOR_SPECULAR, specular) == AI_SUCCESS)
    {
      float specLum = (specular.r + specular.g + specular.b) / 3.0f;
      material.metallic = glm::clamp(specLum, 0.0f, 1.0f);
    }

    // 纹理：albedo / normal / specular / shininess / lightmapAO
    material.albedoMap = loadMaterialTexture(mat, aiTextureType_DIFFUSE);
    material.normalMap = loadMaterialTexture(mat, aiTextureType_NORMALS);
    if (material.normalMap == 0)
      material.normalMap = loadMaterialTexture(mat, aiTextureType_HEIGHT);

    material.metallicMap = loadMaterialTexture(mat, aiTextureType_SPECULAR);
    material.roughnessMap = loadMaterialTexture(mat, aiTextureType_SHININESS);
    material.aoMap = loadMaterialTexture(mat, aiTextureType_LIGHTMAP);

    // 若没有单独的 roughness 贴图，则复用 metallic 贴图（两者常打包在一张图里）
    if (material.roughnessMap == 0 && material.metallicMap != 0)
      material.roughnessMap = material.metallicMap;
  }

  return Mesh(vertices, indices, material);
}

unsigned int Model::loadMaterialTexture(aiMaterial *mat, int type)
{
  if (mat->GetTextureCount(static_cast<aiTextureType>(type)) == 0)
    return 0;

  aiString str;
  mat->GetTexture(static_cast<aiTextureType>(type), 0, &str);

  std::string filename = str.C_Str();

  // 去重：同一文件只加载一次（按完整路径比较）
  auto it = m_textureCache.find(filename);
  if (it != m_textureCache.end())
    return it->second;

  // 有些模型里纹理名已是绝对路径，这里做一次兼容判断
  std::string fullPath = (filename.size() > 1 && filename[1] == ':') || filename[0] == '/'
                             ? filename
                             : m_directory + "/" + filename;

  Texture2D tex;
  if (!tex.load(fullPath, true, false))
    return 0;

  unsigned int id = tex.id();
  m_textureCache[filename] = id;
  return id;
}

void Model::draw(const Shader &shader) const
{
  for (const Mesh &mesh : m_meshes)
    mesh.draw(shader);
}

unsigned int Model::triangleCount() const
{
  unsigned int total = 0;
  for (const Mesh &mesh : m_meshes)
    total += mesh.triangleCount();
  return total;
}

Model::Stats Model::stats() const
{
  Stats s;
  s.meshes = meshCount();
  s.triangles = triangleCount();
  s.textures = static_cast<unsigned int>(m_textureCache.size());
  return s;
}

} // namespace mr

