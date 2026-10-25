#ifndef VIEWCAR_CAMERA_H
#define VIEWCAR_CAMERA_H

// ============================================================
//  viewcar - 相机
//
//  车载可视化需要几种典型视角，本模块把它们统一到一个相机里：
//    Free    自由观察（WASD + 鼠标环视），用于调试场景
//    Chase   追车视角：跟在自车后方一定距离，平滑跟随
//    Bird    鸟瞰视角：自车正上方俯视，ADAS 标定与全局态势常用
//    Cockpit 第一人称：坐在驾驶员位置，看出去和实车一致
//
//  视角切换时用「阻尼插值」过渡，避免画面跳变。
// ============================================================

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core.h"

namespace vc
{

enum class CameraMode
{
  Free = 0,
  Chase,
  Bird,
  Cockpit
};

inline const char *nameOfCameraMode(CameraMode m)
{
  switch (m)
  {
  case CameraMode::Free:
    return "自由视角";
  case CameraMode::Chase:
    return "追车视角";
  case CameraMode::Bird:
    return "鸟瞰视角";
  case CameraMode::Cockpit:
    return "第一人称";
  }
  return "未知";
}

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
  static constexpr float CAM_DEFAULT_YAW = 180.0f; // 面向 +Z（车头方向）
  static constexpr float CAM_DEFAULT_PITCH = -12.0f;
  static constexpr float CAM_DEFAULT_SPEED = 12.0f;
  static constexpr float CAM_DEFAULT_SENSITIVITY = 0.15f;
  static constexpr float CAM_DEFAULT_ZOOM = 50.0f;

  Camera(glm::vec3 position = glm::vec3(0.0f, 3.0f, -10.0f),
         glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
         float yaw = CAM_DEFAULT_YAW,
         float pitch = CAM_DEFAULT_PITCH);

  glm::mat4 viewMatrix() const;
  glm::mat4 projectionMatrix(float aspect) const;

  // 键盘输入：自由视角移动
  void processKeyboard(CameraMovement direction, float dt);

  // 鼠标输入：偏移量（像素）
  void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

  // 滚轮输入
  void processMouseScroll(float yoffset);

  // 让相机跟随自车（由 main 每帧调用），内部按当前模式与 damping 做平滑
  //   egoPos     自车后轴中心
  //   egoForward 自车车头方向（单位向量）
  //   egoHeading 自车航向（度）
  void followEgo(const glm::vec3 &egoPos, const glm::vec3 &egoForward, float egoHeading,
                 float dt);

  // ---- 模式与参数 ----
  CameraMode mode() const { return m_mode; }
  void setMode(CameraMode m) { m_mode = m; }
  void cycleMode();

  float chaseDistance = 9.0f;   // 追车距离
  float chaseHeight = 3.4f;     // 追车高度
  float birdHeight = 45.0f;     // 鸟瞰高度
  float birdPitch = -89.0f;     // 鸟瞰俯角（略小于 -90 避免万向锁）
  float damping = 4.0f;         // 跟随阻尼（越大越跟手）
  float cockpitHeight = 1.25f;  // 驾驶位眼高
  float cockpitForward = 0.4f;  // 驾驶位相对后轴中心的前移量
  bool freeLook = true;         // 追车/驾驶位是否允许鼠标自由环视

  // ---- 访问器 ----
  const glm::vec3 &position() const { return m_position; }
  const glm::vec3 &front() const { return m_front; }
  glm::vec3 right() const { return m_right; }
  float zoom() const { return m_zoom; }
  float yaw() const { return m_yaw; }
  float pitch() const { return m_pitch; }
  float nearPlane() const { return m_near; }
  float farPlane() const { return m_far; }

  void setPosition(const glm::vec3 &pos) { m_position = pos; }
  void setYawPitch(float yaw, float pitch);
  void setSpeed(float speed) { m_speed = speed; }
  void setZoom(float zoom) { m_zoom = clampf(zoom, 10.0f, 100.0f); }
  void setSensitivity(float s) { m_sensitivity = s; }
  float sensitivity() const { return m_sensitivity; }
  float speed() const { return m_speed; }

  // 恢复到默认姿态与参数（相机被转晕时一键复位）
  void reset();

  // 直接给视角加偏移（供 ImGui 里的「视角微调」按钮复用）
  void addRotation(float dYaw, float dPitch) { processMouseMovement(dYaw, dPitch); }

private:
  glm::vec3 m_position;
  glm::vec3 m_front;
  glm::vec3 m_up;
  glm::vec3 m_right;
  glm::vec3 m_worldUp;

  float m_yaw;
  float m_pitch;

  float m_speed;
  float m_sensitivity;
  float m_zoom;

  float m_near = 0.1f;
  float m_far = 1000.0f; // 需要看到远处的道路与目标

  CameraMode m_mode = CameraMode::Chase;

  // 模式的基准角度（环视偏移叠加在基准之上）
  float m_baseYaw = CAM_DEFAULT_YAW;

  void updateCameraVectors();
};

} // namespace vc

#endif // VIEWCAR_CAMERA_H
