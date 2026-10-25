#ifndef PROJECT_CAMERA_H
#define PROJECT_CAMERA_H

// ============================================================
//  Mini Renderer - 自由相机
//  支持 WASD 前后左右移动、鼠标环视、滚轮缩放 FOV
//  与窗口系统解耦：由外部把输入事件喂进来
// ============================================================

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace mr
{

// 相机移动方向枚举
enum class CameraMovement
{
  Forward,
  Backward,
  Left,
  Right,
  Up,
  Down
};

class Camera
{
public:
  // 默认参数
  // 注意：名字加 CAM_ 前缀，避免与 windows.h 中的 DEFAULT_PITCH 等宏冲突
  static constexpr float CAM_DEFAULT_YAW = -90.0f;
  static constexpr float CAM_DEFAULT_PITCH = 0.0f;
  static constexpr float CAM_DEFAULT_SPEED = 4.0f;
  // 鼠标灵敏度：单位为"度/像素"。0.1 过大，轻微抖动就会让视角剧烈偏转，
  // 且俯仰角累积后相机会出现"绕圈"的观感，这里降到 0.05
  static constexpr float CAM_DEFAULT_SENSITIVITY = 0.05f;
  static constexpr float CAM_DEFAULT_ZOOM = 45.0f;

  Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 5.0f),
         glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
         float yaw = CAM_DEFAULT_YAW,
         float pitch = CAM_DEFAULT_PITCH);

  glm::mat4 viewMatrix() const;
  glm::mat4 projectionMatrix(float aspect) const;

  // 键盘输入：direction 为方向，dt 为本帧时间（秒）
  void processKeyboard(CameraMovement direction, float dt);

  // 鼠标输入：偏移量（像素）
  void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

  // 滚轮输入：yoffset 为滚动量
  void processMouseScroll(float yoffset);

  // ---------- 访问器 ----------
  const glm::vec3 &position() const { return m_position; }
  const glm::vec3 &front() const { return m_front; }
  float zoom() const { return m_zoom; }
  float nearPlane() const { return m_near; }
  float farPlane() const { return m_far; }
  float yaw() const { return m_yaw; }
  float pitch() const { return m_pitch; }

  void setPosition(const glm::vec3 &pos) { m_position = pos; }
  void setSpeed(float speed) { m_speed = speed; }
  void setZoom(float zoom) { m_zoom = zoom; }
  void setSensitivity(float s) { m_sensitivity = s; }
  float sensitivity() const { return m_sensitivity; }

private:
  // 相机属性
  glm::vec3 m_position;
  glm::vec3 m_front;
  glm::vec3 m_up;
  glm::vec3 m_right;
  glm::vec3 m_worldUp;

  // 欧拉角
  float m_yaw;
  float m_pitch;

  // 选项
  float m_speed;
  float m_sensitivity;
  float m_zoom;

  // 投影参数
  float m_near = 0.1f;
  float m_far = 200.0f;

  void updateCameraVectors();
};

} // namespace mr

#endif // PROJECT_CAMERA_H
