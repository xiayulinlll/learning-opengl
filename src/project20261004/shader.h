#ifndef PROJECT_SHADER_H
#define PROJECT_SHADER_H

// ============================================================
//  Mini Renderer - Shader 封装
//  负责 GLSL 程序的编译、链接与 uniform 上传
//  路径统一相对于"项目根目录"（即 make run 时所在的目录）
// ============================================================

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <unordered_map>

#include "core.h"

namespace mr
{

class Shader
{
public:
  Shader() : m_id(0) {}

  // 从文件加载并编译着色器
  // vertexPath / fragmentPath / geometryPath 相对于工程根目录
  bool loadFromFiles(const std::string &vertexPath,
                     const std::string &fragmentPath,
                     const std::string &geometryPath = "");

  // 直接从源码字符串编译（用于内联 shader，例如全屏后处理）
  bool loadFromSource(const std::string &vertexSrc,
                      const std::string &fragmentSrc,
                      const std::string &geometrySrc = "");

  void use() const { glUseProgram(m_id); }

  unsigned int id() const { return m_id; }

  void destroy();

  // ---------- uniform 上传 ----------
  void setBool(const std::string &name, bool value) const;
  void setInt(const std::string &name, int value) const;
  void setFloat(const std::string &name, float value) const;
  void setVec2(const std::string &name, const glm::vec2 &v) const;
  void setVec3(const std::string &name, const glm::vec3 &v) const;
  void setVec4(const std::string &name, const glm::vec4 &v) const;
  void setMat3(const std::string &name, const glm::mat3 &m) const;
  void setMat4(const std::string &name, const glm::mat4 &m) const;

private:
  unsigned int m_id;

  // 编译单个着色器阶段，失败返回 0
  unsigned int compileStage(const std::string &source, GLenum type, const std::string &debugName);

  // 链接 program，失败返回 false
  bool linkProgram(unsigned int vertex, unsigned int fragment, unsigned int geometry);

  // 缓存 uniform location，避免每帧重复查询
  mutable std::unordered_map<std::string, int> m_uniformCache;
  int getUniformLocation(const std::string &name) const;

  void checkLinkErrors(unsigned int program);
};

} // namespace mr

#endif // PROJECT_SHADER_H
