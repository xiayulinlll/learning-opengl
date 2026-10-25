// ============================================================
//  viewcar - 车载 ADAS 感知可视化
//
//  基于 OpenGL 3.3 的自动驾驶 / ADAS 感知结果可视化工具。
//  模拟一套 L2 级辅助驾驶系统的感知 + 规划输出，并实时可视化：
//
//    · 自车状态         位置 / 航向 / 车速 / 前轮转角 / 规划轨迹
//    · 感知目标         轿车 / 卡车 / 行人 / 骑行者 / 锥桶（3D 包围盒）
//    · 风险评估         TTC（碰撞时间）/ 纵向横向距离 / 置信度
//    · 车道线检测       实线 / 虚线 / 道路边界（带宽度渲染）
//    · 传感器视场        前视相机 / 前向雷达 / 360° 激光雷达
//    · 规划与控制        车道保持 P 控制器 + 简易 ACC 纵向控制
//
//  操作：
//    W/A/S/D 移动   鼠标 环视   滚轮 缩放/跟随距离
//    1/2/3/4 切换视角   F1 锁定鼠标   F2 面板   F3 功能列表   F11 全屏
//    ESC 退出   P 暂停   R 重置   H/L/V/G/T 切换各可视化层
// ============================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <locale>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "adas.h"
#include "camera.h"
#include "core.h"
#include "renderer.h"
#include "shader.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

using namespace vc;

// ------------------------------ 常量 ------------------------------
// 鼠标单帧最大位移：快速甩动时避免一帧吃掉过大位移导致视角跳变
constexpr float MOUSE_MAX_DELTA = 400.0f;
// 鼠标平滑系数：主循环每帧消耗累积位移的比例，越小越平滑但越滞后
constexpr float MOUSE_SMOOTHING = 0.55f;

// ------------------------------ 全局状态 ------------------------------
struct AppState
{
  int width = 1600;
  int height = 900;

  bool firstMouse = true;
  double lastX = 0.0;
  double lastY = 0.0;

  // 鼠标位移累积（在主循环里平滑消耗，避免逐帧抖动）
  float pendingX = 0.0f;
  float pendingY = 0.0f;

  float deltaTime = 0.0f;

  bool cursorLocked = true;
  bool showPanel = true;
  bool showHelpWindow = false;

  // 界面字体大小（ImGui 必须重建字体图集才能改变字号）
  float fontSize = 18.0f;
  // 中文字体是否加载成功；失败时面板顶部显示提示
  bool chineseFontOk = true;

  // 全屏状态
  bool fullscreen = false;
  int windowedX = 0;
  int windowedY = 0;
  int windowedW = 1600;
  int windowedH = 900;

  // 面板分区折叠状态
  bool sectionView = true;
  bool sectionSim = true;
  bool sectionVisual = true;
  bool sectionObjectList = true;
  bool sectionPerf = true;
  bool sectionHelp = true;

  // 功能列表窗口：按键速查分区
  bool keyRefOnlyActive = false; // 仅列出当前处于「开启」状态的功能
};

static AppState g_app;
static Camera g_camera;
static AdasWorld g_world;
static Renderer g_renderer;
static VisualSettings g_visual;

// ImGui 面板里改动 BEV 分辨率 / 更新间隔后，本帧请求立刻重建栅格。
//  不重建的话新参数要等下一次 updateInterval 到点才生效，
//  拖动滑块时表现为「卡顿式」响应，很难调参。
static bool g_bevRebuildRequested = false;
static FrameStats g_stats;
static GLFWwindow *g_window = nullptr;

// ============================================================
//  工作目录处理
// ============================================================
// 可执行文件位于 <项目根>/output/ 下，而 shader 资源路径相对项目根，
// 因此在启动时把工作目录切回项目根。
static bool resolveProjectRoot()
{
#ifdef _WIN32
  // 源文件是 UTF-8 编码，而 Windows 控制台默认使用 GBK 代码页，
  // 不切换的话日志里的中文会显示成乱码（如「宸ヤ綔鐩綍」）。
  //
  // 两种输出场景要分别处理：
  //   1) 直接输出到控制台 —— 改控制台输出代码页即可
  //   2) 重定向到文件/管道（如 > log.txt）—— 控制台代码页无效，
  //      此时由 C++ 运行时的 locale 决定编码
  SetConsoleOutputCP(CP_UTF8);

  // 注意：locale 名称与运行时/工具链相关，MinGW 上 ".UTF-8" 会抛
  // std::runtime_error("name not valid")，必须逐个尝试并兜住异常，
  // 否则一个「美化日志」的小改动会直接把程序搞崩。
  {
    bool localeOk = false;
    const char *localeCandidates[] = {"zh_CN.UTF-8", "C.UTF-8", ".UTF-8", "en_US.UTF-8"};
    for (const char *name : localeCandidates)
    {
      try
      {
        std::locale::global(std::locale(name));
        localeOk = true;
        break;
      }
      catch (const std::exception &)
      {
        // 该名称当前运行时不可用，继续尝试下一个
      }
    }
    if (!localeOk)
    {
      // 保持默认 "C" locale。MinGW 的 libstdc++ 通常不提供 UTF-8 locale 名称，
      // 但这不影响使用：日志按 GBK 写入，而 GBK 与 UTF-8 在中文区是互补字节位，
      // 用 UTF-8 读取时中文仍可正确还原（只是极少数生僻字可能对不上）。
      // 仅作提示，不是错误。
      logInfo("运行时无 UTF-8 locale，日志按本地代码页输出（中文一般仍可读）");
    }
  }

  char buffer[MAX_PATH] = {0};
  DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
  if (len == 0 || len >= MAX_PATH)
  {
    logError("无法获取可执行文件路径");
    return false;
  }

  std::string exePath(buffer);
  const size_t pos = exePath.find_last_of("\\/");
  if (pos == std::string::npos)
  {
    logError("无法解析可执行文件目录");
    return false;
  }

  const std::string exeDir = exePath.substr(0, pos);
  const size_t parentPos = exeDir.find_last_of("\\/");
  const std::string projectRoot =
      (parentPos == std::string::npos) ? exeDir : exeDir.substr(0, parentPos);

  if (_chdir(projectRoot.c_str()) != 0)
  {
    logError("切换工作目录失败: " + projectRoot);
    return false;
  }
  logInfo("工作目录已切换到项目根: " + projectRoot);
  return true;
#else
  char buffer[4096] = {0};
  const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (len <= 0)
  {
    logWarn("无法定位可执行文件路径，沿用当前工作目录");
    return true;
  }

  const std::string exePath(buffer, static_cast<size_t>(len));
  const size_t pos = exePath.find_last_of('/');
  const std::string exeDir = (pos == std::string::npos) ? "." : exePath.substr(0, pos);
  const size_t parentPos = exeDir.find_last_of('/');
  const std::string projectRoot =
      (parentPos == std::string::npos) ? exeDir : exeDir.substr(0, parentPos);

  if (chdir(projectRoot.c_str()) != 0)
  {
    logError("切换工作目录失败: " + projectRoot);
    return false;
  }
  logInfo("工作目录已切换到项目根: " + projectRoot);
  return true;
#endif
}

// ============================================================
//  中文字体加载
// ============================================================

// 候选中文字体：优先 TTF（ImGui 对 TTC 的兼容性稍差），再退到 TTC
static const char *const kChineseFontCandidates[] = {
    "C:/Windows/Fonts/Deng.ttf",      // 等线（体积小、字形清晰，首选）
    "C:/Windows/Fonts/simhei.ttf",    // 黑体
    "C:/Windows/Fonts/msyh.ttc",      // 微软雅黑
    "C:/Windows/Fonts/msyhbd.ttc",    // 微软雅黑 Bold
    "C:/Windows/Fonts/simsun.ttc",    // 宋体
    "C:/Windows/Fonts/simkai.ttf",    // 楷体
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", // Linux 文泉驿
    "/System/Library/Fonts/PingFang.ttc",             // macOS 苹方
};

// 加载中文字体到 ImGui 字体图集。
// 返回 true 表示成功加载了中文字体，false 表示回退到内置字体（中文会显示为 ?）。
//
// 注意：必须使用 GetGlyphRangesChineseFull() 而不是 GetGlyphRangesChineseSimplifiedCommon()。
// 后者只收录约 2500 个高频字，本项目界面里的「锥」「桶」「骑」「瞰」等字都不在其中，
// 使用它会出现「大部分中文正常、个别字变问号」的诡异现象。
static bool loadChineseFont(ImGuiIO &io, float sizePixels)
{
  ImFontConfig cfg;
  cfg.OversampleH = 2;   // 中文笔画密集，2 倍水平过采样可显著改善可读性
  cfg.OversampleV = 2;
  cfg.PixelSnapH = true;
  cfg.SizePixels = sizePixels;

  for (const char *path : kChineseFontCandidates)
  {
    // AddFontFromFileTTF 在文件不存在时返回 nullptr（并会在内部断言，Release 下为静默失败）
    if (io.Fonts->AddFontFromFileTTF(path, sizePixels, &cfg,
                                     io.Fonts->GetGlyphRangesChineseFull()) != nullptr)
    {
      logInfo(std::string("已加载中文字体: ") + path);
      return true;
    }
    // 该字体不可用，清空图集再试下一个
    io.Fonts->Clear();
  }

  io.Fonts->AddFontDefault();
  logWarn("未找到可用的中文字体，回退到内置字体（界面中文将显示为 ?）");
  return false;
}

// 重建字体图集并重新上传纹理（切换字号时调用）
static void reloadFont(float sizePixels)
{
  ImGuiIO &io = ImGui::GetIO();

  io.Fonts->Clear();
  const bool ok = loadChineseFont(io, sizePixels);
  g_app.chineseFontOk = ok;

  // 销毁旧纹理并强制 OpenGL3 后端下一帧重建
  if (io.Fonts->TexID != nullptr)
  {
    GLuint oldTex = static_cast<GLuint>(reinterpret_cast<uintptr_t>(io.Fonts->TexID));
    glDeleteTextures(1, &oldTex);
    io.Fonts->TexID = nullptr;
  }
  ImGui_ImplOpenGL3_DestroyFontsTexture();
  ImGui_ImplOpenGL3_CreateFontsTexture();

  logInfo(ok ? "界面字体已重建" : "界面字体已重建（无中文字形）");
}

// ============================================================
//  输入回调
// ============================================================

static void framebufferSizeCallback(GLFWwindow *window, int width, int height)
{
  (void)window;
  if (width <= 0 || height <= 0)
    return;

  g_app.width = width;
  g_app.height = height;
  g_renderer.resize(width, height);
}

static void mouseCallback(GLFWwindow *window, double xpos, double ypos)
{
  (void)window;

  if (g_app.firstMouse)
  {
    g_app.lastX = xpos;
    g_app.lastY = ypos;
    g_app.firstMouse = false;
    return;
  }

  float xoffset = static_cast<float>(xpos - g_app.lastX);
  float yoffset = static_cast<float>(g_app.lastY - ypos); // 屏幕 Y 轴向下，取反
  g_app.lastX = xpos;
  g_app.lastY = ypos;

  // ---- 关键点 ----
  // 快速甩动鼠标时单帧位移可能非常大。正确做法是「钳制」而非「丢弃」：
  // 保留位移方向，只限制单帧幅值上限，否则会出现「鼠标有时候不动」的现象。
  xoffset = clampf(xoffset, -MOUSE_MAX_DELTA, MOUSE_MAX_DELTA);
  yoffset = clampf(yoffset, -MOUSE_MAX_DELTA, MOUSE_MAX_DELTA);

  // 累积到待处理位移，由主循环按固定比例逐帧平滑消耗
  g_app.pendingX += xoffset;
  g_app.pendingY += yoffset;
}

static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
  (void)window;
  (void)xoffset;
  g_camera.processMouseScroll(static_cast<float>(yoffset));
}

// 把累积的鼠标位移平滑地喂给相机
static void applyMouseDelta()
{
  if (std::fabs(g_app.pendingX) < 0.01f && std::fabs(g_app.pendingY) < 0.01f)
  {
    g_app.pendingX = 0.0f;
    g_app.pendingY = 0.0f;
    return;
  }

  const float dx = g_app.pendingX * MOUSE_SMOOTHING;
  const float dy = g_app.pendingY * MOUSE_SMOOTHING;

  g_camera.processMouseMovement(dx, dy);

  g_app.pendingX -= dx;
  g_app.pendingY -= dy;
}

// ------------------------------ 全屏切换 ------------------------------
static void toggleFullscreen()
{
  if (!g_app.fullscreen)
  {
    // 记录当前窗口位置尺寸，退出全屏时还原
    glfwGetWindowPos(g_window, &g_app.windowedX, &g_app.windowedY);
    glfwGetWindowSize(g_window, &g_app.windowedW, &g_app.windowedH);

    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    glfwSetWindowMonitor(g_window, monitor, 0, 0, mode->width, mode->height,
                         mode->refreshRate);
    g_app.fullscreen = true;
    logInfo("已切换到全屏模式");
  }
  else
  {
    glfwSetWindowMonitor(g_window, nullptr, g_app.windowedX, g_app.windowedY,
                         g_app.windowedW, g_app.windowedH, 0);
    g_app.fullscreen = false;
    logInfo("已退出全屏模式");
  }
}



// ============================================================
//  键盘输入
// ============================================================

// 需要持续响应（按住生效）的按键
static void handleContinuousInput()
{
  const float dt = g_app.deltaTime;

  // 相机移动（仅自由视角生效，由 Camera 内部判断模式）
  if (glfwGetKey(g_window, GLFW_KEY_W) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Forward, dt);
  if (glfwGetKey(g_window, GLFW_KEY_S) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Backward, dt);
  if (glfwGetKey(g_window, GLFW_KEY_A) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Left, dt);
  if (glfwGetKey(g_window, GLFW_KEY_D) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Right, dt);
  if (glfwGetKey(g_window, GLFW_KEY_SPACE) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Up, dt);
  if (glfwGetKey(g_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
    g_camera.processKeyboard(CameraMovement::Down, dt);

  // 手动接管：方向键微调转向与目标速度
  EgoVehicle &ego = g_world.ego();
  if (glfwGetKey(g_window, GLFW_KEY_LEFT) == GLFW_PRESS)
  {
    ego.steeringAngle = clampf(ego.steeringAngle - 40.0f * dt, -25.0f, 25.0f);
    g_world.laneKeeper().enabled = false; // 手动接管时关闭车道保持
  }
  if (glfwGetKey(g_window, GLFW_KEY_RIGHT) == GLFW_PRESS)
  {
    ego.steeringAngle = clampf(ego.steeringAngle + 40.0f * dt, -25.0f, 25.0f);
    g_world.laneKeeper().enabled = false;
  }
  if (glfwGetKey(g_window, GLFW_KEY_UP) == GLFW_PRESS)
    ego.targetSpeed = clampf(ego.targetSpeed + 6.0f * dt, 0.0f, 35.0f);
  if (glfwGetKey(g_window, GLFW_KEY_DOWN) == GLFW_PRESS)
    ego.targetSpeed = clampf(ego.targetSpeed - 6.0f * dt, 0.0f, 35.0f);
}

namespace
{

// 边沿检测用的「上一帧按键状态」
struct KeyEdgeState
{
  bool f1 = false, f2 = false, f3 = false, f4 = false, f5 = false;
  bool f11 = false, esc = false, enter = false;
  bool k1 = false, k2 = false, k3 = false, k4 = false, tab = false;
  bool p = false, r = false, c = false;
  bool h = false, l = false, v = false, g = false, t = false;
  bool b = false;      // 街景开关
  bool occ = false;    // 占据栅格热力图开关（O）
  bool drv = false;    // 可行驶区域开关（D）
};

KeyEdgeState g_keyEdge;

// 返回 true 表示本帧刚按下（上一帧未按下）
bool justPressed(int key, bool &prevFlag)
{
  const bool down = glfwGetKey(g_window, key) == GLFW_PRESS;
  const bool fired = down && !prevFlag;
  prevFlag = down;
  return fired;
}

} // namespace

// 需要在 main 中调用，但定义在文件的靠后位置，这里前置声明
static void handleKeyToggles();

// ============================================================
//  ImGui 面板
// ============================================================

// 小工具：绘制一个带颜色的分类标签
static void coloredBullet(const glm::vec3 &color)
{
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.r, color.g, color.b, 1.0f));
  ImGui::TextUnformatted("■");
  ImGui::PopStyleColor();
  ImGui::SameLine();
}

// ------------------------------ 自车状态面板 ------------------------------
static void drawEgoStatus()
{
  const EgoVehicle &ego = g_world.ego();

  ImGui::Text("航向角:  %.1f deg", ego.heading);
  ImGui::Text("车速:    %.1f m/s  (%.1f km/h)", ego.speed, ego.speed * 3.6f);
  ImGui::Text("目标速度:%.1f m/s", ego.targetSpeed);
  ImGui::Text("前轮转角:%.2f deg", ego.steeringAngle);
  ImGui::Text("轴距:    %.2f m", ego.wheelBase);

  // 横向偏差：自车到车道中心的距离，车道保持的核心指标
  const float lateralError = ego.position.x - g_world.laneKeeper().laneCenterX;
  ImGui::Text("横向偏差:%.2f m", lateralError);

  // 车道保持状态
  if (g_world.laneKeeper().enabled)
  {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.5f, 1.0f));
    ImGui::TextUnformatted("车道保持: 已启用");
    ImGui::PopStyleColor();
  }
  else
  {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
    ImGui::TextUnformatted("车道保持: 手动接管");
    ImGui::PopStyleColor();
  }

  ImGui::Text("位置:    (%.1f, %.1f, %.1f)", ego.position.x, ego.position.y,
              ego.position.z);
}

// ------------------------------ 感知统计面板 ------------------------------
static void drawPerceptionStats()
{
  ImGui::Text("目标总数: %d", static_cast<int>(g_world.objects().size()));
  ImGui::Text("最近目标: %.1f m", g_world.nearestObjectDistance());

  // 风险目标：将 TTC 小于 4 秒的目标计数，用颜色分级提示
  const int risky = g_world.riskyObjectCount();
  if (risky > 0)
    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "风险目标: %d  (TTC < 4s)", risky);
  else
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "风险目标: 0");

  ImGui::Separator();
  ImGui::TextUnformatted("分类统计");

  const ObjectClass classes[] = {ObjectClass::Car, ObjectClass::Truck,
                                 ObjectClass::Pedestrian, ObjectClass::Cyclist,
                                 ObjectClass::Cone};
  for (ObjectClass c : classes)
  {
    const int n = g_world.objectCountOfClass(c);
    coloredBullet(colorOf(c));
    ImGui::Text("%s: %d", nameOf(c), n);
  }
}

// ------------------------------ 目标列表 ------------------------------
// 按 TTC 升序展示风险最高的目标，等价于真实 ADAS 里的「危险目标排序」
static void drawObjectList()
{
  std::vector<const PerceptionObject *> sorted;
  sorted.reserve(g_world.objects().size());
  for (const auto &o : g_world.objects())
    sorted.push_back(&o);

  std::sort(sorted.begin(), sorted.end(),
            [](const PerceptionObject *a, const PerceptionObject *b)
            {
              // 有 TTC 的排前面，TTC 小的更靠前
              const float ta = (a->ttc > 0.0f) ? a->ttc : 1e9f;
              const float tb = (b->ttc > 0.0f) ? b->ttc : 1e9f;
              return ta < tb;
            });

  if (ImGui::BeginChild("objlist", ImVec2(0, 190), true))
  {
    // ImGui 1.78 尚无表格 API，这里用 Columns 实现等宽列
    ImGui::Columns(5, "objcols", true);

    ImGui::TextUnformatted("ID");
    ImGui::NextColumn();
    ImGui::TextUnformatted("类型");
    ImGui::NextColumn();
    ImGui::TextUnformatted("纵向");
    ImGui::NextColumn();
    ImGui::TextUnformatted("横向");
    ImGui::NextColumn();
    ImGui::TextUnformatted("TTC");
    ImGui::NextColumn();
    ImGui::Separator();

    int shown = 0;
    for (const PerceptionObject *o : sorted)
    {
      if (shown++ >= 12)
        break; // 只显示前 12 个，避免面板过长

      ImGui::Text("%d", o->id);
      ImGui::NextColumn();

      const glm::vec3 c = colorOf(o->cls);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.r, c.g, c.b, 1.0f));
      ImGui::TextUnformatted(nameOf(o->cls));
      ImGui::PopStyleColor();
      ImGui::NextColumn();

      ImGui::Text("%.1f m", o->longitudinal);
      ImGui::NextColumn();

      ImGui::Text("%.1f m", o->lateral);
      ImGui::NextColumn();

      if (o->ttc > 0.0f)
      {
        // TTC 越小颜色越红
        const float t = clampf(o->ttc / 6.0f, 0.0f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f + t * 0.7f, 0.2f, 1.0f));
        ImGui::Text("%.2f s", o->ttc);
        ImGui::PopStyleColor();
      }
      else
      {
        ImGui::TextUnformatted("--");
      }
      ImGui::NextColumn();
    }

    ImGui::Columns(1);
  }
  ImGui::EndChild();
}

// ------------------------------ 主控制面板 ------------------------------
static void drawMainPanel()
{
  ImGui::SetNextWindowSize(ImVec2(380, 660), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("viewcar - ADAS 可视化", &g_app.showPanel))
  {
    ImGui::End();
    return;
  }

  // ---- 帧率信息 ----
  ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "FPS %.1f   (%.2f ms)",
                     g_stats.fps(), g_stats.frameMs());
  ImGui::Text("窗口: %dx%d   渲染: %.2fx", g_app.width, g_app.height,
              g_renderer.renderScale());

  // ---- 中文字体加载失败时给出明确提示（否则用户只会看到一堆问号）----
  if (!g_app.chineseFontOk)
  {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
    ImGui::TextWrapped("警告: 未能加载系统字体，中文可能显示为 '?'");
    ImGui::PopStyleColor();
  }

  // ---- 界面字体大小（重建字体图集，无需重启）----
  {
    float fontScalePct = g_app.fontSize;
    if (ImGui::SliderFloat("界面字号##fontsize", &fontScalePct, 12.0f, 32.0f, "%.0f px"))
    {
      g_app.fontSize = fontScalePct;
      reloadFont(g_app.fontSize);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("拖动即重建字体，中文字形来自系统字体\n"
                        "若中文显示为 ? 说明系统缺少候选中文字体");
  }

  ImGui::Separator();

  // ================= 视图 / 窗口 =================
  if (ImGui::CollapsingHeader("视图 / 窗口", &g_app.sectionView,
                              ImGuiTreeNodeFlags_DefaultOpen))
  {
    // 相机视角切换按钮（当前视角高亮）
    const CameraMode modes[] = {CameraMode::Chase, CameraMode::Bird,
                                CameraMode::Cockpit, CameraMode::Free};
    for (int i = 0; i < 4; ++i)
    {
      const bool active = (g_camera.mode() == modes[i]);
      if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.9f));

      if (ImGui::Button(nameOfCameraMode(modes[i]), ImVec2(82, 0)))
        g_camera.setMode(modes[i]);

      if (active)
        ImGui::PopStyleColor();

      if (i != 3)
        ImGui::SameLine();
    }

    ImGui::Text("当前视角: %s", nameOfCameraMode(g_camera.mode()));

    ImGui::Spacing();
    ImGui::TextUnformatted("相机参数");
    ImGui::SliderFloat("跟随距离##chase", &g_camera.chaseDistance, 4.0f, 40.0f, "%.1f m");
    ImGui::SliderFloat("跟随高度##chaseH", &g_camera.chaseHeight, 1.0f, 15.0f, "%.1f m");
    ImGui::SliderFloat("鸟瞰高度##birdH", &g_camera.birdHeight, 12.0f, 200.0f, "%.0f m");
    ImGui::SliderFloat("驾驶位眼高##cockpit", &g_camera.cockpitHeight, 0.5f, 2.5f, "%.2f m");

    // 相机的 zoom / speed / sensitivity 都是私有成员，通过临时变量 + setter 回写
    {
      float zoom = g_camera.zoom();
      float speed = g_camera.speed();
      float sens = g_camera.sensitivity();

      if (ImGui::SliderFloat("视野 FOV", &zoom, 20.0f, 100.0f, "%.0f deg"))
        g_camera.setZoom(zoom);
      if (ImGui::SliderFloat("相机速度", &speed, 1.0f, 60.0f, "%.1f m/s"))
        g_camera.setSpeed(speed);
      if (ImGui::SliderFloat("鼠标灵敏度", &sens, 0.02f, 0.60f, "%.3f"))
        g_camera.setSensitivity(sens);
    }

    ImGui::Checkbox("跟随视角允许环视", &g_camera.freeLook);

    ImGui::Spacing();
    if (ImGui::Button("重置相机##resetCam", ImVec2(120, 0)))
      g_camera.reset();
    ImGui::SameLine();
    if (ImGui::Button(g_app.fullscreen ? "退出全屏##fs" : "全屏 F11##fs", ImVec2(120, 0)))
      toggleFullscreen();

    if (ImGui::Button("隐藏面板 (F2)##panel", ImVec2(140, 0)))
      g_app.showPanel = false;
    ImGui::SameLine();
    ImGui::Checkbox("功能列表 (F3)", &g_app.showHelpWindow);
  }

  ImGui::Separator();

  // ================= 仿真 / 控制 =================
  if (ImGui::CollapsingHeader("仿真 / 控制", &g_app.sectionSim,
                              ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::Checkbox("仿真运行 (P)", &g_world.simulationRunning);
    ImGui::SameLine();
    if (ImGui::Button("重置世界 (R)##resetWorld", ImVec2(120, 0)))
      g_world.init();

    ImGui::SliderFloat("时间缩放", &g_world.timeScale, 0.0f, 4.0f, "%.2fx");

    ImGui::Spacing();
    ImGui::TextUnformatted("横向控制（车道保持）");
    ImGui::Checkbox("启用车道保持 (C)", &g_world.laneKeeper().enabled);
    ImGui::SliderFloat("车道中心 X##laneX", &g_world.laneKeeper().laneCenterX,
                       -7.5f, 7.5f, "%.2f m");
    ImGui::SliderFloat("预瞄距离", &g_world.laneKeeper().lookAheadDistance, 2.0f, 40.0f,
                       "%.1f m");
    ImGui::SliderFloat("横向增益 P", &g_world.laneKeeper().gain, 0.05f, 3.0f, "%.2f");
    ImGui::SliderFloat("车道宽度", &g_world.laneKeeper().laneWidth, 2.5f, 4.5f, "%.2f m");

    ImGui::Spacing();
    ImGui::TextUnformatted("纵向控制（简易 ACC）");
    ImGui::SliderFloat("巡航车速", &g_world.ego().targetSpeed, 0.0f, 35.0f, "%.1f m/s");
  }

  ImGui::Separator();

  // ================= 可视化层 =================
  if (ImGui::CollapsingHeader("可视化层", &g_app.sectionVisual,
                              ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::TextUnformatted("感知要素");
    ImGui::Checkbox("车道线 (L)", &g_visual.showLaneLines);
    ImGui::Checkbox("目标包围盒 (H)", &g_visual.showBoundingBoxes);
    ImGui::Checkbox("目标速度矢量", &g_visual.showSpeedVector);
    ImGui::Checkbox("目标预测轨迹", &g_visual.showPredictedPath);
    ImGui::Checkbox("检测点云", &g_visual.showDetectionPoints);
    ImGui::Checkbox("标签引线", &g_visual.showLabels);

    ImGui::Spacing();
    ImGui::TextUnformatted("规控要素");
    ImGui::Checkbox("自车规划轨迹", &g_visual.showEgoPath);
    ImGui::Checkbox("自车行驶轨迹 (T)", &g_visual.showTrail);
    ImGui::Checkbox("自车车身", &g_visual.showEgoBody);

    ImGui::Spacing();
    ImGui::TextUnformatted("场景辅助");
    ImGui::Checkbox("传感器 FOV (V)", &g_visual.showSensorFov);
    ImGui::Checkbox("地面网格 (G)", &g_visual.showGrid);

    ImGui::Spacing();
    ImGui::TextUnformatted("城市街景（双向六车道 + 十字路口 + 高架）");
    ImGui::Checkbox("街景显示", &g_visual.showStreetScene);
    ImGui::Checkbox("车灯自发光", &g_visual.showHeadlights);
    ImGui::SliderFloat("街景密度", &g_visual.streetDensity, 0.3f, 2.0f, "%.2fx");

    if (ImGui::TreeNode("街景构成##streetStats"))
    {
      const AdasWorld &w = g_world;
      ImGui::Text("道具总数: %d", static_cast<int>(w.props().size()));
      ImGui::Separator();
      ImGui::TextUnformatted("— 建筑与绿化 —");
      ImGui::Text("行道树: %d", w.propCountOfKind(static_cast<int>(PropKind::Tree)));
      ImGui::Text("灌木: %d", w.propCountOfKind(static_cast<int>(PropKind::Bush)));
      ImGui::Text("楼房: %d", w.propCountOfKind(static_cast<int>(PropKind::Building)));
      ImGui::Separator();
      ImGui::TextUnformatted("— 街道设施 —");
      ImGui::Text("路灯: %d", w.propCountOfKind(static_cast<int>(PropKind::StreetLamp)));
      ImGui::Text("电线杆: %d", w.propCountOfKind(static_cast<int>(PropKind::UtilityPole)));
      ImGui::Text("信号灯: %d", w.propCountOfKind(static_cast<int>(PropKind::TrafficLight)));
      ImGui::Text("公交站: %d", w.propCountOfKind(static_cast<int>(PropKind::BusStop)));
      ImGui::Text("护栏: %d", w.propCountOfKind(static_cast<int>(PropKind::GuardRail)));
      ImGui::Text("广告牌: %d", w.propCountOfKind(static_cast<int>(PropKind::Billboard)));
      ImGui::Separator();
      ImGui::TextUnformatted("— 交通参与者 —");
      ImGui::Text("路边行人: %d", w.propCountOfKind(static_cast<int>(PropKind::Pedestrian)));
      ImGui::Text("骑行者: %d", w.propCountOfKind(static_cast<int>(PropKind::Cyclist)));
      ImGui::Text("路边停车: %d", w.propCountOfKind(static_cast<int>(PropKind::ParkedCar)));
      ImGui::Separator();
      ImGui::TextUnformatted("— 道路网络 —");
      ImGui::Text("十字路口: %d", static_cast<int>(w.crossRoads().size()));
      ImGui::Text("斑马线: %d", w.propCountOfKind(static_cast<int>(PropKind::Zebra)));
      ImGui::Text("停止线: %d", w.propCountOfKind(static_cast<int>(PropKind::StopLine)));
      ImGui::Text("路口铺装: %d", w.propCountOfKind(static_cast<int>(PropKind::CrossBox)));
      ImGui::TreePop();
    }

    // ============================================================
    //  BEV + 占据栅格（特斯拉 FSD 车机可视化风格）
    // ============================================================
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("BEV 俯视 / 占据栅格（F4 开关 · F5 布局）");

    ImGui::Checkbox("BEV 显示##bevOn", &g_visual.bev.enabled);

    // ---- 布局选择 ----
    {
      const char *layouts[] = {"全屏 BEV", "画中画 BEV", "上下分屏", "BEV 主画面"};
      int cur = static_cast<int>(g_visual.bev.layout);
      if (ImGui::Combo("布局##bevLayout", &cur, layouts, 4))
        g_visual.bev.layout = static_cast<BevLayout>(cur);
    }

    ImGui::Checkbox("自车朝向锁定##bevLock", &g_visual.bev.lockToEgo);
    ImGui::SameLine();
    ImGui::TextDisabled("(?");

    // ---- 视野与分辨率 ----
    ImGui::SliderFloat("前向范围##bevFwd", &g_visual.bev.rangeForward, 40.0f,
                       260.0f, "%.0f m");
    ImGui::SliderFloat("后向范围##bevBack", &g_visual.bev.rangeBack, 10.0f,
                       120.0f, "%.0f m");
    ImGui::SliderFloat("横向范围##bevSide", &g_visual.bev.rangeSide, 20.0f,
                       160.0f, "%.0f m");

    if (ImGui::SliderFloat("栅格分辨率##bevCell", &g_visual.bev.cellSize, 0.20f,
                           2.00f, "%.2f m/格"))
    {
      // 分辨率改变需要立即重建栅格，否则要等下一帧才生效
      g_bevRebuildRequested = true;
    }

    if (ImGui::SliderInt("更新间隔##bevInterval", &g_visual.bev.updateInterval,
                         1, 8, "每 %d 帧"))
    {
      g_bevRebuildRequested = true;
    }

    ImGui::TextDisabled("提示: 分辨率越细、更新越勤，CPU 开销越高");

    // ---- 图层开关 ----
    ImGui::Checkbox("占据栅格热力图 (O)", &g_visual.bev.showOccupancy);
    ImGui::Checkbox("可行驶区域 (D)", &g_visual.bev.showDrivable);
    ImGui::Checkbox("车道拓扑##bevLane", &g_visual.bev.showLaneTopology);
    ImGui::Checkbox("目标框 / 速度矢量##bevObj", &g_visual.bev.showObjects);
    ImGui::Checkbox("预测轨迹##bevPred", &g_visual.bev.showPredictions);
    ImGui::Checkbox("传感器覆盖##bevSens", &g_visual.bev.showSensorCoverage);
    ImGui::Checkbox("米格网##bevGrid", &g_visual.bev.showGrid);

    ImGui::SliderFloat("占据不透明度##bevOccA", &g_visual.bev.occupancyOpacity,
                       0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("可行驶不透明度##bevDrvA",
                       &g_visual.bev.drivableOpacity, 0.0f, 1.0f, "%.2f");

    if (g_visual.bev.layout == BevLayout::PictureInPicture)
    {
      ImGui::SliderFloat("小地图宽度##bevPipW", &g_visual.bev.pipWidthRatio,
                         0.12f, 0.50f, "%.2f × 窗宽");
    }

    // ---- BEV 实时统计 ----
    if (ImGui::TreeNode("BEV 栅格统计##bevStats"))
    {
      const BevStats &bs = g_renderer.stats_bev();

      ImGui::Text("栅格尺寸: %d × %d 格", bs.gridWidth, bs.gridHeight);
      ImGui::Text("可行驶格数: %d", bs.drivableCells);
      ImGui::Text("被占据格数: %d", bs.occupiedCells);
      ImGui::Text("占据比例: %.2f%%", bs.occupancyRatio * 100.0f);
      ImGui::Text("视野内目标: %d", bs.objectsInView);
      ImGui::Text("重建耗时: %.3f ms", bs.buildMs);

      // CPU 预算提示：真实车载平台上一帧的总预算是 33ms(30fps)
      //  或 16ms(60fps)，栅格重建占掉多少要有明确数字。
      const float budget = 16.0f;
      const float pct = static_cast<float>(bs.buildMs) / budget * 100.0f;
      ImGui::Text("占 60fps 预算: %.1f%%", pct);
      if (pct > 25.0f)
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "建议调粗分辨率或加大更新间隔");
      ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("几何参数");
    ImGui::SliderFloat("车道线宽度", &g_visual.laneLineWidth, 0.4f, 4.0f, "%.1f m");
    ImGui::SliderFloat("车道线高度", &g_visual.laneHeight, 0.01f, 0.30f, "%.2f m");
    ImGui::SliderFloat("包围盒放大", &g_visual.boxLineScale, 0.8f, 1.5f, "%.2fx");
    ImGui::SliderFloat("FOV 透明度", &g_visual.fovAlpha, 0.1f, 1.0f, "%.2f");
    ImGui::SliderInt("网格密度", &g_visual.gridDivisions, 10, 80);
    ImGui::SliderFloat("网格范围", &g_visual.gridExtent, 20.0f, 300.0f, "%.0f m");

    ImGui::Separator();
    ImGui::TextUnformatted("画面效果");
    ImGui::Checkbox("雾效", &g_visual.enableFog);
    ImGui::SameLine();
    ImGui::Checkbox("暗角", &g_visual.enableVignette);
    if (g_visual.enableFog)
      ImGui::SliderFloat("雾浓度", &g_visual.fogDensity, 0.0005f, 0.03f, "%.4f");

    // renderScale 是私有成员，用临时变量 + setter 回写
    {
      float rs = g_renderer.renderScale();
      if (ImGui::SliderFloat("渲染缩放", &rs, 0.5f, 1.5f, "%.2fx"))
        g_renderer.setRenderScale(rs);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("光照");
    ImGui::SliderFloat3("主光方向", &g_visual.lightDir.x, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("主光强度", &g_visual.lightIntensity, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("环境光强度", &g_visual.ambientIntensity, 0.0f, 2.0f, "%.2f");

    if (ImGui::TreeNode("天空与雾颜色##skyColors"))
    {
      ImGui::ColorEdit3("天顶##skyTop", &g_visual.skyTop.x);
      ImGui::ColorEdit3("地平线##skyHorizon", &g_visual.skyHorizon.x);
      ImGui::ColorEdit3("地面##skyBottom", &g_visual.skyBottom.x);
      ImGui::ColorEdit3("雾色##fogColor", &g_visual.fogColor.x);
      ImGui::TreePop();
    }

    if (ImGui::Button("恢复默认可视化设置", ImVec2(-1, 0)))
    {
      g_visual = VisualSettings();
      g_renderer.setRenderScale(1.0f);
    }
  }

  ImGui::Separator();

  // ================= 自车状态 =================
  if (ImGui::CollapsingHeader("自车状态", ImGuiTreeNodeFlags_DefaultOpen))
    drawEgoStatus();

  ImGui::Separator();

  // ================= 感知统计 =================
  if (ImGui::CollapsingHeader("感知统计", ImGuiTreeNodeFlags_DefaultOpen))
    drawPerceptionStats();

  ImGui::Separator();

  // ================= 危险目标列表 =================
  if (ImGui::CollapsingHeader("目标列表（按风险排序）", &g_app.sectionObjectList,
                              ImGuiTreeNodeFlags_DefaultOpen))
    drawObjectList();

  ImGui::Separator();

  // ================= 渲染统计 =================
  if (ImGui::CollapsingHeader("渲染统计", &g_app.sectionPerf))
  {
    const RenderStats &st = g_renderer.stats();
    ImGui::Text("实例化目标: %d", st.instanceCount);
    ImGui::Text("线段数量:   %d", st.lineCount);
    ImGui::Text("车道线采样: %d", st.lanePointCount);
    ImGui::Text("三角形数:   %d", st.triangleCount);
    ImGui::Text("Draw Call:  %d", st.drawCalls);
  }

  ImGui::End();
}

// ------------------------------ 按键速查：可点击执行的动作 ------------------------------
// 每个条目把「按键 + 说明 + 当前状态 + 点击动作」绑在一起，
// 这样按键速查表既能当说明书，也能当控制面板用。

// 动作类型：速查表上的按钮点击后要执行的具体行为
enum class QuickAction
{
  None,
  CameraChase, CameraBird, CameraCockpit, CameraFree, CameraCycle,
  ToggleCursorLock, TogglePanel, ToggleHelpWindow, ToggleFullscreen,
  TogglePause, ResetWorld, ToggleLaneKeep,
  ToggleLaneLines, ToggleBoundingBox, ToggleSensorFov, ToggleGrid, ToggleTrail,
  ToggleStreetScene,
  // ---- BEV / 占据栅格 ----
  ToggleBev, CycleBevLayout, ToggleOccupancy, ToggleDrivable,
  ResetCamera,
};

static void runQuickAction(QuickAction action)
{
  switch (action)
  {
  case QuickAction::CameraChase:
    g_camera.setMode(CameraMode::Chase);
    break;
  case QuickAction::CameraBird:
    g_camera.setMode(CameraMode::Bird);
    break;
  case QuickAction::CameraCockpit:
    g_camera.setMode(CameraMode::Cockpit);
    break;
  case QuickAction::CameraFree:
    g_camera.setMode(CameraMode::Free);
    break;
  case QuickAction::CameraCycle:
    g_camera.cycleMode();
    break;

  case QuickAction::ToggleCursorLock:
    g_app.cursorLocked = !g_app.cursorLocked;
    glfwSetInputMode(g_window, GLFW_CURSOR,
                     g_app.cursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (g_app.cursorLocked)
      g_app.firstMouse = true;
    break;

  case QuickAction::TogglePanel:
    g_app.showPanel = !g_app.showPanel;
    break;
  case QuickAction::ToggleHelpWindow:
    g_app.showHelpWindow = !g_app.showHelpWindow;
    break;
  case QuickAction::ToggleFullscreen:
    toggleFullscreen();
    break;

  case QuickAction::TogglePause:
    g_world.simulationRunning = !g_world.simulationRunning;
    break;

  case QuickAction::ResetWorld:
    g_world.init();
    g_renderer.clearEgoTrail();
    break;

  case QuickAction::ToggleLaneKeep:
    g_world.laneKeeper().enabled = !g_world.laneKeeper().enabled;
    break;

  case QuickAction::ToggleLaneLines:
    g_visual.showLaneLines = !g_visual.showLaneLines;
    break;
  case QuickAction::ToggleBoundingBox:
    g_visual.showBoundingBoxes = !g_visual.showBoundingBoxes;
    break;
  case QuickAction::ToggleSensorFov:
    g_visual.showSensorFov = !g_visual.showSensorFov;
    break;
  case QuickAction::ToggleGrid:
    g_visual.showGrid = !g_visual.showGrid;
    break;
  case QuickAction::ToggleTrail:
    g_visual.showTrail = !g_visual.showTrail;
    break;
  case QuickAction::ToggleStreetScene:
    g_visual.showStreetScene = !g_visual.showStreetScene;
    break;

  // ---- BEV / 占据栅格 ----
  case QuickAction::ToggleBev:
    g_visual.bev.enabled = !g_visual.bev.enabled;
    break;

  case QuickAction::CycleBevLayout:
  {
    if (!g_visual.bev.enabled)
      g_visual.bev.enabled = true;
    const int next = (static_cast<int>(g_visual.bev.layout) + 1) %
                     static_cast<int>(BevLayout::Count);
    g_visual.bev.layout = static_cast<BevLayout>(next);
    break;
  }

  case QuickAction::ToggleOccupancy:
    g_visual.bev.showOccupancy = !g_visual.bev.showOccupancy;
    g_visual.bev.enabled = true;
    break;

  case QuickAction::ToggleDrivable:
    g_visual.bev.showDrivable = !g_visual.bev.showDrivable;
    g_visual.bev.enabled = true;
    break;

  case QuickAction::ResetCamera:
    g_camera.reset();
    break;

  case QuickAction::None:
  default:
    break;
  }
}

// 速查条目：按键、功能说明、当前状态文本、对应的可执行动作
struct QuickEntry
{
  const char *key;   // 按键名
  const char *desc;  // 功能说明
  const char *state; // 当前状态文本（空串表示无状态）
  bool stateIsOn;    // 状态是否为「开启」
  QuickAction action;
};

// 绘制可交互的按键速查表（按键按钮 + 功能说明 + 实时状态）
static void drawQuickReferenceTable()
{
  QuickEntry e[] = {
      // ---- 视角切换 ----
      {"1", "追车视角", g_camera.mode() == CameraMode::Chase ? "当前" : "",
       g_camera.mode() == CameraMode::Chase, QuickAction::CameraChase},
      {"2", "鸟瞰视角", g_camera.mode() == CameraMode::Bird ? "当前" : "",
       g_camera.mode() == CameraMode::Bird, QuickAction::CameraBird},
      {"3", "驾驶位视角", g_camera.mode() == CameraMode::Cockpit ? "当前" : "",
       g_camera.mode() == CameraMode::Cockpit, QuickAction::CameraCockpit},
      {"4", "自由视角", g_camera.mode() == CameraMode::Free ? "当前" : "",
       g_camera.mode() == CameraMode::Free, QuickAction::CameraFree},
      {"Tab", "循环切换四种视角", "", false, QuickAction::CameraCycle},
      {"复位", "相机复位（默认姿态）", "", false, QuickAction::ResetCamera},

      // ---- 窗口与鼠标 ----
      {"F1", "锁定 / 解锁鼠标", g_app.cursorLocked ? "已锁定" : "已解锁",
       g_app.cursorLocked, QuickAction::ToggleCursorLock},
      {"F2", "主控制面板", g_app.showPanel ? "显示中" : "已隐藏",
       g_app.showPanel, QuickAction::TogglePanel},
      {"F3", "本功能列表窗口", g_app.showHelpWindow ? "显示中" : "已隐藏",
       g_app.showHelpWindow, QuickAction::ToggleHelpWindow},
      {"F11", "全屏", g_app.fullscreen ? "全屏中" : "窗口",
       g_app.fullscreen, QuickAction::ToggleFullscreen},

      // ---- 仿真 ----
      {"P", "仿真运行", g_world.simulationRunning ? "运行中" : "已暂停",
       g_world.simulationRunning, QuickAction::TogglePause},
      {"R", "重置仿真世界", "", false, QuickAction::ResetWorld},
      {"C", "车道保持（自动转向）", g_world.laneKeeper().enabled ? "开启" : "关闭",
       g_world.laneKeeper().enabled, QuickAction::ToggleLaneKeep},

      // ---- 可视化层 ----
      {"L", "车道线", g_visual.showLaneLines ? "开启" : "关闭",
       g_visual.showLaneLines, QuickAction::ToggleLaneLines},
      {"H", "目标包围盒", g_visual.showBoundingBoxes ? "开启" : "关闭",
       g_visual.showBoundingBoxes, QuickAction::ToggleBoundingBox},
      {"V", "传感器 FOV", g_visual.showSensorFov ? "开启" : "关闭",
       g_visual.showSensorFov, QuickAction::ToggleSensorFov},
      {"G", "地面网格", g_visual.showGrid ? "开启" : "关闭",
       g_visual.showGrid, QuickAction::ToggleGrid},
      {"T", "行驶轨迹", g_visual.showTrail ? "开启" : "关闭",
       g_visual.showTrail, QuickAction::ToggleTrail},
      {"B", "城市街景（树 / 楼 / 行人 / 路口 / 高架）",
       g_visual.showStreetScene ? "开启" : "关闭", g_visual.showStreetScene,
       QuickAction::ToggleStreetScene},

      // ---- BEV / 占据栅格（特斯拉 FSD 风格）----
      {"F4", "BEV 俯视视图", g_visual.bev.enabled ? "开启" : "关闭",
       g_visual.bev.enabled, QuickAction::ToggleBev},
      {"F5", "循环 BEV 布局",
       nameOfBevLayout(g_visual.bev.layout), g_visual.bev.enabled,
       QuickAction::CycleBevLayout},
      {"O", "占据栅格热力图", g_visual.bev.showOccupancy ? "开启" : "关闭",
       g_visual.bev.showOccupancy, QuickAction::ToggleOccupancy},
      {"D", "可行驶区域", g_visual.bev.showDrivable ? "开启" : "关闭",
       g_visual.bev.showDrivable, QuickAction::ToggleDrivable},
  };
  const int count = static_cast<int>(sizeof(e) / sizeof(e[0]));

  ImGui::Checkbox("仅显示已开启的功能", &g_app.keyRefOnlyActive);
  ImGui::SameLine();
  ImGui::TextDisabled("(点击左侧按钮可直接执行)");
  ImGui::Separator();

  ImGui::Columns(3, "quickref", true);
  ImGui::SetColumnWidth(0, 56.0f);
  ImGui::SetColumnWidth(1, 190.0f);

  ImGui::TextUnformatted("按键");
  ImGui::NextColumn();
  ImGui::TextUnformatted("功能");
  ImGui::NextColumn();
  ImGui::TextUnformatted("当前状态");
  ImGui::NextColumn();
  ImGui::Separator();

  for (int i = 0; i < count; ++i)
  {
    // 只看「已开启」时，跳过非开启状态的条目（无状态的常驻动作始终显示）
    if (g_app.keyRefOnlyActive && e[i].state[0] != '\0' && !e[i].stateIsOn)
      continue;

    // ---- 第 1 列：按键名本身就是按钮 ----
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.28f, 0.36f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.42f, 0.58f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.52f, 0.75f, 1.0f));
    ImGui::PushID(i);
    if (ImGui::Button(e[i].key, ImVec2(50.0f, 0.0f)))
      runQuickAction(e[i].action);
    ImGui::PopID();
    ImGui::PopStyleColor(3);
    ImGui::NextColumn();

    // ---- 第 2 列：功能说明 ----
    ImGui::TextUnformatted(e[i].desc);
    ImGui::NextColumn();

    // ---- 第 3 列：实时状态 ----
    if (e[i].state[0] != '\0')
    {
      const ImVec4 col = e[i].stateIsOn ? ImVec4(0.42f, 0.90f, 0.50f, 1.0f)
                                        : ImVec4(0.55f, 0.55f, 0.60f, 1.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, col);
      ImGui::TextUnformatted(e[i].state);
      ImGui::PopStyleColor();
    }
    ImGui::NextColumn();
  }

  ImGui::Columns(1);
}

// ------------------------------ 功能列表 / 操作说明窗口 ------------------------------
static void drawHelpWindow()
{
  if (!g_app.showHelpWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(540, 620), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(410, 12), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("功能列表 / 操作说明 (F3)", &g_app.showHelpWindow))
  {
    ImGui::End();
    return;
  }

  ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "viewcar - 车载 ADAS 感知可视化");
  ImGui::TextWrapped("模拟 L2 辅助驾驶的感知与规划输出，实时可视化目标检测、"
                     "车道线、传感器视场与规控结果。");
  ImGui::Separator();

  // ============ 分区 0：按键速查（可点击执行）============
  if (ImGui::CollapsingHeader("⓪ 按键速查（按钮可直接点击执行）",
                              ImGuiTreeNodeFlags_DefaultOpen))
  {
    drawQuickReferenceTable();
  }

  // 复用同一个键位列表绘制辅助（ImGui 1.78 用 Columns 实现两列）
  const auto drawKeyTable = [](const char *tableId, const char *const rows[][2], int count)
  {
    ImGui::Columns(2, tableId, true);
    ImGui::SetColumnWidth(0, 130.0f);

    ImGui::TextUnformatted("按键");
    ImGui::NextColumn();
    ImGui::TextUnformatted("功能");
    ImGui::NextColumn();
    ImGui::Separator();

    for (int i = 0; i < count; ++i)
    {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.45f, 1.0f));
      ImGui::TextUnformatted(rows[i][0]);
      ImGui::PopStyleColor();
      ImGui::NextColumn();

      ImGui::TextUnformatted(rows[i][1]);
      ImGui::NextColumn();
    }

    ImGui::Columns(1);
  };

  // ============ 分区 1：相机操作 ============
  if (ImGui::CollapsingHeader("① 相机操作", ImGuiTreeNodeFlags_DefaultOpen))
  {
    static const char *rows[][2] = {
        {"W / S", "相机前进 / 后退（自由视角）"},
        {"A / D", "相机左移 / 右移（自由视角）"},
        {"Space / Ctrl", "相机上升 / 下降（自由视角）"},
        {"鼠标移动", "环视（追车 / 驾驶位视角为环绕观察）"},
        {"滚轮", "缩放 FOV；追车视角调节跟随距离"},
        {"方向键 ← / →", "手动接管：调整前轮转角"},
        {"方向键 ↑ / ↓", "调整巡航目标车速"},
    };
    drawKeyTable("keys_cam", rows, 7);
  }

  // ============ 分区 2：视图与窗口 ============
  if (ImGui::CollapsingHeader("② 视图与窗口", ImGuiTreeNodeFlags_DefaultOpen))
  {
    static const char *rows[][2] = {
        {"1", "追车视角（默认）"},
        {"2", "鸟瞰视角（全局态势）"},
        {"3", "第一人称（驾驶位）"},
        {"4", "自由视角（WASD 飞行）"},
        {"Tab", "循环切换以上四种视角"},
        {"F1", "锁定 / 解锁鼠标"},
        {"F2", "显示 / 隐藏主控制面板"},
        {"F3", "显示 / 隐藏本功能列表窗口"},
        {"F11", "切换全屏"},
        {"Alt + Enter", "切换全屏"},
        {"ESC", "全屏时退出全屏；窗口模式退出程序"},
    };
    drawKeyTable("keys_view", rows, 11);
  }

  // ============ 分区 3：仿真控制 ============
  if (ImGui::CollapsingHeader("③ 仿真与可视化层开关", ImGuiTreeNodeFlags_DefaultOpen))
  {
    static const char *rows[][2] = {
        {"P", "暂停 / 继续仿真"},
        {"R", "重置仿真世界"},
        {"C", "开启 / 关闭车道保持（手动接管）"},
        {"L", "车道线显示开关"},
        {"H", "目标包围盒显示开关"},
        {"V", "传感器 FOV 显示开关"},
        {"G", "地面网格显示开关"},
        {"T", "自车行驶轨迹显示开关"},
    };
    drawKeyTable("keys_sim", rows, 8);
  }

  // ============ 分区 4：已实现特性 ============
  if (ImGui::CollapsingHeader("④ 已实现的功能特性", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::BulletText("自车运动学：自行车模型（轴距 / 前轮转角 / 航向角积分）");
    ImGui::BulletText("横向控制：预瞄式 P 控制器实现车道保持");
    ImGui::BulletText("纵向控制：基于前车时距的简易 ACC 加减速");
    ImGui::BulletText("感知目标：轿车 / 卡车 / 行人 / 骑行者 / 锥桶分类");
    ImGui::BulletText("真实车型：每类由多零件装配（车身 / 玻璃 / 车轮 / 车灯）");
    ImGui::BulletText("城市街景：行道树 / 楼房 / 路灯 / 信号灯 / 行人 / 护栏");
    ImGui::BulletText("道路结构：路缘石 + 抬高人行道 + 中央绿化分隔带");
    ImGui::BulletText("3D 包围盒：朝向感知 OBB + 底面强化轮廓");
    ImGui::BulletText("风险评估：TTC 碰撞时间计算与红色告警");
    ImGui::BulletText("车道线：实线 / 虚线 / 道路边界，带宽度渲染");
    ImGui::BulletText("传感器视场：相机 / 毫米波雷达 / 360 度激光雷达锥");
    ImGui::BulletText("规划可视化：规划轨迹 + 目标预测轨迹 + 速度矢量");
    ImGui::BulletText("GPU 实例化：批次归并，整场景仅需数十次 draw call");
    ImGui::BulletText("无限道路：世界循环平移 + 目标与街景动态回收生成");
    ImGui::BulletText("稳定伪随机：街景哈希生成，世界平移无跳变");
    ImGui::BulletText("自发光通道：车灯 / 路灯 / 信号灯 / 楼体窗带");
    ImGui::BulletText("渲染管线：程序化天空 + 前向渲染 + 后处理（暗角 / 对比度）");
    ImGui::BulletText("视觉增强：半兰伯特光照 + 菲涅尔边缘光 + 指数雾");
  }

  // ============ 分区 5：目标类别配色与车型约定 ============
  if (ImGui::CollapsingHeader("⑤ 目标类别配色与车型约定"))
  {
    ImGui::Columns(3, "colors", true);
    ImGui::SetColumnWidth(0, 60.0f);
    ImGui::SetColumnWidth(1, 90.0f);

    ImGui::TextUnformatted("包围盒颜色");
    ImGui::NextColumn();
    ImGui::TextUnformatted("类别");
    ImGui::NextColumn();
    ImGui::TextUnformatted("尺寸（长 x 高 x 宽）");
    ImGui::NextColumn();
    ImGui::Separator();

    const ObjectClass classes[] = {ObjectClass::Car, ObjectClass::Truck,
                                   ObjectClass::Pedestrian, ObjectClass::Cyclist,
                                   ObjectClass::Cone};
    for (ObjectClass c : classes)
    {
      const glm::vec3 col = colorOf(c);
      const glm::vec3 sz = sizeOf(c);

      ImGui::ColorButton("##c", ImVec4(col.r, col.g, col.b, 1.0f),
                         ImGuiColorEditFlags_NoTooltip, ImVec2(40, 16));
      ImGui::NextColumn();

      ImGui::TextUnformatted(nameOf(c));
      ImGui::NextColumn();

      ImGui::Text("%.2f x %.2f x %.2f m", sz.x, sz.y, sz.z);
      ImGui::NextColumn();
    }

    ImGui::Columns(1);

    ImGui::Spacing();
    ImGui::TextWrapped("提示：TTC 小于 3 秒的危险目标包围盒会变为红色，"
                       "未被跟踪器稳定关联的目标会变暗。");
  }

  ImGui::End();
}

// ============================================================
//  main
// ============================================================

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  // ---- 工作目录切回项目根（保证能读到 src/viewcar/shader/...）----
  if (!resolveProjectRoot())
    return -1;

  // ---- 初始化 GLFW ----
  if (!glfwInit())
  {
    logError("GLFW 初始化失败");
    return -1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  glfwWindowHint(GLFW_SAMPLES, 4); // MSAA：ADAS 线条密集，抗锯齿很关键

  g_window = glfwCreateWindow(g_app.width, g_app.height,
                              "viewcar - 车载 ADAS 感知可视化", nullptr, nullptr);
  if (!g_window)
  {
    logError("创建窗口失败");
    glfwTerminate();
    return -1;
  }

  glfwMakeContextCurrent(g_window);
  glfwSwapInterval(1); // 垂直同步，避免无意义的满载功耗

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
  {
    logError("GLAD 初始化失败");
    glfwTerminate();
    return -1;
  }

  logInfo(std::string("OpenGL 版本: ") +
          reinterpret_cast<const char *>(glGetString(GL_VERSION)));

  // ---- 注册回调 ----
  glfwSetFramebufferSizeCallback(g_window, framebufferSizeCallback);
  glfwSetCursorPosCallback(g_window, mouseCallback);
  glfwSetScrollCallback(g_window, scrollCallback);

  // 默认锁定鼠标（追车 / 第一人称视角的惯用交互）
  glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  // 高 DPI 屏幕上帧缓冲尺寸可能大于窗口逻辑尺寸，以实际值为准
  glfwGetFramebufferSize(g_window, &g_app.width, &g_app.height);

  // ---- 初始化 ImGui ----
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGui::GetStyle().WindowRounding = 6.0f;

  // ---- 加载中文字体 ----
  // ImGui 内置字体 ProggyClean 只包含 ASCII 字形，界面上的中文会全部渲染成 '?'。
  // 必须从系统字体文件加载 TTF/TTC 并指定中文字形范围。
  g_app.chineseFontOk = loadChineseFont(io, g_app.fontSize);
  ImGui_ImplGlfw_InitForOpenGL(g_window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  // ---- 初始化渲染器与仿真世界 ----
  if (!g_renderer.init())
  {
    logError("渲染器初始化失败");
    glfwTerminate();
    return -1;
  }
  g_renderer.resize(g_app.width, g_app.height);

  g_world.init();

  // 相机初始姿态：自车后方（追车视角）
  g_camera.setMode(CameraMode::Chase);
  g_camera.setPosition(glm::vec3(0.0f, 3.4f, -9.0f));
  g_camera.setYawPitch(Camera::CAM_DEFAULT_YAW, Camera::CAM_DEFAULT_PITCH);

  logInfo("viewcar 启动完成 —— 按 F3 查看操作说明");

  // ============================================================
  //  主循环
  // ============================================================
  float trailTimer = 0.0f;

  while (!glfwWindowShouldClose(g_window))
  {
    // ---- 帧计时 ----
    g_app.deltaTime = g_stats.tick();
    const float dt = g_app.deltaTime;

    glfwPollEvents();

    // ---- 输入处理 ----
    applyMouseDelta();
    handleContinuousInput();
    handleKeyToggles();

    // ---- 仿真推进 ----
    // 街景密度由 ImGui 面板改动，需同步给世界生成器；
    // 密度变化时重建街道布局（前面的道具也要跟着变疏密）
    if (std::fabs(g_world.streetDensity - g_visual.streetDensity) > 0.001f)
    {
      g_world.streetDensity = g_visual.streetDensity;
      g_world.rebuildStreetNow();
    }

    g_world.update(dt);

    // ---- BEV 栅格参数变动的即时生效 ----
    //  ImGui 面板拖动分辨率 / 更新间隔滑块时，本帧就把栅格重建一次；
    //  否则要等 updateInterval 到点才响应，调参手感很差。
    if (g_bevRebuildRequested)
    {
      g_bevRebuildRequested = false;
      if (g_visual.bev.enabled)
        g_renderer.bevEngine().rebuild(g_world, g_visual.bev);
    }

    // ---- 采样自车轨迹（每 0.15 秒一个点）----
    trailTimer += dt;
    if (trailTimer > 0.15f)
    {
      trailTimer = 0.0f;
      g_renderer.recordEgoTrail(g_world.ego().center());
    }

    // ---- 相机跟随自车 ----
    g_camera.followEgo(g_world.ego().position, g_world.ego().forward(),
                       g_world.ego().heading, dt);

    // ---- ImGui 新帧 ----
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    drawMainPanel();
    drawHelpWindow();

    ImGui::Render();

    // ---- 3D 渲染 ----
    g_renderer.render(g_camera, g_world, g_visual);

    // ---- 叠加 ImGui 界面 ----
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(g_window);
  }

  // ---- 清理 ----
  g_renderer.destroy();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(g_window);
  glfwTerminate();

  logInfo("viewcar 已退出");
  return 0;
}

// 需要边沿检测（按一下触发一次）的按键
static void handleKeyToggles()
{
  // ---- F1：切换鼠标锁定 ----
  if (justPressed(GLFW_KEY_F1, g_keyEdge.f1))
  {
    g_app.cursorLocked = !g_app.cursorLocked;
    glfwSetInputMode(g_window, GLFW_CURSOR,
                     g_app.cursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (g_app.cursorLocked)
      g_app.firstMouse = true;
    logInfo(g_app.cursorLocked ? "鼠标已锁定" : "鼠标已解锁");
  }

  // ---- F2：切换主控制面板 ----
  if (justPressed(GLFW_KEY_F2, g_keyEdge.f2))
    g_app.showPanel = !g_app.showPanel;

  // ---- F3：切换功能列表窗口 ----
  if (justPressed(GLFW_KEY_F3, g_keyEdge.f3))
    g_app.showHelpWindow = !g_app.showHelpWindow;

  // ---- F11：切换全屏 ----
  if (justPressed(GLFW_KEY_F11, g_keyEdge.f11))
    toggleFullscreen();

  // ---- Alt + Enter：切换全屏 ----
  {
    const bool enterDown = glfwGetKey(g_window, GLFW_KEY_ENTER) == GLFW_PRESS;
    const bool altDown = glfwGetKey(g_window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                         glfwGetKey(g_window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    if (enterDown && !g_keyEdge.enter && altDown)
      toggleFullscreen();
    g_keyEdge.enter = enterDown;
  }

  // ---- ESC：全屏时先退全屏，否则退出程序 ----
  if (justPressed(GLFW_KEY_ESCAPE, g_keyEdge.esc))
  {
    if (g_app.fullscreen)
      toggleFullscreen();
    else
      glfwSetWindowShouldClose(g_window, true);
  }

  // ---- 1/2/3/4：直接切换相机视角 ----
  if (justPressed(GLFW_KEY_1, g_keyEdge.k1))
    g_camera.setMode(CameraMode::Chase);
  if (justPressed(GLFW_KEY_2, g_keyEdge.k2))
    g_camera.setMode(CameraMode::Bird);
  if (justPressed(GLFW_KEY_3, g_keyEdge.k3))
    g_camera.setMode(CameraMode::Cockpit);
  if (justPressed(GLFW_KEY_4, g_keyEdge.k4))
    g_camera.setMode(CameraMode::Free);

  // ---- Tab：循环切换视角 ----
  if (justPressed(GLFW_KEY_TAB, g_keyEdge.tab))
  {
    g_camera.cycleMode();
    logInfo(std::string("视角切换为: ") + nameOfCameraMode(g_camera.mode()));
  }

  // ---- P：暂停 / 继续仿真 ----
  if (justPressed(GLFW_KEY_P, g_keyEdge.p))
  {
    g_world.simulationRunning = !g_world.simulationRunning;
    logInfo(g_world.simulationRunning ? "仿真已继续" : "仿真已暂停");
  }

  // ---- R：重置世界 ----
  if (justPressed(GLFW_KEY_R, g_keyEdge.r))
  {
    g_world.init();
    logInfo("仿真世界已重置");
  }

  // ---- C：切换车道保持（自动 / 手动接管）----
  if (justPressed(GLFW_KEY_C, g_keyEdge.c))
  {
    g_world.laneKeeper().enabled = !g_world.laneKeeper().enabled;
    logInfo(g_world.laneKeeper().enabled ? "车道保持已启用（自动）"
                                         : "车道保持已关闭（手动接管）");
  }

  // ---- 可视化层开关 ----
  if (justPressed(GLFW_KEY_H, g_keyEdge.h))
    g_visual.showBoundingBoxes = !g_visual.showBoundingBoxes;
  if (justPressed(GLFW_KEY_L, g_keyEdge.l))
    g_visual.showLaneLines = !g_visual.showLaneLines;
  if (justPressed(GLFW_KEY_V, g_keyEdge.v))
    g_visual.showSensorFov = !g_visual.showSensorFov;
  if (justPressed(GLFW_KEY_G, g_keyEdge.g))
    g_visual.showGrid = !g_visual.showGrid;
  if (justPressed(GLFW_KEY_T, g_keyEdge.t))
    g_visual.showTrail = !g_visual.showTrail;

  // ---- B：切换街景（背景城市环境）----
  if (justPressed(GLFW_KEY_B, g_keyEdge.b))
  {
    g_visual.showStreetScene = !g_visual.showStreetScene;
    logInfo(g_visual.showStreetScene ? "街景已显示" : "街景已隐藏");
  }

  // ---- F4：BEV 视图开关（特斯拉 FSD 风格俯视可视化）----
  if (justPressed(GLFW_KEY_F4, g_keyEdge.f4))
  {
    g_visual.bev.enabled = !g_visual.bev.enabled;
    logInfo(g_visual.bev.enabled ? "BEV 视图已开启（F4 关闭 / F5 切换布局）"
                                 : "BEV 视图已关闭");
  }

  // ---- F5：在四种 BEV 布局之间循环 ----
  //  对应特斯拉车机的四种形态：全屏 / 画中画 / 分屏 / BEV 主画面
  if (justPressed(GLFW_KEY_F5, g_keyEdge.f5))
  {
    if (!g_visual.bev.enabled)
      g_visual.bev.enabled = true; // 没开 BEV 时按 F5 直接开启

    const int next =
        (static_cast<int>(g_visual.bev.layout) + 1) %
        static_cast<int>(BevLayout::Count);
    g_visual.bev.layout = static_cast<BevLayout>(next);

    logInfo(std::string("BEV 布局: ") + nameOfBevLayout(g_visual.bev.layout));
  }

  // ---- O：占据栅格热力图开关 ----
  if (justPressed(GLFW_KEY_O, g_keyEdge.occ))
  {
    g_visual.bev.showOccupancy = !g_visual.bev.showOccupancy;
    g_visual.bev.enabled = true;
    logInfo(g_visual.bev.showOccupancy ? "占据栅格已显示" : "占据栅格已隐藏");
  }

  // ---- D：可行驶区域开关 ----
  if (justPressed(GLFW_KEY_D, g_keyEdge.drv))
  {
    g_visual.bev.showDrivable = !g_visual.bev.showDrivable;
    g_visual.bev.enabled = true;
    logInfo(g_visual.bev.showDrivable ? "可行驶区域已显示" : "可行驶区域已隐藏");
  }
}
