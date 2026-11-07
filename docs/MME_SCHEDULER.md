# P5.3：脚本、分配及跨效果目标调度

公共接口是 [`mme_schedule.hpp`](../include/mikudancestudio/mme_schedule.hpp)。它消费 P5.1 的
不可变定义、P5.2 的实例／参数事务，以及宿主冻结的对象、材质条件和语义上下文；不需要原生
SAS、COM、窗口或 GPU。输出是完整有序的 CPU 执行计划和资源依赖图。

当前本地门禁通过，三平台定向验收待执行。P5.4 负责将计划中的材质／Buffer 操作装配为真实
着色器与几何绘制，P5.5 才接入 Qt 效果服务。计划报告明确标记 `executionValidated=false`；
Null 资源操作通过不代表 GPU 效果已运行，更不代表 C2 或完整 Ray 已通过。

## 模块与执行边界

- `schedule_description.cpp`：从原始 FX 描述读取目标、技术条件和脚本命令。
- `schedule_assignment.cpp`：统一普通自动查找、显式覆盖和 DefaultEffect 分配。
- `schedule_emm.cpp`：读取已解码 `.emm` 文本并事务式映射到宿主对象／目标作用域。
- `schedule_targets.cpp`：持有 CPU 资源身份、发布者、代次和重建规则。
- `schedule_frame.cpp`：展开子目标与嵌套场景，冻结参数，校验整帧资源依赖。
- `schedule_report.cpp`：输出不含机器绝对路径的稳定 JSON 计划。

旧 Windows SAS 与新路径共用 `mme_assignment_rules.h` 的 Subset／通配符规则；场景循环、
捕获及材质脚本复用原有 `MmePortableSceneLayout`／`MmePortableMaterialLayout`。
`RenderPassState` 提取为纯 CPU 的 `mikudancestudio_render_contracts`，与 Null／原生后端
继续使用同一份实现。生产依赖审计覆盖该闭合目标图，未为调度引入整套 RHI 或图形库依赖。

本批支持主颜色轮次的 CPU 计划。技术选择器保留 object、object_ss、shadow、edge、zplot
五种 MmdPass；实际主颜色计划暂只接 object，其他模式明确拒绝，待 P5.4 装配多类绘制。
技术有效性必须由调用方提供；编译／能力检查未知不能等同成功。选择第一个有效匹配，全部
匹配均无效则失败，无匹配交回宿主绘制。匹配到零 Pass 技术则跳过绘制，不能当作缺失技术。
Subset 的空串保留原版“隐藏全部子集”的哨兵；数值限定为跨平台一致的有符号 32 位。

普通材质支持有序 Pass 和 RenderColorTarget0..3／RenderColorTarget；场景支持一个
postprocess 技术、Draw=Buffer、目标／清除设置、一次 ScriptExternal=Color、有限循环及
私有 int LoopGetIndex。共享循环索引、Stencil 清除、全局技术表达式、preprocess、多次捕获、顶点纹理和
任意 Pass 脚本继续明确失败。捕获目前要求单个、与当前轮次同尺寸的颜色目标。

## 分配和可见性

分配按子集覆盖、整对象覆盖、当前目标的有序 DefaultEffect、普通自动查找的顺序处理。
自动查找消费宿主已加载的效果清单：先同目录同名 `.fx`，再 `name[effect.fx].pmx` 命名，
最后是 EMM Default。此层不探测文件、不修改进程 cwd；文件发现／加载由调用方完成。
缺失的显式路径或过期效果 ID 会使准备失败，不能静默替换成其他效果。

显式 `none` 使用宿主材质并抑制自动查找；`hide` 跳过对象；`main_default` 回到主场景的
当前材质分配。DefaultEffect 只比较对象的文件名及扩展名，按声明顺序取首个匹配；`self`
是大小写敏感的特殊名称，其他模式保留原版大小写不敏感的 `*`、`?` 和反斜杠转义规则。
没有匹配项时使用宿主绘制，不能擅自隐藏对象。

主场景的子集 show 覆盖整对象 show。离屏目标有独立 show 记录；普通对象可以在主场景
隐藏而仍参与离屏绘制，场景载体默认不参与离屏轮次。显式 show 可以覆盖这项默认。
`main_default` 没有独立 show 时继承主场景可见性。宿主通过 `sceneCarrier` 提供载体身份。

## `.emm` 范围与路径

`ParseEmm` 接收宿主已解码的文本；兼容层可先解码 CP932，新宿主可传 UTF-8。原生文本
语法不接收 BOM。支持 Version 1..3、Info／Object／Effect 和 Effect@TargetName(n) 分区；
Default 为主场景自动查找的最后候选，不是强制覆盖全部对象。支持 PmdN／AcsN、子集路径
及 show-only 行；越界子集行按旧宿主规则忽略。

对象记录按类型与路径消耗匹配：先全路径／相对尾路径，再文件名兜底；序号不直接当作当前
宿主索引。路径统一分隔符并作词法规范化，保留旧 Windows 规则的 ASCII 大小写不敏感身份。
实际文件加载仍由 P5.1／宿主处理；跨平台文件系统上的同名大小写冲突不在这里猜测解决。
相对效果路径以 `.emm` 目录为基准；DefaultEffect 路径以声明效果的目录为基准。

命名分区通过分区名选择具体 OFFSCREENRENDERTARGET，不限制所有者只能有一个目标。
Owner 支持 `PmdN[subset]@ParentTarget(n)` 形式，先解析父分区，再解析继承的子效果；
Owner 本身不向主场景添加效果。缺失对象／路径／目标、循环 Owner、重复键／分区、未知
格式或超预算均原子失败，保留旧分配表。这一阶段不提供 EMD 或新增 PMM 私有块。

## 资源、顺序和提交

`TargetRegistry::Reconcile` 接受完整存活清单和视口／设备修订。共享池身份由名称、
不区分大小写的语义和精确 FX 纹理类型构成；`texture` 与 `texture2D` 不能合并。
无创建注解的 shared 声明是读者，需要存活发布者；各效果按加载顺序发布，最后一个发布者
获胜。旧发布者仍存活时保留其自有分配；同身份发布者的尺寸／格式／mip 契约必须相容。
私有同名目标永不合并。

支持 RGBA8／RGBA16F 颜色目标及 D24S8 深度附件，固定尺寸或 ViewportRatio。沿用已有
有限资源配置：自动生成 mip 只用于显式 Miplevels=0 的 RGBA8 离屏目标；其他目标保持单层。
未知格式、部分 mip 链、相对尺寸与固定尺寸混用及不支持的采样方式明确失败。浮点目标的
GPU 能力与回读资格仍由 P5.4 后端检查，不能由本批 CPU 格式描述代替。

分配 ID 单调增长；效果源修订、卸载重载、尺寸或设备变化使相应 lease 失效。未变化的清单
保留身份。每帧保留注册表版本，准备后发生重建或分配变化，`Ready`／`Commit` 拒绝旧计划。
宿主须在加载／卸载／重建时协调更新完整清单，不允许复用旧 ID／修订冒充新实例。

离屏目标按旧发现顺序建图、逆序执行，保证后代先完成；每个目标在整轮结束后生成 mip。
同轮场景载体按显示顺序逆序进入，ScriptExternal 递归执行内层，再按原顺序恢复；同显示
顺序保留 accessory 优先规则。深度／工作量及循环预算明确限制，循环依赖不能提交。

所有清除、附件、采样和 mip 操作先经共用 RenderPassState 检查，拒绝反馈、未初始化读取、
未生成 mip 读取及尺寸不匹配。每次材质／Buffer 绘制保留独立参数快照；失败或放弃准备
不会提交循环索引和共享池。真实执行方在成功提交整帧后调用 Commit，失败则放弃并使用
同帧回退；计划本身不更新动画或物理。跨帧纹理历史尚未在本批引入。

报告包含效果修订、共享资源别名／代次、实际附件描述、对象／子集、技术／Pass 索引、
附件加载／存储、读取和 mip 顺序，以及带类型／维度的参数原始 32 位组件和字符串。
报告是准备时的快照，后续资源重建不能更改旧报告。

## 验证入口

公开 CC0 夹具见 [`assets/effects/schedule`](../assets/effects/schedule/README.md)：
材质 → ChildMap → ParentMap → 读者材质 → 内层场景 → 外层两次 Buffer。
测试从原始 FX 经 P5.1 预处理生成定义，独立断言顺序，并实际重放 Null 资源操作。

```sh
cmake -S cmake/portable-mme -B out/build-mme -DMDS_DXC=/absolute/path/to/dxc
cmake --build out/build-mme --config Release --target mme_schedule_test mme_assignment_test mme_targets_test
ctest --test-dir out/build-mme -C Release -R '^mme_(schedule|assignment|targets)$' --output-on-failure
```

`mme_assignment` 覆盖技术有效性、三种纹理条件、空技术、空 Subset、范围溢出、通配符、
可见性、自动查找和 EMM 的显式覆盖／嵌套 Owner／失败原子性。`mme_targets` 覆盖发布顺序、
格式尺寸冲突、类型身份、私有目标、源修订、卸载重载及设备／视口变化。`mme_schedule`
覆盖两级场景、子效果、mip、循环快照、无早期写回、非法反馈和父目标未初始化读取。
Windows 另由独立 D3DX 编译器读取所有新夹具的元数据／脚本，并由隐藏 NULLREF 参数
参考验证共享纹理的类型／语义身份；不进行绘制或 Present。
