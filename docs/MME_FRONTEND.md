# P5.1：无设备 FX 源码前端

`mikudancestudio_mme_frontend` 从原始 `.fx` 创建不可变 `EffectDefinition`，
`mikudancestudio_mme_runtime` 复用既有标准参数与场景计划 CPU 算法。两者的公共
接口不包含 COM、D3DX、窗口或旧编辑器对象。Windows 原生引擎也链接这两个库，
没有复制一份源码解析器。

## 使用

```powershell
cmake -S cmake/portable-mme -B out/build-mme -DMDS_DXC=C:/tools/dxc.exe
cmake --build out/build-mme --config Release --target mds_effect_cli mme_source_test mme_definition_test mme_legacy_source_test mme_standard_parameters_test mme_scene_plan_test mme_frontend_native_test
ctest --test-dir out/build-mme -C Release -L p5 --output-on-failure
out/build-mme/production/Release/mds_effect_cli.exe inspect assets/effects/frontend/Inspect.fx --dxc C:/tools/dxc.exe -D MME_MIPMAP=
```

Linux／macOS 使用相同 CMake 入口，指定本机 DXC；单配置生成器的程序路径为
`out/build-mme/production/mds_effect_cli`。`mme_frontend_native_test` 仅 Windows
存在，直接调用原生 `D3DPreprocess` 和 `ID3DXEffectCompiler` 的共同
`ID3DXBaseEffect` 接口，不创建设备／窗口。用编译器对象读取元数据，避免为纯
源码验收依赖可用的 DX9 显示会话；实际绘制的原生对照仍属于后续 P5.4。

原生测试默认从系统读取 `d3dx9_43.dll`。干净 CI 用
`python tools/fetch_d3dx_oracle.py --output out/d3dx-oracle` 获取 SHA256 固定的
Microsoft NuGet 包，通过 `-DMDS_D3DX_ORACLE=.../D3DX9_43.dll` 明确指定。
必须一起提供 `D3DX9_43.dll` 和同包的 `D3DCompiler_43.dll`：D3DX 在创建编译器时
动态加载后者，单独加载 D3DX 成功并不能证明编译器依赖完整。显式路径会验证并
预加载同目录编译器，避免被本机安装的旧 SDK 掩盖缺项。这两个 DLL 仅用于独立
参考，未链接／打包进 Qt 编辑器或前端 CLI。

`inspect` 支持重复 `-I DIRECTORY`、`-D NAME=VALUE`，以及 `--output report.json`。
成功报告使用 `status: described`、`executionValidated: false`；导入描述不表示
着色器、脚本、状态或后端能够执行。失败返回非零及分阶段 JSON 诊断。

## 本批范围

- 复用现有有界 FX9 声明解析器，保留参数类型、数组、共享／不可变标志、默认
  **表达式**、注解、纹理／采样器、技术和 Pass 顺序。原有严格执行导入入口继续
  拒绝未知状态；描述入口可以记录它们，但不会批准执行。
- UTF-8（可带 BOM）、UTF-16LE BOM、严格 CP932。非法编码拒绝加载，不依赖系统
  ACP；相对路径包含非 ASCII 字符和空格时仍可加载。
- 引号 include 先查引用文件所在目录，再按 `-I` 顺序；尖括号只查 `-I`。
  支持字面文件名、相对路径、include guard 和 `#pragma once`。默认宏为
  `_INDEX=PSIZE15`；`MME_MIPMAP` 由宿主／CLI 显式选择。
- 文件内容在一次加载中只读取一次，依赖图和宏一起保存在定义中。先冻结字面
  include，再让 DXC 以 legacy macro expansion 处理条件、函数宏和 token paste。
  使用 ASCII 文件编号交给预处理器，再用冻结来源图恢复 UTF-8 `#line`／`__FILE__`
  和诊断路径，避免依赖外部编译器的 Unicode 文件名诊断实现。非法工具日志字节
  显式转义，不能破坏 JSON；不修改宿主进程 cwd，不通过 shell 执行工具。
- 默认 16 MiB 源码／展开预算、256 文件、64 层 include、30 秒工具超时。合法的
  guarded cycle 保留条件语义，活动的无保护循环／深度溢出明确失败。
- 依赖图保守包含非活动分支中实际存在的字面文件；缺失 include 在非活动分支
  不报错。非活动文件也要满足编码和预算限制。这不是仅活动依赖的缓存实现。

首批明确拒绝活动的宏文件名 include、绝对 include、`include_next`／`import`、
预处理器内部保留标识符及现有声明解析器不支持的语法。默认表达式尚未被计算为
实例字节；此工作属于 P5.2。描述不尝试覆盖任意 FX9 语法或完整 Ray。

## 验证与后续

Windows 本地独立无 GPU 构建的 P5 测试 **8/8 通过**：四项定义／旧 CPU 算法，
源码加载、CLI、生产依赖审计、原生编译器对照。原生对照覆盖公开 `Inspect.fx`、
`LegacyMaterial.fx`、`LegacyScene.fx` 的宏 token、参数类型／尺寸／共享标志／
语义、注解、技术／Pass 顺序，以及部分默认值；没有用新解析器生成参考值。
RHI 构建另注册原有运行时着色器编译器回归，验证共用子进程提取。

现有 Portable editor 的三个平台作业运行 `p4|p5`。初轮仅新增前端门禁出现失败：
Windows 原生参考缺少动态加载的编译器；Linux／macOS 的 Unicode 文件名诊断分别
损坏了工具日志 UTF-8 和位置映射。此次 CTest 中原有 P4 用例全部通过。已修复实际依赖及来源映射，
保留原有断言，未重复完整构建矩阵。

最终纯前端 CI [42020565345](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/42020565345)
通过 Windows **8/8**、Linux **7/7**、macOS **7/7**。下载三份 CLI 报告后，排除
工具版本和源文件原始字节数，所有语义字段、路径、编码和 include 边严格一致。
二进制依赖审计均通过；原始失败、修复和相关渲染／基础回归见
[版本化验收证据](evidence/p5-frontend-42020565345.json)。P5.1 仅在上述明确子集内通过，
P5／C2 整体仍未完成。

纯前端补验可用 Portable editor 的 `mme_only=true`，按 `platform` 选择独立作业，
只构建专用入口并运行 Windows 8 项／Linux、macOS 各 7 项无设备测试；不安装 Qt、
不创建 GPU 设备，也不重复发布包。macOS 复用渲染工作流已建立的固定 DXC 缓存。

本地 Ray 根效果的原始文件已通过预处理，随后在 `Shader/math.fxsub:97` 的
`EncodeRGBM` 函数参数默认值处明确报告解析缺口。该结果只证明真实 include 链
能够进入前端，不代表 Ray 导入或渲染通过。第三方效果未复制进仓库。

下一批按 [P5 计划](CROSS_PLATFORM_P5.md) 进入实例与共享池：先确认类型／默认
值求值、初始化顺序及写回点，再接参数语义和帧快照，随后推进脚本、RHI 和 Qt。
