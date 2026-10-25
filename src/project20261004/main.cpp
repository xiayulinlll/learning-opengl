// ============================================================
//  Mini Renderer —— 基于 OpenGL 3.3 的实时 3D 渲染器
//
//  功能：
//    · Cook-Torrance PBR 光照（GGX + Smith + Fresnel）
//    · 方向光阴影贴图（PCF 软阴影）
//    · 点光源 / 聚光灯
//    · 法线贴图（切线空间）
//    · 天空盒
//    · assimp 模型加载 + 资源缓存
//    · ImGui 实时调参面板 + 性能统计
//
//  操作：
//    W/A/S/D 移动   鼠标 环视   滚轮 缩放   ESC 退出   F1 切换鼠标锁定
// ============================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <string>
#include <vector>
#include <cmath>

// Windows 下用 GetModuleFileNameA 定位可执行文件所在目录，
// 从而把工作目录切回工程根目录（output/ 的上一级）
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <tool/stb_image.h>

#include "core.h"
#include "camera.h"
#include "light.h"
#include "mesh.h"
#include "renderer.h"
#include "scene.h"
#include "shader.h"
#include "texture.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

using namespace mr;

// ------------------------------ 工作目录处理 ------------------------------
// 可执行文件位于 <项目根>/output/ 下，而 shader / static 资源路径都相对于项目根，
// 因此在启动时把工作目录切回项目根，保证 "shader/xxx.glsl"、"static/xxx" 能被找到。
static bool resolveProjectRoot()
{
#ifdef _WIN32
  char buffer[MAX_PATH] = {0};
  DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
  if (len == 0 || len >= MAX_PATH)
  {
    logError("无法获取可执行文件路径");
    return false;
  }

  std::string exePath(buffer);
  size_t pos = exePath.find_last_of("\\/");
  if (pos == std::string::npos)
  {
    logError("无法解析可执行文件目录");
    return false;
  }

  // exePath 的目录即 output/，其上一级才是项目根
  std::string exeDir = exePath.substr(0, pos);
  size_t parentPos = exeDir.find_last_of("\\/");
  std::string projectRoot = (parentPos == std::string::npos) ? exeDir : exeDir.substr(0, parentPos);

  if (_chdir(projectRoot.c_str()) != 0)
  {
    logError("切换工作目录失败: " + projectRoot);
    return false;
  }
  logInfo("工作目录已切换到项目根: " + projectRoot);
  return true;
#else
  // 非 Windows 平台：可执行文件在 <项目根>/output 下，同样回退一级
  char buffer[4096] = {0};
  ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (len <= 0)
  {
    logWarn("无法定位可执行文件路径，沿用当前工作目录");
    return true;
  }

  std::string exePath(buffer, len);
  size_t pos = exePath.find_last_of('/');
  std::string exeDir = (pos == std::string::npos) ? "." : exePath.substr(0, pos);
  size_t parentPos = exeDir.find_last_of('/');
  std::string projectRoot = (parentPos == std::string::npos) ? exeDir : exeDir.substr(0, parentPos);

  if (chdir(projectRoot.c_str()) != 0)
  {
    logError("切换工作目录失败: " + projectRoot);
    return false;
  }
  logInfo("工作目录已切换到项目根: " + projectRoot);
  return true;
#endif
}

// ------------------------------ 全局状态 ------------------------------
static int g_width = 1280;
static int g_height = 720;

// 单帧鼠标位移上限（像素）：防止异常跳变，同时保留足够的快速甩动空间
static constexpr float MOUSE_MAX_DELTA = 400.0f;
// 鼠标平滑系数（0~1）：越小越平滑但越"拖"，1.0 表示不平滑
static constexpr float MOUSE_SMOOTHING = 0.55f;

// 窗口 / 全屏状态
static int g_windowedX = 100;
static int g_windowedY = 100;
static int g_windowedW = 1280;
static int g_windowedH = 720;
static bool g_fullscreen = false;

static Camera g_camera(glm::vec3(0.0f, 2.0f, 6.0f));
static FrameStats g_frameStats;

static bool g_mouseCaptured = true;
static float g_lastX = g_width / 2.0f;
static float g_lastY = g_height / 2.0f;
static bool g_firstMouse = true;

// 鼠标平滑用的累积位移
static float g_pendingX = 0.0f;
static float g_pendingY = 0.0f;

// 功能列表 / 快捷键帮助窗口是否显示（由 F2 或面板按钮开关）
static bool g_showHelpWindow = true;

// ------------------------------ 回调 ------------------------------
static void framebufferSizeCallback(GLFWwindow *window, int width, int height)
{
  g_width = width;
  g_height = height;
  glViewport(0, 0, width, height);
}

static void mouseCallback(GLFWwindow *window, double xpos, double ypos)
{
  if (!g_mouseCaptured)
    return;

  if (g_firstMouse)
  {
    g_lastX = static_cast<float>(xpos);
    g_lastY = static_cast<float>(ypos);
    g_firstMouse = false;
    return; // 本次仅记录基准点，不产生旋转，避免首次跳变导致视角猛转
  }

  float xoffset = static_cast<float>(xpos) - g_lastX;
  float yoffset = g_lastY - static_cast<float>(ypos); // 屏幕 Y 轴向下，需取反
  g_lastX = static_cast<float>(xpos);
  g_lastY = static_cast<float>(ypos);

  // 注意：这里必须"钳制"而不是"丢弃"。
  // 早期版本把超过 100px 的位移直接置 0，导致快速甩鼠标时位移被整体吃掉，
  // 表现为"鼠标有时候不动"。改为钳制后仍保留移动量，只是限制单帧最大位移。
  if (xoffset > MOUSE_MAX_DELTA)
    xoffset = MOUSE_MAX_DELTA;
  else if (xoffset < -MOUSE_MAX_DELTA)
    xoffset = -MOUSE_MAX_DELTA;
  if (yoffset > MOUSE_MAX_DELTA)
    yoffset = MOUSE_MAX_DELTA;
  else if (yoffset < -MOUSE_MAX_DELTA)
    yoffset = -MOUSE_MAX_DELTA;

  // 累积到待处理位移，由主循环按帧平滑消耗，
  // 这样即使鼠标事件频率与渲染帧率不一致，视角也不会"跳"或"丢步"
  g_pendingX += xoffset;
  g_pendingY += yoffset;
}

// 每帧调用：平滑消耗累积的鼠标位移
static void applyMouseDelta()
{
  if (!g_mouseCaptured)
  {
    g_pendingX = 0.0f;
    g_pendingY = 0.0f;
    return;
  }

  if (g_pendingX == 0.0f && g_pendingY == 0.0f)
    return;

  // 把尚未消耗的位移按 MOUSE_SMOOTHING 比例逐帧应用
  g_camera.processMouseMovement(g_pendingX * MOUSE_SMOOTHING,
                                g_pendingY * MOUSE_SMOOTHING);

  float usedX = g_pendingX * MOUSE_SMOOTHING;
  float usedY = g_pendingY * MOUSE_SMOOTHING;
  g_pendingX -= usedX;
  g_pendingY -= usedY;

  // 残留极小位移直接清零，避免无限逼近
  if (std::fabs(g_pendingX) < 0.01f)
    g_pendingX = 0.0f;
  if (std::fabs(g_pendingY) < 0.01f)
    g_pendingY = 0.0f;
}

// 切换全屏 / 窗口模式（Alt+Enter 或 F11 触发）
static void toggleFullscreen(GLFWwindow *window)
{
  if (!g_fullscreen)
  {
    // 记录当前窗口位置与尺寸，便于还原
    glfwGetWindowPos(window, &g_windowedX, &g_windowedY);
    glfwGetWindowSize(window, &g_windowedW, &g_windowedH);

    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    g_fullscreen = true;
    logInfo("已切换到全屏 (" + std::to_string(mode->width) + "x" + std::to_string(mode->height) + ")");
  }
  else
  {
    glfwSetWindowMonitor(window, nullptr, g_windowedX, g_windowedY,
                         g_windowedW, g_windowedH, 0);
    g_fullscreen = false;
    logInfo("已切换回窗口模式");
  }
}

static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
  g_camera.processMouseScroll(static_cast<float>(yoffset));
}

// 处理键盘输入（每帧调用）
static void processInput(GLFWwindow *window, float dt)
{
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);

  if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
    g_camera.setSpeed(8.0f);
  else
    g_camera.setSpeed(4.0f);

  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Forward, dt);
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Backward, dt);
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Left, dt);
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Right, dt);
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Down, dt);
  if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Up, dt);
}

// F1 切换鼠标锁定状态（方便在 ImGui 面板上操作）
static void handleMouseToggle(GLFWwindow *window)
{
  static bool f1Pressed = false;
  bool down = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;

  if (down && !f1Pressed)
  {
    g_mouseCaptured = !g_mouseCaptured;
    glfwSetInputMode(window, GLFW_CURSOR,
                     g_mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    g_firstMouse = true;
    g_pendingX = 0.0f;
    g_pendingY = 0.0f;
    logInfo(g_mouseCaptured ? "鼠标已锁定（环视模式）" : "鼠标已解锁（可操作面板）");
  }
  f1Pressed = down;
}

// 全屏切换：F11 或 Alt+Enter；F2 显示/隐藏功能列表窗口
static void handleFullscreenToggle(GLFWwindow *window)
{
  static bool f11Pressed = false;
  static bool altEnterPressed = false;
  static bool f2Pressed = false;

  bool f11 = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;
  if (f11 && !f11Pressed)
    toggleFullscreen(window);
  f11Pressed = f11;

  bool altDown = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
  bool enterDown = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS ||
                   glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS;
  bool altEnter = altDown && enterDown;
  if (altEnter && !altEnterPressed)
    toggleFullscreen(window);
  altEnterPressed = altEnter;

  // F2 开关功能列表窗口
  bool f2 = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
  if (f2 && !f2Pressed)
    g_showHelpWindow = !g_showHelpWindow;
  f2Pressed = f2;
}


// ------------------------------ 功能列表 / 快捷键帮助窗口 ------------------------------
// 所有按键操作集中在这里，便于查阅；由 F2 或面板按钮开关
static void drawHelpWindow()
{
  if (!g_showHelpWindow)
    return;

  ImGui::SetNextWindowPos(ImVec2(370, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(430, 560), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("功能列表 / 操作说明", &g_showHelpWindow))
  {
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.7f, 1.0f), "Mini Renderer 操作一览");
    ImGui::Separator();

    // ---- 相机移动 ----
    if (ImGui::CollapsingHeader("相机移动", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Text("W / S          前进 / 后退");
      ImGui::Text("A / D          左移 / 右移");
      ImGui::Text("Q / E          下降 / 上升");
      ImGui::Text("Shift (按住)   加速移动 (4.0 -> 8.0)");
      ImGui::Text("鼠标移动       环视视角（需鼠标锁定）");
      ImGui::Text("鼠标滚轮       缩放 FOV (1~45)");
    }

    // ---- 鼠标与窗口 ----
    if (ImGui::CollapsingHeader("鼠标与窗口", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Text("F1             锁定 / 解锁鼠标");
      ImGui::Text("F11            切换全屏");
      ImGui::Text("Alt + Enter    切换全屏");
      ImGui::Text("F2             显示 / 隐藏本窗口");
      ImGui::Text("ESC            退出程序");
      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                         "提示: 鼠标锁定时无法点击面板，先按 F1");
    }

    // ---- 面板功能 ----
    if (ImGui::CollapsingHeader("ImGui 面板功能"))
    {
      ImGui::BulletText("性能统计: FPS / 帧耗时 / Draw Call / 三角面 / 对象数");
      ImGui::BulletText("相机: 位置 / FOV / 鼠标灵敏度");
      ImGui::BulletText("光照: 方向光 / 点光源 / 聚光灯 的颜色与强度");
      ImGui::BulletText("光照诊断: 实时显示各光源 RGB 与辐射量");
      ImGui::BulletText("场景: 每个物体的可见性 / 位置 / 旋转 / 缩放");
      ImGui::BulletText("渲染选项: 天空盒开关");
      ImGui::BulletText("视图/窗口: 全屏开关 / 重置相机");
    }

    // ---- 渲染特性 ----
    if (ImGui::CollapsingHeader("已实现的渲染特性"))
    {
      ImGui::BulletText("Cook-Torrance PBR (GGX + Smith + Schlick)");
      ImGui::BulletText("金属度-粗糙度工作流");
      ImGui::BulletText("方向光阴影贴图 + 3x3 PCF 软阴影");
      ImGui::BulletText("点光源 x4 / 聚光灯 (带平滑圆锥过渡)");
      ImGui::BulletText("法线贴图 (TBN 切线空间 -> 世界空间)");
      ImGui::BulletText("Cubemap 天空盒");
      ImGui::BulletText("Reinhard 色调映射 + Gamma 校正");
      ImGui::BulletText("4x MSAA 抗锯齿");
    }

    ImGui::Separator();
    ImGui::TextWrapped("鼠标灵敏度建议 0.03 ~ 0.05；若画面过曝看不出颜色差异，"
                       "请降低对应光源强度。");
  }
  ImGui::End();
}

// ------------------------------ ImGui 面板 ------------------------------
static void drawInfoPanel(Renderer &renderer, Scene &scene)
{
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);

  ImGui::Begin("Mini Renderer");

  // ---- 性能统计 ----
  if (ImGui::CollapsingHeader("性能统计", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::Text("FPS: %.1f", g_frameStats.fps());
    ImGui::Text("帧耗时: %.2f ms", g_frameStats.frameMs());

    const Renderer::RenderStats &s = renderer.stats();
    ImGui::Text("Draw Call: %u", s.drawCalls);
    ImGui::Text("三角面: %u", s.triangles);
    ImGui::Text("场景对象: %u", scene.objectCount());
  }

  // ---- 相机 ----
  if (ImGui::CollapsingHeader("相机"))
  {
    glm::vec3 pos = g_camera.position();
    if (ImGui::DragFloat3("位置", &pos.x, 0.1f))
      g_camera.setPosition(pos);

    float fov = g_camera.zoom();
    if (ImGui::SliderFloat("FOV", &fov, 10.0f, 90.0f))
      g_camera.setZoom(fov);

    float sens = g_camera.sensitivity();
    if (ImGui::SliderFloat("鼠标灵敏度", &sens, 0.01f, 0.20f, "%.3f"))
      g_camera.setSensitivity(sens);

    ImGui::Text("移动速度: 4.0 单位/秒 (按住 Shift 加速到 8.0)");
    ImGui::Text("鼠标操作: 按 F1 锁定/解锁鼠标");
  }

  // ---- 光照 ----
  if (ImGui::CollapsingHeader("光照", ImGuiTreeNodeFlags_DefaultOpen))
  {
    DirectionalLight &d = renderer.dirLight();
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "方向光");
    // 注意：每个控件的标签都带 ## 唯一后缀，避免 ImGui 按标签字符串哈希时发生 ID 冲突
    ImGui::DragFloat3("方向##dir", &d.direction.x, 0.02f, -1.0f, 1.0f);
    ImGui::ColorEdit3("颜色##dir", &d.color.x);
    ImGui::SliderFloat("强度##dir", &d.intensity, 0.0f, 20.0f);
    ImGui::Checkbox("投射阴影##dir", &d.castShadow);
    ImGui::SliderFloat("阴影偏移##dir", &d.shadowBias, 0.0f, 0.05f, "%.4f");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "点光源");
    PointLight &p = renderer.pointLight(0);
    ImGui::Checkbox("启用##point", &p.enabled);
    ImGui::DragFloat3("位置##point", &p.position.x, 0.1f);
    ImGui::ColorEdit3("颜色##point", &p.color.x);
    ImGui::SliderFloat("强度##point", &p.intensity, 0.0f, 100.0f);
    // 衰减系数：常数项越大，光照范围越小；调小可让点光源影响更广
    ImGui::SliderFloat("常数衰减##point", &p.constant, 0.01f, 2.0f, "%.3f");
    ImGui::SliderFloat("线性衰减##point", &p.linear, 0.0f, 0.5f, "%.3f");
    ImGui::SliderFloat("二次衰减##point", &p.quadratic, 0.0f, 0.2f, "%.4f");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "聚光灯");
    SpotLight &sp = renderer.spotLight();
    ImGui::Checkbox("启用##spot", &sp.enabled);
    ImGui::DragFloat3("位置##spot", &sp.position.x, 0.1f);
    ImGui::DragFloat3("方向##spot", &sp.direction.x, 0.02f, -1.0f, 1.0f);
    ImGui::ColorEdit3("颜色##spot", &sp.color.x); // 此前缺失，导致"改不了聚光灯颜色"
    ImGui::SliderFloat("强度##spot", &sp.intensity, 0.0f, 200.0f);
    ImGui::SliderFloat("内角##spot", &sp.cutOff, 1.0f, 45.0f);
    ImGui::SliderFloat("外角##spot", &sp.outerCutOff, 1.0f, 45.0f);
    ImGui::SliderFloat("常数衰减##spot", &sp.constant, 0.01f, 2.0f, "%.3f");
    ImGui::SliderFloat("线性衰减##spot", &sp.linear, 0.0f, 0.5f, "%.3f");
    ImGui::SliderFloat("二次衰减##spot", &sp.quadratic, 0.0f, 0.2f, "%.4f");

    // ---- 光照诊断：直观确认颜色 / 强度是否已生效 ----
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "光照诊断（辐射量 = 颜色 x 强度）");
    ImGui::Text("方向光 RGB = (%.2f, %.2f, %.2f)", d.color.r, d.color.g, d.color.b);
    ImGui::Text("方向光辐射 = (%.2f, %.2f, %.2f)", d.color.r * d.intensity,
                d.color.g * d.intensity, d.color.b * d.intensity);
    if (d.intensity > 2.0f)
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                         "提示: 强度过高会被色调映射压成白色，颜色看不出差异");
    ImGui::Text("点光源 RGB = (%.2f, %.2f, %.2f)  d0 = %.2f",
                p.color.r, p.color.g, p.color.b,
                glm::length(p.position - g_camera.position()));
    ImGui::Text("聚光灯 RGB = (%.2f, %.2f, %.2f)  d0 = %.2f",
                sp.color.r, sp.color.g, sp.color.b,
                glm::length(sp.position - g_camera.position()));
    if (ImGui::Button("重置光照为默认值##resetLights"))
    {
      d.color = glm::vec3(1.0f, 0.98f, 0.92f);
      d.intensity = 1.5f;
      p.color = glm::vec3(0.2f, 0.6f, 1.0f);
      p.intensity = 25.0f;
      sp.color = glm::vec3(1.0f);
      sp.intensity = 30.0f;
    }
  }

  // ---- 场景 ----
  if (ImGui::CollapsingHeader("场景", ImGuiTreeNodeFlags_DefaultOpen))
  {
    for (SceneObject &obj : scene.objects())
    {
      if (ImGui::TreeNode(obj.name.c_str()))
      {
        ImGui::Checkbox("可见", &obj.visible);
        ImGui::DragFloat3("位置", &obj.position.x, 0.1f);
        ImGui::DragFloat3("旋转", &obj.rotation.x, 1.0f);
        ImGui::DragFloat3("缩放", &obj.scale.x, 0.05f, 0.01f, 10.0f);
        ImGui::TreePop();
      }
    }
  }

  // ---- 渲染选项 ----
  if (ImGui::CollapsingHeader("渲染选项"))
  {
    bool skybox = renderer.skyboxEnabled();
    if (ImGui::Checkbox("天空盒", &skybox))
      renderer.setSkyboxEnabled(skybox);

    ImGui::TextWrapped("按 F1 可锁定/解锁鼠标");
  }

  // ---- 视图 / 窗口 ----
  if (ImGui::CollapsingHeader("视图 / 窗口", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::Text("窗口尺寸: %d x %d", g_width, g_height);

    if (ImGui::Button(g_fullscreen ? "退出全屏 (F11)##fs" : "进入全屏 (F11)##fs"))
      toggleFullscreen(glfwGetCurrentContext());

    ImGui::Checkbox("显示功能列表 (F2)", &g_showHelpWindow);

    if (ImGui::Button("重置相机视角##resetCam"))
    {
      g_camera.setPosition(glm::vec3(0.0f, 2.0f, 6.0f));
      g_camera.setZoom(45.0f);
      logInfo("相机视角已重置");
    }
  }

  ImGui::End();
}


// ------------------------------ 程序入口 ------------------------------
int main(int argc, char *argv[])
{
  // Makefile 的 run 目标会传入示例目录："src/project20261004/"
  // 这里保留该参数以便扩展（例如定位示例私有资源），当前统一使用相对工程根目录的路径
  (void)argc;
  (void)argv;

  // 先把工作目录切回工程根目录，确保 shader/ 与 static/ 的相对路径可用
  if (!resolveProjectRoot())
  {
    logError("无法定位项目根目录，程序退出");
    return -1;
  }

  // ------------------------ GLFW / OpenGL 初始化 ------------------------
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 4); // 4x MSAA 抗锯齿

  GLFWwindow *window = glfwCreateWindow(g_width, g_height, "Mini Renderer - PBR", nullptr, nullptr);
  if (!window)
  {
    logError("创建 GLFW 窗口失败");
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // 开启垂直同步，避免显卡空转

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
  {
    logError("初始化 GLAD 失败");
    return -1;
  }

  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
  glfwSetCursorPosCallback(window, mouseCallback);
  glfwSetScrollCallback(window, scrollCallback);
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  glViewport(0, 0, g_width, g_height);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glEnable(GL_MULTISAMPLE);

  // ------------------------ ImGui 初始化 ------------------------
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  // 加载中文字体：ImGui 默认字体仅含 ASCII，界面上的中文会显示为“?”
  // 依次尝试若干常见中文字体，取第一个可用的
  {
    ImGuiIO &io = ImGui::GetIO();
    const char *fontCandidates[] = {
        "C:/Windows/Fonts/Deng.ttf",   // 等线（TTF，兼容性最好）
        "C:/Windows/Fonts/simhei.ttf", // 黑体
        "C:/Windows/Fonts/msyh.ttc",   // 微软雅黑
        "C:/Windows/Fonts/simsun.ttc", // 宋体
    };
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;
    bool fontLoaded = false;
    for (const char *path : fontCandidates)
    {
      // 使用"中文全字表"而非"简体常用字表"：
      // GetGlyphRangesChineseSimplifiedCommon() 仅收录约 2500 个高频字，
      // 界面上的"渲染选项"的"项"字不在其中，会退化成 "?"
      if (io.Fonts->AddFontFromFileTTF(path, 18.0f, &cfg, io.Fonts->GetGlyphRangesChineseFull()))
      {
        logInfo(std::string("已加载中文字体: ") + path);
        fontLoaded = true;
        break;
      }
      io.Fonts->Clear(); // 该字体不可用，清掉再试下一个
    }
    if (!fontLoaded)
    {
      io.Fonts->AddFontDefault();
      logWarn("未找到中文字体，回退到默认字体（中文将显示为 ?）");
    }
  }

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  // ------------------------ 渲染器初始化 ------------------------
  // 着色器路径相对于工程根目录
  Renderer renderer;
  if (!renderer.init(2048, 2048))
  {
    logError("渲染器初始化失败");
    glfwTerminate();
    return -1;
  }

  // 天空盒（仓库自带 6 张立方体贴图）
  renderer.loadSkybox({
      "static/texture/skybox/right.jpg",
      "static/texture/skybox/left.jpg",
      "static/texture/skybox/top.jpg",
      "static/texture/skybox/bottom.jpg",
      "static/texture/skybox/front.jpg",
      "static/texture/skybox/back.jpg",
  });

  // 默认光照
  renderer.dirLight().direction = glm::vec3(-0.5f, -1.0f, -0.3f);
  renderer.dirLight().color = glm::vec3(1.0f, 0.98f, 0.92f);
  // 强度 3.5 配合 Reinhard 色调映射会让 R/G/B 全部饱和到 1.0，
  // 结果不管把颜色改成什么都会过曝成白色，因此降到 1.5 左右
  renderer.dirLight().intensity = 1.5f;

  renderer.pointLight(0).position = glm::vec3(3.0f, 3.0f, 3.0f);
  renderer.pointLight(0).color = glm::vec3(0.2f, 0.6f, 1.0f);
  renderer.pointLight(0).intensity = 25.0f;

  renderer.spotLight().position = glm::vec3(0.0f, 6.0f, 0.0f);
  renderer.spotLight().direction = glm::vec3(0.0f, -1.0f, 0.0f);
  renderer.spotLight().enabled = true;

  // ------------------------ 场景构建 ------------------------
  Scene scene;
  SceneObject &ground = scene.addObject("地面", "static/model/rock/rock.obj",
                                        glm::vec3(0.0f, -1.0f, 0.0f));
  ground.scale = glm::vec3(0.02f);

  SceneObject &hero = scene.addObject("主角", "static/model/cerberus/Cerberus.obj",
                                      glm::vec3(0.0f, 0.5f, 0.0f));
  hero.scale = glm::vec3(0.5f);

  // ------------------------ 主循环 ------------------------
  logInfo("进入主循环：WASD 移动，鼠标环视，F1 锁定鼠标，F11/Alt+Enter 全屏，ESC 退出");

  while (!glfwWindowShouldClose(window))
  {
    float dt = g_frameStats.tick();

    processInput(window, dt);
    handleMouseToggle(window);
    handleFullscreenToggle(window);
    applyMouseDelta(); // 平滑消耗本帧累积的鼠标位移

    // ---- ImGui 新帧 ----
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    drawInfoPanel(renderer, scene);
    drawHelpWindow();

    // ---- 收集可见对象 ----
    std::vector<const Model *> models;
    std::vector<glm::mat4> matrices;
    scene.collectVisible(models, matrices);

    // ---- 相机矩阵 ----
    glm::mat4 view = g_camera.viewMatrix();
    float aspect = (g_height == 0) ? 1.0f : static_cast<float>(g_width) / static_cast<float>(g_height);
    glm::mat4 projection = g_camera.projectionMatrix(aspect);

    // ---- Pass 1: 阴影 ----
    renderer.renderShadowPass(models, matrices);

    // ---- Pass 2: 主场景 ----
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_width, g_height);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderer.renderScene(models, matrices, view, projection, g_camera.position());
    renderer.renderSkybox(view, projection);

    // ---- 绘制 ImGui ----
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  // ------------------------ 清理 ------------------------
  // 先销毁场景（释放 Mesh 持有的 GL 资源），再关闭渲染器与上下文
  scene = Scene();
  renderer.destroy();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
