#ifndef VIEWCAR_CORE_H
#define VIEWCAR_CORE_H

// ============================================================
//  viewcar - 核心工具层
//  提供：日志、文件读取、时间/帧率统计
//  （与 project20261004 保持一致的接口风格，但本模块自包含）
// ============================================================

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <cmath>

namespace vc
{

// ------------------------------ 日志 ------------------------------
enum class LogLevel
{
  Info,
  Warn,
  Error
};

inline void log(LogLevel level, const std::string &msg)
{
  const char *tag = "[INFO ]";
  if (level == LogLevel::Warn)
    tag = "[WARN ]";
  else if (level == LogLevel::Error)
    tag = "[ERROR]";

  std::ostream &out = (level == LogLevel::Error) ? std::cerr : std::cout;
  out << tag << " " << msg << std::endl;
}

inline void logInfo(const std::string &msg) { log(LogLevel::Info, msg); }
inline void logWarn(const std::string &msg) { log(LogLevel::Warn, msg); }
inline void logError(const std::string &msg) { log(LogLevel::Error, msg); }

// ------------------------------ 文件读取 ------------------------------
inline std::string readTextFile(const std::string &path)
{
  std::ifstream file;
  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  try
  {
    file.open(path);
    std::stringstream ss;
    ss << file.rdbuf();
    file.close();
    return ss.str();
  }
  catch (std::ifstream::failure &e)
  {
    (void)e;
    std::cerr << "[ERROR] 无法读取文件: " << path << std::endl;
    return std::string();
  }
}

// ------------------------------ 数值工具 ------------------------------
inline float clampf(float v, float lo, float hi)
{
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// 车辆坐标系约定：
//   +X = 车辆右侧,  +Y = 上,  +Z = 车头前进方向（与相机 -Z 朝向相反）
// 角度单位统一用「度」，内部再转弧度。

// ------------------------------ 帧率统计 ------------------------------
class FrameStats
{
public:
  FrameStats()
      : m_lastTime(clock::now()), m_frameCount(0), m_fps(0.0f), m_frameMs(0.0f)
  {
  }

  // 每帧调用一次，返回本帧的 deltaTime（秒），并做上限保护
  float tick()
  {
    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - m_lastTime).count();
    m_lastTime = now;

    // 切换视角/断点调试后 dt 可能异常巨大，钳制避免物体瞬移穿模
    if (dt > 0.1f)
      dt = 0.1f;

    m_frameMs = dt * 1000.0f;
    m_frameCount++;
    m_accumTime += dt;

    if (m_accumTime >= 0.5f)
    {
      m_fps = static_cast<float>(m_frameCount) / m_accumTime;
      m_frameCount = 0;
      m_accumTime = 0.0f;
    }
    return dt;
  }

  float fps() const { return m_fps; }
  float frameMs() const { return m_frameMs; }

private:
  using clock = std::chrono::high_resolution_clock;
  clock::time_point m_lastTime;
  int m_frameCount;
  float m_fps;
  float m_frameMs;
  float m_accumTime = 0.0f;
};

} // namespace vc

#endif // VIEWCAR_CORE_H
