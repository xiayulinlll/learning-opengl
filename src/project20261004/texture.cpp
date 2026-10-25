#include "texture.h"

#include "core.h"

#include <tool/stb_image.h>

namespace mr
{

// ------------------------------ Texture2D ------------------------------

bool Texture2D::load(const std::string &path, bool flipVertically, bool gammaCorrect)
{
  stbi_set_flip_vertically_on_load(flipVertically);

  int width, height, nrComponents;
  unsigned char *data = stbi_load(path.c_str(), &width, &height, &nrComponents, 0);
  if (!data)
  {
    logError("纹理加载失败: " + path);
    return false;
  }

  GLenum internalFormat = GL_RGB;
  GLenum dataFormat = GL_RGB;
  if (nrComponents == 1)
  {
    internalFormat = GL_RED;
    dataFormat = GL_RED;
  }
  else if (nrComponents == 3)
  {
    internalFormat = gammaCorrect ? GL_SRGB : GL_RGB;
    dataFormat = GL_RGB;
  }
  else if (nrComponents == 4)
  {
    internalFormat = gammaCorrect ? GL_SRGB_ALPHA : GL_RGBA;
    dataFormat = GL_RGBA;
  }

  glGenTextures(1, &m_id);
  glBindTexture(GL_TEXTURE_2D, m_id);
  glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);

  // 环绕与过滤
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  stbi_image_free(data);
  logInfo("加载纹理: " + path + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
  return true;
}

bool Texture2D::loadHdr(const std::string &path, bool flipVertically)
{
  stbi_set_flip_vertically_on_load(flipVertically);

  int width, height, nrComponents;
  float *data = stbi_loadf(path.c_str(), &width, &height, &nrComponents, 0);
  if (!data)
  {
    logError("HDR 纹理加载失败: " + path);
    return false;
  }

  glGenTextures(1, &m_id);
  glBindTexture(GL_TEXTURE_2D, m_id);
  // RGB16F 保存 HDR 浮点数据（范围可超过 1.0）
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  stbi_image_free(data);
  logInfo("加载 HDR 纹理: " + path + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
  return true;
}

void Texture2D::bind(unsigned int unit) const
{
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, m_id);
}

void Texture2D::destroy()
{
  if (m_id != 0)
  {
    glDeleteTextures(1, &m_id);
    m_id = 0;
  }
}

// ------------------------------ Cubemap ------------------------------

bool Cubemap::load(const std::vector<std::string> &faces, bool flipVertically)
{
  if (faces.size() != 6)
  {
    logError("Cubemap 需要 6 张面纹理");
    return false;
  }

  stbi_set_flip_vertically_on_load(flipVertically);

  glGenTextures(1, &m_id);
  glBindTexture(GL_TEXTURE_CUBE_MAP, m_id);

  int width, height, nrComponents;
  for (unsigned int i = 0; i < 6; i++)
  {
    unsigned char *data = stbi_load(faces[i].c_str(), &width, &height, &nrComponents, 0);
    if (data)
    {
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB,
                   width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
      stbi_image_free(data);
    }
    else
    {
      logError("Cubemap 面加载失败: " + faces[i]);
      stbi_image_free(data);
      return false;
    }
  }

  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  logInfo("加载 Cubemap 完成 (" + std::to_string(6) + " 面)");
  return true;
}

// 说明：把 HDR 等距柱状图转换成辐照度/预过滤贴图需要额外的离屏渲染管线，
// 本项目为保持代码可读性，天空盒直接使用 6 张 LDR 贴图（static/texture/skybox）。
// 若需要 IBL，可在此基础上扩展 cubemap 卷积渲染（详见 README 中的扩展建议）。

void Cubemap::bind(unsigned int unit) const
{
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_CUBE_MAP, m_id);
}

void Cubemap::destroy()
{
  if (m_id != 0)
  {
    glDeleteTextures(1, &m_id);
    m_id = 0;
  }
}

} // namespace mr
