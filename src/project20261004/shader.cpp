#include "shader.h"

#include <vector>

namespace mr
{

unsigned int Shader::compileStage(const std::string &source, GLenum type, const std::string &debugName)
{
  if (source.empty())
  {
    logError("着色器源码为空: " + debugName);
    return 0;
  }

  unsigned int shader = glCreateShader(type);
  const char *src = source.c_str();
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);

  // 检查编译结果
  GLint success = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success)
  {
    GLchar infoLog[1024];
    glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
    logError("着色器编译失败 (" + debugName + "):\n" + std::string(infoLog));
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

bool Shader::linkProgram(unsigned int vertex, unsigned int fragment, unsigned int geometry)
{
  m_id = glCreateProgram();
  glAttachShader(m_id, vertex);
  glAttachShader(m_id, fragment);
  if (geometry != 0)
    glAttachShader(m_id, geometry);

  glLinkProgram(m_id);

  GLint success = 0;
  glGetProgramiv(m_id, GL_LINK_STATUS, &success);
  if (!success)
  {
    checkLinkErrors(m_id);
    glDeleteProgram(m_id);
    m_id = 0;
    return false;
  }

  // 链接完成后着色器对象即可释放（program 已持有引用）
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  if (geometry != 0)
    glDeleteShader(geometry);

  return true;
}

bool Shader::loadFromFiles(const std::string &vertexPath,
                           const std::string &fragmentPath,
                           const std::string &geometryPath)
{
  std::string vsSrc = readTextFile(vertexPath);
  std::string fsSrc = readTextFile(fragmentPath);
  std::string gsSrc;
  if (!geometryPath.empty())
    gsSrc = readTextFile(geometryPath);

  return loadFromSource(vsSrc, fsSrc, gsSrc);
}

bool Shader::loadFromSource(const std::string &vertexSrc,
                            const std::string &fragmentSrc,
                            const std::string &geometrySrc)
{
  unsigned int vertex = compileStage(vertexSrc, GL_VERTEX_SHADER, "vertex");
  if (vertex == 0)
    return false;

  unsigned int fragment = compileStage(fragmentSrc, GL_FRAGMENT_SHADER, "fragment");
  if (fragment == 0)
  {
    glDeleteShader(vertex);
    return false;
  }

  unsigned int geometry = 0;
  if (!geometrySrc.empty())
  {
    geometry = compileStage(geometrySrc, GL_GEOMETRY_SHADER, "geometry");
    if (geometry == 0)
    {
      glDeleteShader(vertex);
      glDeleteShader(fragment);
      return false;
    }
  }

  bool ok = linkProgram(vertex, fragment, geometry);
  if (ok)
  {
    m_uniformCache.clear();
    logInfo("着色器编译链接成功 (program id = " + std::to_string(m_id) + ")");
  }
  return ok;
}

void Shader::destroy()
{
  if (m_id != 0)
  {
    glDeleteProgram(m_id);
    m_id = 0;
  }
  m_uniformCache.clear();
}

void Shader::checkLinkErrors(unsigned int program)
{
  GLchar infoLog[1024];
  glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
  logError("着色器程序链接失败:\n" + std::string(infoLog));
}

// 缓存 uniform location：同名 uniform 只查询一次
int Shader::getUniformLocation(const std::string &name) const
{
  auto it = m_uniformCache.find(name);
  if (it != m_uniformCache.end())
    return it->second;

  int location = glGetUniformLocation(m_id, name.c_str());
  m_uniformCache[name] = location;
  return location;
}

void Shader::setBool(const std::string &name, bool value) const
{
  glUniform1i(getUniformLocation(name), static_cast<int>(value));
}

void Shader::setInt(const std::string &name, int value) const
{
  glUniform1i(getUniformLocation(name), value);
}

void Shader::setFloat(const std::string &name, float value) const
{
  glUniform1f(getUniformLocation(name), value);
}

void Shader::setVec2(const std::string &name, const glm::vec2 &v) const
{
  glUniform2fv(getUniformLocation(name), 1, glm::value_ptr(v));
}

void Shader::setVec3(const std::string &name, const glm::vec3 &v) const
{
  glUniform3fv(getUniformLocation(name), 1, glm::value_ptr(v));
}

void Shader::setVec4(const std::string &name, const glm::vec4 &v) const
{
  glUniform4fv(getUniformLocation(name), 1, glm::value_ptr(v));
}

void Shader::setMat3(const std::string &name, const glm::mat3 &m) const
{
  glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(m));
}

void Shader::setMat4(const std::string &name, const glm::mat4 &m) const
{
  glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(m));
}

} // namespace mr
