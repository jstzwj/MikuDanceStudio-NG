# P5.2：参数实例、共享池和帧快照

`mikudancestudio_mme_runtime` 是无窗口、无 GPU、无 D3DX 对象的生产库。公共接口为
[`mme_runtime.hpp`](../include/mikudancestudio/mme_runtime.hpp)，定义来自 P5.1 的
`EffectDefinition`。这一步不表示 Qt 已能运行 MME；脚本／资源调度、GPU 装配和编辑器
接入分别属于 P5.3、P5.4、P5.5。纹理声明继续保留在定义中，纹理资源的共享与代次属于 P5.3。

## 默认值和实例

`EvaluateDefaults` 输出逻辑 32 位组件，矩阵为行主序，不包含后端常量缓冲填充。
支持 float、float2/3/4、float4x4、int、bool、string，以及最多 256 元素的数值数组。
单元素数组保留独立身份。支持有限的字面量、括号、构造器／转换、聚合、正负号、逻辑非、
四则运算、比较、逻辑与／或以及前置 `static const` 数值声明引用。字符串支持
`\n`、`\r`、`\t`、`\\`、`\"`。

原生差分确认：FX9 的编译期浮点常量折叠保留较高精度，最终转成 float32；不能按每一步
float32 舍入。静态常量引用也保留折叠精度。普通 `const` 是着色器限定符，仍然是宿主可写
的实例参数；`static` 不向实例暴露，只有 `static const` 可被后续默认表达式引用。

未知函数、运行时／前向引用、非有限值、整数溢出、未支持运算、形状错误和超预算明确失败；
不以零代替。默认表达式上限 64 KiB，递归深度 64，单变量组件上限 4096，声明上限 256。
有符号 32 位以外的整数、八进制字面量、字符串数组、部分数组初始化和完整 HLSL 常量语言
尚未支持。逻辑运算会校验两边表达式；不承诺跳过非法右侧的短路求值。
失败保留整个旧输出，原因包含参数名。

`EffectInstance::Create` 完成所有初始化后才发布共享槽；克隆共享不可变定义，复制私有值。
`Read` 返回副本，`Write` 要求精确类型／维度／数组身份并拒绝非有限值和非法 bool。
调用方在一个宿主线程串行操作池和实例；公共类不提供并发写入锁。

## 共享池身份与写入顺序

独立 D3DX NULLREF 效果对象确认以下规则：

- 身份为名称、语义、数值／字符串类型、行列、数组长度和是否数组；语义忽略大小写，名称区分大小写。
- 第一个仍存活的共享槽初始化获胜。后加载效果的默认值不覆盖它；不同语义或形状可以并存。
- 克隆继续引用共享槽，私有值独立。最后一个实例释放后，重新加载使用新的默认值；清理过期键防止热重载持续积累。
- 显式宿主 `Write` 立即可见，对应原生 `SetValue` 阶段；不是把所有宿主写入强行移到帧末。

## 标准语义与控制对象

复用原有 `MmeStandardClassify`、`MmeStandardEvaluate`、`MmeAdvanceFrameClock` 和
`ModelNameRegistry`。矩阵运算使用已验收的 CPU 数学库：行向量约定、逆／转置、
`W*(V*P)` 结合顺序保持不变；逆矩阵使用 CPU 数学契约的误差标准，不承诺 D3DX 逐位相同。
材质／灯光／相机／视口／TIME 系列和已有命名参数沿用原分类器；不认识的声明明确报告。

宿主在 `SemanticContext` 提供稳定对象 ID、名称、顺序、可见性、世界矩阵，以及已经冻结的
骨骼、形变和附件伪节点值。支持重名选择、`(self)`、`(OffscreenOwner)`、
`(AttachedModel)`、`(AttachedBone)`。对象消失会在本次准备重新解析，不能保留悬空指针。
不存在对象与存在对象但缺少 Item 的默认值不同；缺失附件全对象缩放为 10，模型为 1，
缺失 Item 标量为 0。Windows 原 SAS 适配器也调用同一个 `EvaluateControl` 数值转换，
对象查询和原生宿主读取仍留在适配器。

## 准备、提交和失败恢复

`EffectFrame::Begin` 以单调帧 ID 采样一次时钟。同 ID 的失败重试复用首次时钟，即使传入
新的 tick／播放状态；成功提交后的旧 ID 被拒绝。暂停、倒退 seek、编辑／播放切换和 tick
回绕沿用原时钟规则。库不驱动动画或物理更新。

每次 `Prepare` 先完整求值，再冻结该次绘制的独立 `ParameterSnapshot`。同一事务中后续
分配能读取之前暂存的共享写入；已生成的绘制快照保持原值。`Commit` 一次性写回共享池和
提交时钟，销毁事务即放弃写回。参数池被宿主写入／加载／克隆改变、实例被卸载、时钟被
其他事务提交或后续帧替换时，旧准备会被拒绝。失败不替换旧快照，也不部分发布语义写入。

提交边界供 P5.3／P5.5 的有序执行计划使用：原生 `MmeUpdateControlObjects` 每次分配执行，
直接宿主参数修改立即生效，准备中的分配顺序则保留在事务内。循环索引、屏幕历史和资源
写回尚未接入；这些属于后续调度事务，不能据此声称完整脚本已兼容。

## 验证入口

```text
mds_effect_cli initialize assets/effects/frontend/Runtime.fx --dxc PATH --output runtime.json
ctest --test-dir out/build-mme -C Release -L p5 --output-on-failure
```

CLI 报告 `status=initialized`、逻辑组件 `words` 和字符串，始终
`executionValidated=false`。`inspect` 继续只报告定义。默认值、池、控制对象与事务各有
可移植 CPU 门禁；Windows 独立参考使用动态加载的 D3DX9_43、NULLREF 和自有隐藏窗口，
不显示／激活窗口，也不提交绘制或 Present，不依赖工作桌面上的 GPU 显示设备。
三平台定向 CI [42168010475](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/42168010475)
通过 Windows 12、Linux 10、macOS 10 项门禁。下载产物中完整声明／默认组件值与逐绘制
帧快照一致；[版本化验收证据](evidence/p5-runtime-42168010475.json)保存组件值、哈希、
测试集合及产物身份。本次只使用一轮聚焦的 MME 工作流，没有重跑发行包。
