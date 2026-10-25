#include "camera.h"

#include <cmath>

namespace mr
{

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : m_position(position),
      m_front(glm::vec3(0.0f, 0.0f, -1.0f)),
      m_worldUp(up),
      m_yaw(yaw),
      m_pitch(pitch),
      m_speed(CAM_DEFAULT_SPEED),
      m_sensitivity(CAM_DEFAULT_SENSITIVITY),
      m_zoom(CAM_DEFAULT_ZOOM)
{
  updateCameraVectors();
}

glm::mat4 Camera::viewMatrix() const
{
  return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::projectionMatrix(float aspect) const
{
  return glm::perspective(glm::radians(m_zoom), aspect, m_near, m_far);
}

void Camera::processKeyboard(CameraMovement direction, float dt)
{
  float velocity = m_speed * dt;
  switch (direction)
  {
  case CameraMovement::Forward:
    m_position += m_front * velocity;
    break;
  case CameraMovement::Backward:
    m_position -= m_front * velocity;
    break;
  case CameraMovement::Left:
    m_position -= m_right * velocity;
    break;
  case CameraMovement::Right:
    m_position += m_right * velocity;
    break;
  case CameraMovement::Up:
    m_position += m_worldUp * velocity;
    break;
  case CameraMovement::Down:
    m_position -= m_worldUp * velocity;
    break;
  }
}

void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch)
{
  xoffset *= m_sensitivity;
  yoffset *= m_sensitivity;

  m_yaw += xoffset;
  m_pitch += yoffset;

  // 限制俯仰角，防止画面翻转
  if (constrainPitch)
  {
    if (m_pitch > 89.0f)
      m_pitch = 89.0f;
    if (m_pitch < -89.0f)
      m_pitch = -89.0f;
  }

  updateCameraVectors();
}

void Camera::processMouseScroll(float yoffset)
{
  m_zoom -= yoffset;
  if (m_zoom < 1.0f)
    m_zoom = 1.0f;
  if (m_zoom > 45.0f)
    m_zoom = 45.0f;
}

void Camera::updateCameraVectors()
{
  // 由欧拉角计算朝向向量
  glm::vec3 front;
  front.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
  front.y = std::sin(glm::radians(m_pitch));
  front.z = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
  m_front = glm::normalize(front);

  // 重新计算右向量与上向量
  m_right = glm::normalize(glm::cross(m_front, m_worldUp));
  m_up = glm::normalize(glm::cross(m_right, m_front));
}

} // namespace mr
