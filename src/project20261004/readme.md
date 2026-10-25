# project20261004 —— 基于 OpenGL 3.3 的实时 PBR 渲染器

一个从零实现的实时基于物理的渲染（Physically Based Rendering, PBR）演示程序。
使用 **Cook-Torrance BRDF + GGX 法线分布 + Smith 几何衰减 + Fresnel-Schlick 近似**，
支持平行光 / 点光源 / 聚光灯、阴影贴图（3x3 PCF）、法线贴图、金属度-粗糙度工作流，
并带有天空盒（Cubemap）、轨道相机与 ImGui 实时调参面板。

## 构建与运行

```bash
make run dir=project20261004
```

- 编译产物：`output/main.exe`
- 依赖：GLFW3、GLAD、assimp（`lib/` 目录下已附带）
- 程序启动时会自动把工作目录切换到项目根目录，因此可以直接双击或在任意目录运行

## 功能特性

### 渲染管线
- **OpenGL 3.3 Core Profile**，前向渲染（Forward Rendering）
- **Cook-Torrance BRDF**
  - 法线分布函数：GGX / Trowbridge-Reitz
  - 几何衰减函数：Schlick-GGX + Smith（考虑视线与光线方向）
  - 菲涅尔项：Schlick 近似，介电质基础反射率 `F0 = 0.04`
  - 能量守恒：`kD = (1 - F) * (1 - metallic)`
- **色调映射**：Reinhard `color / (color + 1)`
- **Gamma 校正**：`pow(color, 1/2.2)`，线性空间 → sRGB

### 光照
- **平行光**（Directional Light）：带方向、颜色、强度与阴影开关
- **点光源**（最多 4 个）：带常数 / 线性 / 二次衰减系数
- **聚光灯**（Spot Light）：内圆锥到外圆锥平滑过渡
- **半球环境光近似**：`ambient = 0.03 * albedo * ao`

### 阴影
- 从光源视角渲染深度图（`depth.vert` / `depth.frag`）
- 3x3 PCF 软阴影
- 动态偏移（bias）依据法线与光方向夹角自适应，缓解阴影痤疮
- 超出阴影贴图范围自动判定为无阴影

### 材质
- 金属度-粗糙度工作流（Metallic-Roughness Workflow）
- glTF 通道约定：metallic 取贴图 **B** 通道，roughness 取 **G** 通道，AO 取 **R** 通道
- 支持贴图：albedo / normal / metallic / roughness / AO
- 法线贴图使用 TBN 矩阵变换到世界空间
- 未提供贴图时自动回退到 uniform 材质参数

### 场景资源
- 天空盒 Cubemap：`static/texture/skybox/*.jpg`（6 面）
- 模型：`static/model/rock/rock.obj`、`static/model/cerberus/Cerberus.obj`
- 纹理：`static/model/rock/Rock-Texture-Surface.jpg`

> 说明：所用的 assimp 版本较旧，仅提供 Blinn-Phong 风格材质接口
> （无 `AI_MATKEY_METALLIC_FACTOR` / `AI_MATKEY_ROUGHNESS_FACTOR`），
> 因此 metallic / roughness 由传统 `shininess` 等参数推导得到。

## 操作方式

| 按键 | 功能 |
| --- | --- |
| `W` / `A` / `S` / `D` | 相机前后左右移动 |
| `Q` / `E` | 相机下降 / 上升 |
| `Shift` | 移动加速（4.0 → 8.0 单位/秒） |
| 鼠标移动 | 环视（需鼠标处于锁定状态） |
| `F1` | 切换鼠标锁定 / 解锁 |
| `F11` 或 `Alt+Enter` | 切换全屏 / 窗口模式 |
| `F2` | 显示 / 隐藏「功能列表 / 操作说明」窗口 |
| 鼠标滚轮 | 缩放（调整 FOV） |
| `ESC` | 退出程序 |

> 程序内置一个 **「功能列表 / 操作说明」** 窗口（默认显示在右侧），
> 把所有按键、面板功能和已实现的渲染特性集中列出，可按 `F2` 开关，
> 也可在主面板的「视图 / 窗口」分组里用复选框控制。

> 启动时鼠标默认为 **锁定**（`GLFW_CURSOR_DISABLED`），此时鼠标只用于环视，无法点击 ImGui 面板。
> 想拖动面板上的滑块，请先按 **`F1`** 解锁鼠标；操作完再按 `F1` 锁回。

### 鼠标手感说明

- 鼠标位移采用 **累积 + 逐帧平滑消耗** 的方式处理，
  即使鼠标事件频率与渲染帧率不一致，视角也不会跳变或丢步。
- 单帧位移上限为 `400` 像素（`MOUSE_MAX_DELTA`），用于过滤驱动异常跳变。
  注意这里是 **钳制** 而非丢弃 —— 早期版本把超过 100px 的位移直接置 0，
  导致快速甩鼠标时位移被整体吃掉，表现为「鼠标有时候不动」。
- 平滑系数 `MOUSE_SMOOTHING = 0.55`，数值越大跟手越紧，越小越平滑但越「拖」。
- 灵敏度可在面板「相机 → 鼠标灵敏度」中实时调整，建议范围 `0.03 ~ 0.05`。

## 常见问题

### UI 中文显示为 “?”

ImGui 内置的默认字体 `ProggyClean.ttf` 仅包含 ASCII 字形，渲染中文时会退化为 `?`。
本程序在 ImGui 初始化阶段调用 `AddFontFromFileTTF()` 加载系统中文字体解决该问题：

- 候选字体依次为 `Deng.ttf`（等线）→ `simhei.ttf`（黑体）→ `msyh.ttc`（微软雅黑）→ `simsun.ttc`（宋体），
  取第一个加载成功的；全部失败则回退到 `AddFontDefault()` 并打印警告。
- 字形区间使用 **`GetGlyphRangesChineseFull()`**（覆盖 `0x4E00–0x9FAF`，约 2 万字），
  **不要**换成 `GetGlyphRangesChineseSimplifiedCommon()` —— 后者仅收录约 2500 个高频字，
  像“项”这类字不在其中，仍会显示成 `?`。

### 鼠标移动一点点，视角就转得很快

`Camera` 的鼠标灵敏度单位为“度/像素”（`m_sensitivity`）。默认值已从 `0.1` 调整为 `0.05`，
并可在 ImGui 面板 **相机 → 鼠标灵敏度** 中实时调节（范围 `0.01 ~ 0.20`）。

此外 `mouseCallback()` 增加了两道防抖：

1. 进入锁定状态的第一次鼠标事件只记录基准点，不产生旋转，避免首次坐标跳变导致视角猛转；
2. 单次位移超过 100 像素时判定为异常跳变并丢弃。

### 改了光源颜色，画面却看不出变化

这是最容易踩的坑，通常由以下原因之一造成：

1. **聚光灯此前根本没有颜色控件**（已修复）。
   旧版 ImGui 面板只提供了 `启用 / 位置 / 方向 / 内角 / 外角`，
   而 `pbr.frag` 第 284 行确实使用了 `uSpotLight.color`，导致"颜色改了没反应"的错觉其实是无处可改。
   现已补上 `颜色##spot`。

2. **方向光强度过高导致过曝**。
   着色器第 244 行 `radiance = uDirLight.color * uDirLight.intensity`，
   随后第 295 行做 Reinhard 色调映射 `color / (color + 1)`。
   当强度为 `3.5` 时，辐射量约 `(3.5, 3.43, 3.22)`，映射后全部趋近 `1.0`，
   于是**无论把颜色调成什么，最终都显示为白色**。
   现已把默认强度从 `3.5` 降到 `1.5`，面板上强度上限仍为 `20`，可从低往高调。

3. **点光源 / 聚光灯的衰减过强，影响范围太小**。
   着色器使用平方反比衰减：

   ```glsl
   attenuation = 1.0 / (constant + linear * d + quadratic * d * d);
   ```

   取默认 `1.0 / 0.09 / 0.032` 时，距离 `d = 5` 处衰减系数仅 `0.44`，`d = 10` 处仅 `0.09`，
   即光源只照亮半径约 5 单位的一小块区域。
   如果相机没对着那块区域，改颜色自然"看不出变化"。
   现已把 `常数衰减 / 线性衰减 / 二次衰减` 也做成滑块，调小它们可显著扩大影响范围。

4. **ImGui 标签 ID 冲突**。
   ImGui 以字符串哈希标识控件，同一窗口内 `ColorEdit3("颜色", ...)` 出现两次会共用 ID，
   导致两个颜色框联动或在重排时错位。
   现已给所有控件加上唯一的 `##` 后缀（`##dir` / `##point` / `##spot`）。

**排查手段**：面板「光照」分组底部新增了 **光照诊断** 区域，实时显示：

```
方向光 RGB = (1.00, 0.98, 0.92)
方向光辐射 = (1.50, 1.47, 1.38)      <- 颜色 x 强度，超过 2.0 就会开始过曝
点光源 RGB = (0.20, 0.60, 1.00)  d0 = 5.20
聚光灯 RGB = (1.00, 1.00, 1.00)  d0 = 6.31
[重置光照为默认值]
```

只要在这里看到 RGB 数值跟着你的调色变化，就说明参数已经写入 CPU 端；
若画面仍无变化，再看「辐射」是否过大（过曝）或光源距离 `d0` 是否太远（衰减吃掉）。

### 按 WASD 感觉“没反应”或“画面在绕圈”

- 相机初始位于 `(0, 2, 6)`，场景尺度约为 **±20 单位**，而移动速度为 **4 单位/秒**，
  按住 `W` 约 1.5 秒即可穿过整个场景，视觉上变化极快。可用 `Shift` 之外的场合适当降低需求。
- 若鼠标灵敏度偏高，俯仰角（pitch）会在轻微抖动中持续累积，使相机不断低头或抬头，
  观感上就像“绕着天空和地面转圈”。把灵敏度调到 `0.05` 以下即可明显改善。
- 鼠标处于锁定状态时，鼠标无法点击窗口以外的区域；若不慎点到别处导致窗口失去焦点，
  需要重新点击窗口内部再操作。

## 目录结构

```
src/project20261004/
├── main.cpp        # 程序入口：初始化 GLFW/GLAD、ImGui，主循环
├── renderer.h/.cpp # 渲染器：阴影 Pass + 主 Pass + 天空盒，着色器与 uniform 管理
├── scene.h/.cpp    # 场景：光源、物体、材质与渲染组织
├── mesh.h/.cpp     # 网格：assimp 模型加载、顶点数据上传、材质属性解析
├── shader.h/.cpp   # 着色器：编译 / 链接 / 错误检查 / uniform location 缓存
├── texture.h/.cpp  # 纹理：stb_image 加载、Cubemap 加载
├── camera.h/.cpp   # 相机：FPS 轨道相机（键盘 + 鼠标）
├── light.h         # 光源结构定义（平行光 / 点光源 / 聚光灯）
├── core.h          # 公共头文件与常量
└── shader/
    ├── pbr.vert / pbr.frag       # PBR 主着色器（GGX + 阴影 PCF + 法线贴图）
    ├── depth.vert / depth.frag   # 阴影贴图深度渲染
    └── skybox.vert / skybox.frag # 天空盒
```

## 实现要点

1. **阴影 Pass**：`glCullFace(GL_FRONT)` + `GL_DEPTH_TEST`，使用正交投影光空间矩阵，
   通过 `uLightSpaceMatrix` 传入主 Pass 做坐标变换。
2. **uniform 缓存**：`Shader` 类内部用 `unordered_map` 缓存 uniform location，避免每帧重复查询。
3. **工作目录自适应**：`resolveProjectRoot()` 通过 `GetModuleFileNameA`（Windows）
   或 `readlink`（Linux）定位可执行文件位置，再 `_chdir` 回项目根目录，
   保证相对路径资源在任意工作目录下都能正确加载。
