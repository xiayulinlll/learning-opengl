#include "camera.h"

#include <algorithm>
#include <cmath>

namespace vc
{

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : m_position(position),
      m_front(glm::vec3(0.0f, 0.0f, -1.0f)),
      m_up(up),
      m_right(glm::vec3(1.0f, 0.0f, 0.0f)),
      m_worldUp(up),
      m_yaw(yaw),
      m_pitch(pitch),
      m_speed(CAM_DEFAULT_SPEED),
      m_sensitivity(CAM_DEFAULT_SENSITIVITY),
      m_zoom(CAM_DEFAULT_ZOOM),
      m_baseYaw(yaw)
{
  updateCameraVectors();
}

glm::mat4 Camera::viewMatrix() const
{
  return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::projectionMatrix(float aspect) const
{
  // 车载可视化需要比通用场景更远的可视距离，far 取 1000 米
  return glm::perspective(glm::radians(m_zoom), aspect, m_near, m_far);
}

void Camera::processKeyboard(CameraMovement direction, float dt)
{
  // 只有自由视角才响应键盘移动；跟随类视角的位置由 followEgo 接管
  if (m_mode != CameraMode::Free)
    return;

  const float velocity = m_speed * dt;

  if (direction == CameraMovement::Forward)
    m_position += m_front * velocity;
  if (direction == CameraMovement::Backward)
    m_position -= m_front * velocity;
  if (direction == CameraMovement::Left)
    m_position -= m_right * velocity;
  if (direction == CameraMovement::Right)
    m_position += m_right * velocity;
  if (direction == CameraMovement::Up)
    m_position += m_worldUp * velocity;
  if (direction == CameraMovement::Down)
    m_position -= m_worldUp * velocity;
}

void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch)
{
  // 自由视角始终响应；追车/驾驶位仅在允许环视时响应
  if (m_mode != CameraMode::Free && !freeLook)
    return;

  xoffset *= m_sensitivity;
  yoffset *= m_sensitivity;

  m_yaw += xoffset;
  m_pitch += yoffset;

  if (constrainPitch)
    m_pitch = clampf(m_pitch, -89.0f, 89.0f);

  // 偏航归一到 (-180, 180]，防止长时间旋转导致数值精度下降
  while (m_yaw > 180.0f)
    m_yaw -= 360.0f;
  while (m_yaw <= -180.0f)
    m_yaw += 360.0f;

  updateCameraVectors();
}

void Camera::processMouseScroll(float yoffset)
{
  m_zoom = clampf(m_zoom - yoffset, 10.0f, 100.0f);

  // 跟随视角下滚轮改为调整跟随距离/高度，比缩放 FOV 更符合直觉
  if (m_mode == CameraMode::Chase)
    chaseDistance = clampf(chaseDistance - yoffset * 1.5f, 4.0f, 40.0f);
  else if (m_mode == CameraMode::Bird)
    birdHeight = clampf(birdHeight - yoffset * 4.0f, 12.0f, 200.0f);
}

void Camera::followEgo(const glm::vec3 &egoPos, const glm::vec3 &egoForward,
                       float egoHeading, float dt)
{
  // 自车航向约定为「车头指向 +Z 时为 0 度」；
  // 相机 yaw 约定为「0 看向 -Z、180 看向 +Z」，因此看向车头时 yaw = 180 + heading
  m_baseYaw = 180.0f + egoHeading;

  switch (m_mode)
  {
  case CameraMode::Free:
    return; // 自由视角不跟随

  case CameraMode::Chase:
  {
    // 基准位置：自车后方 chaseDistance、上方 chaseHeight
    glm::vec3 offset = -egoForward * chaseDistance + glm::vec3(0.0f, chaseHeight, 0.0f);

    // 鼠标环视：让「跟随偏移」绕 Y 轴旋转，产生环绕观察的观感
    if (freeLook)
    {
      float orbit = m_yaw - m_baseYaw;
      if (orbit > 180.0f)
        orbit -= 360.0f;
      if (orbit < -180.0f)
        orbit += 360.0f;

      const float r = glm::radians(orbit);
      const float c = std::cos(r);
      const float s = std::sin(r);
      offset = glm::vec3(offset.x * c - offset.z * s, offset.y,
                         offset.x * s + offset.z * c);
    }

    // 阻尼插值：t 与 dt 相关，保证不同帧率下手感一致
    const float t = 1.0f - std::exp(-damping * dt);
    m_position = glm::mix(m_position, egoPos + offset, t);

    // 看向自车前方一点，让车头方向占据更多画面
    const glm::vec3 lookTarget = egoPos + egoForward * 6.0f + glm::vec3(0.0f, 1.0f, 0.0f);
    m_front = glm::normalize(lookTarget - m_position);

    // 由 front 反推 yaw/pitch，保持鼠标环视的连续性
    m_pitch = glm::degrees(std::asin(clampf(m_front.y, -1.0f, 1.0f)));
    return;
  }

  case CameraMode::Bird:
  {
    // 相机在自车上方，向前偏移一点以看清前方目标（ADAS 鸟瞰图常见构图）
    const glm::vec3 desired = egoPos + glm::vec3(0.0f, birdHeight, 8.0f);
    const float t = 1.0f - std::exp(-damping * 0.7f * dt);
    m_position = glm::mix(m_position, desired, t);

    // 俯视角度由 birdPitch 直接给定，yaw 与车头保持一致
    m_pitch = birdPitch;
    m_yaw = m_baseYaw;
    updateCameraVectors();
    return;
  }

  case CameraMode::Cockpit:
  {
    // 驾驶位：位置固定在车身内，可小幅环视
    const glm::vec3 desired = egoPos + egoForward * cockpitForward +
                              glm::vec3(0.0f, cockpitHeight, 0.0f);

    // 人坐在车内，位置几乎不应滞后，因此用很大的阻尼系数
    const float t = 1.0f - std::exp(-25.0f * dt);
    m_position = glm::mix(m_position, desired, t);

    // 环视偏移限制在 ±60 度，超出范围缓慢回正（模拟视线回到前方）
    float orbit = m_yaw - m_baseYaw;
    if (orbit > 180.0f)
      orbit -= 360.0f;
    if (orbit < -180.0f)
      orbit += 360.0f;

    if (std::fabs(orbit) > 60.0f)
    {
      const float excess = std::fabs(orbit) - 60.0f;
      const float sign = (orbit > 0.0f) ? 1.0f : -1.0f;
      m_yaw -= sign * excess * (1.0f - std::exp(-3.0f * dt));
    }

    updateCameraVectors();
    return;
  }
  }
}

void Camera::cycleMode()
{
  switch (m_mode)
  {
  case CameraMode::Chase:
    m_mode = CameraMode::Bird;
    break;
  case CameraMode::Bird:
    m_mode = CameraMode::Cockpit;
    break;
  case CameraMode::Cockpit:
    m_mode = CameraMode::Free;
    break;
  default:
    m_mode = CameraMode::Chase;
    break;
  }

  // 切换到鸟瞰时立刻把俯角拉到位，避免过渡期间出现地平线翻转
  if (m_mode == CameraMode::Bird)
  {
    m_pitch = birdPitch;
    m_yaw = m_baseYaw;
  }

  // 进入跟随类视角时把环视偏移清零，回到「正后方/正前方」的默认姿态
  if (m_mode == CameraMode::Chase)
  {
    m_yaw = m_baseYaw;
    m_pitch = CAM_DEFAULT_PITCH;
  }
  else if (m_mode == CameraMode::Cockpit)
  {
    m_yaw = m_baseYaw;
    m_pitch = 0.0f;
  }

  updateCameraVectors();
}

void Camera::setYawPitch(float yaw, float pitch)
{
  m_yaw = yaw;
  m_pitch = clampf(pitch, -89.0f, 89.0f);
  updateCameraVectors();
}

// 恢复默认姿态与参数：视角被鼠标转晕、或参数被调乱时一键复位
void Camera::reset()
{
  m_mode = CameraMode::Chase;

  // 参数恢复出厂值
  chaseDistance = 9.0f;
  chaseHeight = 3.4f;
  birdHeight = 45.0f;
  birdPitch = -89.0f;
  damping = 4.0f;
  cockpitHeight = 1.25f;
  cockpitForward = 0.4f;

  m_speed = CAM_DEFAULT_SPEED;
  m_sensitivity = CAM_DEFAULT_SENSITIVITY;
  m_zoom = CAM_DEFAULT_ZOOM;

  m_baseYaw = CAM_DEFAULT_YAW;
  m_yaw = CAM_DEFAULT_YAW;
  m_pitch = CAM_DEFAULT_PITCH;

  m_position = glm::vec3(0.0f, chaseHeight, -chaseDistance);

  updateCameraVectors();
}

void Camera::updateCameraVectors()
{
  glm::vec3 front;
  front.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
  front.y = std::sin(glm::radians(m_pitch));
  front.z = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
  m_front = glm::normalize(front);

  // 处理俯仰接近 ±90 度时 up 与 front 退化的情况
  m_right = glm::normalize(glm::cross(m_front, m_worldUp));
  if (glm::length(m_right) < 1e-4f)
    m_right = glm::vec3(1.0f, 0.0f, 0.0f);

  m_up = glm::normalize(glm::cross(m_right, m_front));
}

} // namespace vc
