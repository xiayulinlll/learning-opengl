# src 示例运行指南（每次运行前先读这份）

> **约定：每次要运行 `src/` 下任意一个示例，先完整看一遍本文件，再动手。**
> 本文记录的是在这台机器上实测过的环境、正确命令、以及已经踩过的坑，照做可以避免重复调试。

---

## 0. 一句话结论

```bat
set PATH=D:\OpenGL\MinGW\MinGW\bin;%PATH%
mingw32-make.exe dir=02_hello_triangle
cd output && main.exe
```

-   必须用 **`D:\OpenGL\MinGW\MinGW\bin`** 里的 **MinGW.org GCC 9.2.0（32 位 `mingw32`）**。
-   **不要**用 PATH 上默认的 `g++`（它是 `D:\qt6\Tools\mingw1120_64\bin` 的 **64 位 x86_64**），否则一定链接失败。
-   运行时 DLL 必须在 `output\` 里（见第 4 节）。

---

## 1. 为什么（第一次）运行这么慢

### 1.1 结论先说：不是编译慢

实测（本机，32 位工具链）：

| 步骤 | 耗时 |
| --- | --- |
| 编译 `02_hello_triangle/main.cpp` | 约 366 ms |
| 链接（含 assimp 导入库） | 约 146 ms |
| 全量重编 5 个 imgui 目标文件 | 约 3878 ms |
| **全量重建合计** | **约 4.2 秒** |

也就是说：**只要环境一次性配对，从零到出现窗口大约 4~5 秒**。真正"慢"的是环境定位。

### 1.2 慢在哪里（实际排查过程）

1. **先被 64 位编译器误导**：`g++ --version` 能正常输出版本，编译 `.cpp` 也成功，看起来一切正常；
2. 真正失败发生在**链接阶段**，报错信息分散且互相矛盾：
    - `skipping incompatible lib/libglad.a`
    - `machine type x86 conflicts with x64`
    - `undefined symbol: glfwInit` / `undefined symbol: glad_glClear`
3. 于是开始一轮轮试错：换 64 位 → 换 llvm-mingw 的 `i686` 编译器 → 反复清理 `.o` 重编……
    **每试一种工具链都要重新编译一遍，还要判断报错属于"架构"问题还是"符号"问题**，这才是耗时的根源；
4. 期间还被几个"假问题"干扰：`make clean` 自身报错、终端 PATH 里连 `where`/`tasklist`/`findstr` 都没有（无法快速确认进程是否真的跑起来）、DLL 缺失导致程序一闪而过。

**教训**：遇到链接错误，先用 `-dumpmachine` / `objdump -f` 确认架构，不要靠"再编一次看看"来试。

---

## 2. 环境事实表（本机实测）

| 项目 | 值 | 说明 |
| --- | --- | --- |
| 正确工具链 | `D:\OpenGL\MinGW\MinGW\bin` | `g++.exe -dumpmachine` → `mingw32`，GCC **9.2.0**（MinGW.org Build-2），带 GNU `ld` |
| 该目录自带 make | `D:\OpenGL\MinGW\MinGW\bin\mingw32-make.exe` | 可直接用，**没有** `make.exe` |
| 错误工具链（PATH 默认） | `D:\qt6\Tools\mingw1120_64\bin` | `-dumpmachine` → `x86_64-w64-mingw32`，GCC 11.2.0 |
| 另一个不能用的 | `D:\qt6\Tools\llvm-mingw1706_64\bin` | 有 `i686-w64-mingw32-g++.exe`，但用 `ld.lld`，**不认这些老式导入库符号** |
| 随仓库的库 | `lib\libglad.a`、`lib\libglfw3dll.a`、`lib\libassimp.*.a` | 全部是 **32 位 `pe-i386`** |
| 库的符号风格 | `_glfwInit`、`_glad_glClear`（带下划线前缀） | 由 MinGW.org 的 `dlltool` + GNU `ld` 生成 |
| 运行时 DLL 已有 | `output\glfw3.dll`、`output\libassimp.dll` | 仓库自带 |
| 运行时 DLL 需补 | `output\libgcc_s_dw2-1.dll`、`output\libstdc++-6.dll` | 从 `D:\OpenGL\MinGW\MinGW\bin` 拷 |

> 参考（非本机）：项目根 README 里写的是下载 **32-bit** 的 GLFW 预编译包，和这里的 `lib\` 一致，所以必须 32 位工具链。

---

## 3. 标准运行流程

### 3.1 第一步：设置 PATH（每个新终端都要做）

```bat
set PATH=D:\OpenGL\MinGW\MinGW\bin;%PATH%
```

验证（可选，两条输出必须都是 32 位）：

```bat
g++ -dumpmachine                 :: 期望：mingw32
mingw32-make.exe --version       :: 期望：GNU Make 4.x
```

> 注意：`set PATH=...` 只对**当前终端窗口**有效。换窗口 / 重启 VS Code 后要重新执行。

### 3.2 第二步：编译（在项目根目录执行）

`dir=` 只填 `src` 下的**文件夹名**（不要带 `src/`，不要带末尾 `/`）：

```bat
mingw32-make.exe dir=02_hello_triangle
```

正常输出示例：

```
g++ -std=c++17 -Wall -Wextra -g -Iinclude -c src/02_hello_triangle/main.cpp  -o src/02_hello_triangle/main.o
g++ -std=c++17 ... -o output/main.exe src/02_hello_triangle/main.o ... -Llib -lglad -lglfw3dll -llibassimp
Executing 'all' complete!
```

-   产物固定是 **`output\main.exe`**（所有示例共用一个文件名，编译新示例会覆盖旧的）。
-   出现 `warning: unused parameter 'window'` 属于**正常**，不用管。

### 3.3 第三步：运行

```bat
cd output
main.exe
```

-   也可以从项目根目录：`start "" output\main.exe`。
-   示例里加载资源多用相对路径（`static/...`），涉及贴图/模型/着色器文件的示例，**从 `output` 启动时要注意资源路径**；`02_hello_triangle` 不涉及资源，命令行对它是安全的。
-   关闭窗口：按 `ESC`（代码里 `processInput` 处理了 ESC）或点右上角 ×。

### 3.4 清理

```bat
mingw32-make.exe dir=02_hello_triangle clean
```

> ⚠️ 这条在 Windows 上**会报错**（Makefile 的 `RM := del /q a/f` 写错了），见坑 5。手动清理：

```bat
del /q output\main.exe
del /q src\02_hello_triangle\main.o
```

---

## 4. 运行时 DLL

`output\` 里必须有这 4 个 DLL，缺任何一个程序都会"闪退 / 提示找不到 DLL"：

```
output\glfw3.dll               ← 仓库自带
output\libassimp.dll           ← 仓库自带
output\libgcc_s_dw2-1.dll      ← 需从 D:\OpenGL\MinGW\MinGW\bin 拷贝
output\libstdc++-6.dll         ← 需从 D:\OpenGL\MinGW\MinGW\bin 拷贝
```

拷贝命令（在项目根目录，**目标文件名要加引号**，否则 `++` 会被 shell 吃掉）：

```bat
copy /y "D:\OpenGL\MinGW\MinGW\bin\libgcc_s_dw2-1.dll" "output\libgcc_s_dw2-1.dll"
copy /y "D:\OpenGL\MinGW\MinGW\bin\libstdc++-6.dll"    "output\libstdc++-6.dll"
```

只在第一次（或 `output\` 被清空后）做一次即可。

---

## 5. 踩过的坑清单

| # | 坑 | 现象 / 报错 | 原因 | 正确做法 |
| - | --- | --- | --- | --- |
| 1 | **用了 64 位 g++** | `skipping incompatible lib/libglad.a when searching for -lglad`、`cannot find -lglad` | 库是 32 位 `pe-i386`，编译器是 x86_64 | 把 `D:\OpenGL\MinGW\MinGW\bin` 放到 PATH 最前面 |
| 2 | **只换编译器，没同步重编 `.o`** | `machine type x86 conflicts with x64` | `main.o` / `imgui\*.o` 是上一次用另一种架构编出来的 | 换工具链后先删旧 `.o`：`del /q src\<示例>\main.o include\imgui\*.o` |
| 3 | **用 llvm-mingw 的 i686 编译器** | 编译全通过，链接全是 `undefined symbol: glfwInit` / `undefined symbol: glad_glClear` | 该工具链用 `ld.lld`，不识别 MinGW.org 生成的带 `_` 前缀的旧式导入符号 | 直接用 GNU GCC（`D:\OpenGL\MinGW\MinGW`），**别用 llvm-mingw 编这个仓库** |
| 4 | **漏抄 GCC 运行时 DLL** | 双击 `main.exe` 一闪而过 / 弹"找不到 xxx.dll"；从终端启动也无任何输出 | 32 位 GCC 程序依赖 `libgcc_s_dw2-1.dll`、`libstdc++-6.dll` | 按第 4 节补齐 `output\` 的 4 个 DLL |
| 5 | **`make clean` 在 Windows 上本身是坏的** | `del /q a/f output/main.exe` → `无效开关 - "main.exe"`，`Error 1` | Makefile 里 `RM := del /q a/f`，本意应是 `/s` 或 `/f`（**未修改，保持仓库原样**） | 手动 `del`，或忽略该报错 |
| 6 | **`dir=` 参数写法** | 找不到源文件 / 报错 | 写成了 `src/02_hello_triangle` 或 `02_hello_triangle/` | 只写文件夹名：`dir=02_hello_triangle` |
| 7 | **在错误目录执行 make** | `Makefile not found` / 编出了别的示例 | `Makefile` 在项目根，`-Iinclude`、`-Llib` 都是相对路径 | 先 `cd /d D:\OpenGL\start-learning-opengl-main` |
| 8 | **以为 `output\main.exe` 是某个示例专属** | 怎么运行都是上一次的画面 | 所有示例共用 `output\main.exe`，后编的覆盖前面的 | 确认编译日志里的 `src/<你要的示例>/main.o`，再运行 |
| 9 | **终端里 `where`/`tasklist`/`findstr`/`ping` 都"不是内部或外部命令"** | 无法确认进程是否真的启动了，也无法过滤报错 | 当前终端 PATH 被裁过（`System32` 不在里面），但 `g++` 在 | 用完整路径，如 `"%SystemRoot%\System32\tasklist.exe"`；或 `> log.txt` 后读文件 |
| 10 | **`copy` 目标文件名被 `++` 破坏** | `系统找不到指定的文件`，但源文件明明存在 | 命令行里 `libstdc++-6.dll` 未加引号，`++` 被解释掉了 | 源和目标都加双引号（见第 4 节） |
| 11 | **VS Code 任务/调试仍用默认 64 位工具链** | 按 `F5` / `Ctrl+Shift+B` 又回到坑 1 的报错 | `.vscode\tasks.json` 写死 `make dir=13_model_view_projection`，且依赖 PATH 顺序 | 先按 3.1 设好 PATH 再启动 VS Code，并把 `dir=` 改成目标示例（**本次未修改仓库配置**） |
| 12 | **感觉"每次都很慢"** | 全量重建要 4 秒多 | `Makefile` 把 5 个 imgui `.cpp` 一起编 | 别删 `include\imgui\*.o`，编一次后复用；只改动的示例会快速重编 |

---

## 6. 30 秒自查清单

运行前依次确认：

1. [ ] 当前目录是项目根（有 `Makefile`、`src`、`lib`、`include`）
2. [ ] `g++ -dumpmachine` 输出 **`mingw32`**
3. [ ] 用的是 **`mingw32-make.exe`**（确认它来自 `D:\OpenGL\MinGW\MinGW\bin`，不是 64 位工具链）
4. [ ] `dir=` 只写文件夹名
5. [ ] `output\` 里有 4 个 DLL（第 4 节）
6. [ ] 换过工具链 → 删过旧 `.o`

---

## 7. 实测记录：02_hello_triangle（首次跑通）

-   命令：`set PATH=D:\OpenGL\MinGW\MinGW\bin;%PATH%` → `mingw32-make.exe dir=02_hello_triangle` → `cd output` → `main.exe`
-   产物：`output\main.exe`，**PE32 / i386**，2,351,022 字节
-   结果：弹出 800×600 的 `LearnOpenGL` 窗口，深色背景（`25/255` 灰）上一个**橙色方块**（EBO 索引画的两个三角形拼成，片元着色器颜色 `vec4(1.0, 0.5, 0.2, 1.0)`）
-   退出：`ESC`，进程正常结束、无残留
-   校验方式：截取窗口客户区 → 800×600；抽样 30000 点中 7500 点为橙色、22500 点为背景色，与预期一致
