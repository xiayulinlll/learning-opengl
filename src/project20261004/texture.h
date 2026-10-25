#ifndef PROJECT_TEXTURE_H
#define PROJECT_TEXTURE_H

// ============================================================
//  Mini Renderer - 纹理
//  支持：2D 纹理加载、HDR 浮点纹理、立方体贴图（天空盒/IBL）
//  依赖 stb_image（仓库 include/tool/stb_image.h 提供实现）
// ============================================================

#include <glad/glad.h>

#include <string>
#include <vector>

namespace mr
{

class Texture2D
{
public:
  Texture2D() : m_id(0) {}

  // 加载普通 2D 纹理，flipVertically 控制是否翻转 Y 轴
  bool load(const std::string &path, bool flipVertically = true, bool gammaCorrect = false);

  // 加载 HDR 浮点纹理（用于 IBL 环境贴图）
  bool loadHdr(const std::string &path, bool flipVertically = true);

  void bind(unsigned int unit = 0) const;
  void destroy();

  unsigned int id() const { return m_id; }

private:
  unsigned int m_id;
};

// 立方体贴图（天空盒 / 环境贴图）
class Cubemap
{
public:
  Cubemap() : m_id(0) {}

  // faces 顺序：+X, -X, +Y, -Y, +Z, -Z
  bool load(const std::vector<std::string> &faces, bool flipVertically = false);

  void bind(unsigned int unit = 0) const;
  void destroy();

  unsigned int id() const { return m_id; }

private:
  unsigned int m_id;
};

} // namespace mr

#endif // PROJECT_TEXTURE_H
