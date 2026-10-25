#include "shader.h"

#include <vector>

namespace vc
{

unsigned int Shader::compileStage(const std::string &source, GLenum type,
                                  const std::string &debugName)
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

  int success = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success)
  {
    int logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> infoLog(static_cast<size_t>(logLength > 1 ? logLength : 1));
    glGetShaderInfoLog(shader, logLength, nullptr, infoLog.data());

    logError("着色器编译失败 (" + debugName + "):\n" + std::string(infoLog.data()));
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

bool Shader::loadFromFiles(const std::string &vertexPath,
                           const std::string &fragmentPath,
                           const std::string &geometryPath)
{
  const std::string vertexSrc = readTextFile(vertexPath);
  const std::string fragmentSrc = readTextFile(fragmentPath);
  std::string geometrySrc;
  if (!geometryPath.empty())
    geometrySrc = readTextFile(geometryPath);

  return loadFromSource(vertexSrc, fragmentSrc, geometrySrc);
}

bool Shader::loadFromSource(const std::string &vertexSrc,
                            const std::string &fragmentSrc,
                            const std::string &geometrySrc)
{
  unsigned int vertex = compileStage(vertexSrc, GL_VERTEX_SHADER, "vertex");
  if (!vertex)
    return false;

  unsigned int fragment = compileStage(fragmentSrc, GL_FRAGMENT_SHADER, "fragment");
  if (!fragment)
  {
    glDeleteShader(vertex);
    return false;
  }

  unsigned int geometry = 0;
  if (!geometrySrc.empty())
  {
    geometry = compileStage(geometrySrc, GL_GEOMETRY_SHADER, "geometry");
    if (!geometry)
    {
      glDeleteShader(vertex);
      glDeleteShader(fragment);
      return false;
    }
  }

  const bool ok = linkProgram(vertex, fragment, geometry);

  // 链接完成后着色器对象即可删除（program 已持有引用）
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  if (geometry)
    glDeleteShader(geometry);

  return ok;
}

bool Shader::linkProgram(unsigned int vertex, unsigned int fragment, unsigned int geometry)
{
  if (m_id)
    destroy();

  m_id = glCreateProgram();
  glAttachShader(m_id, vertex);
  glAttachShader(m_id, fragment);
  if (geometry)
    glAttachShader(m_id, geometry);
  glLinkProgram(m_id);

  int success = 0;
  glGetProgramiv(m_id, GL_LINK_STATUS, &success);
  if (!success)
  {
    checkLinkErrors(m_id);
    glDeleteProgram(m_id);
    m_id = 0;
    return false;
  }

  m_uniformCache.clear();
  return true;
}

void Shader::checkLinkErrors(unsigned int program)
{
  int logLength = 0;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
  std::vector<char> infoLog(static_cast<size_t>(logLength > 1 ? logLength : 1));
  glGetProgramInfoLog(program, logLength, nullptr, infoLog.data());
  logError("着色器链接失败:\n" + std::string(infoLog.data()));
}

void Shader::destroy()
{
  if (m_id)
    glDeleteProgram(m_id);
  m_id = 0;
  m_uniformCache.clear();
}

int Shader::getUniformLocation(const std::string &name) const
{
  auto it = m_uniformCache.find(name);
  if (it != m_uniformCache.end())
    return it->second;

  const int loc = glGetUniformLocation(m_id, name.c_str());
  m_uniformCache[name] = loc;
  return loc;
}

// ------------------------------ uniform 上传 ------------------------------

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

} // namespace vc
