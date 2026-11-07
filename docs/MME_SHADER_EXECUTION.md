# P5.4：着色器布局与实际执行

本批把 P5.3 的 CPU／Null 调度接到真正的 RHI 绘制，Qt 编辑器接入仍属于 P5.5。
本地 Windows DX11、DX12、Vulkan 已完成离屏执行；三平台集中验收结果随后记录在本页。

## 生产边界

`mikudancestudio_mme_render` 依赖无 GPU 的 `mikudancestudio_mme_runtime` 和 RHI，
没有反向依赖。源码前端、参数池和调度 CLI 继续保持 P5.1～P5.3 的依赖限制。
新装配层不包含 Windows、COM、D3DX 或原生效果对象。

执行顺序为：

1. `LoadEffectDefinition` 冻结源码、include、宏和元数据。
2. `EffectCompiler::Compile` 从同一份定义生成现代 HLSL、常量布局和 SPIR-V，
   核对真实编译结果的绑定、类型、偏移、VS／PS 接口及 MRT 输出。
3. 将实际编译出的 `techniques` 有效性交给 `ScheduledFrame::Prepare`。
4. `AssembleRenderFrame` 合并冻结参数、宿主几何和资源引用，再完整验证依赖和绘制。
5. 宿主通过 `SceneRenderer` 准备、提交；提交成功后才 `ScheduledFrame::Commit`。

装配不分配 GPU 资源、不提交、不重新采样时钟或更新业务状态。准备失败保留原输出；
提交事务和旧画面处理在 P5.5 的宿主接入中完成，不能把编译通过当作帧执行通过。

## 布局与缓存

- FX 数值使用显式 `packoffset`；向量不跨 16 字节寄存器，数组与矩阵从寄存器起点开始。
  数组步长 16 字节，`float4x4` 为四个行向量、步长 64 字节；bool 以 uint32 传输。
- `static const` 留在着色器内；普通 `const` 保持 FX9 的宿主可写语义。字符串及
  `STANDARDSGLOBAL` 不进入数值常量块。每次绑定核对名称、逻辑类型、形状和范围。
- t0～t15／s0～s15 的顺序来自源定义，仅绑定可达采样器。上传纹理与渲染目标仍分别引用。
  默认将原生各向异性请求映射为已确认的可移植三线性档位；寻址与点／线性过滤保留。
  当前通用采样契约不能表达对多级纹理禁用 mip，遇到该组合明确拒绝。
- 缓存键包含全部冻结文件及 include 内容（含非活动分支）、编码、宏、预处理器身份、
  重载修订、前端／布局版本、编译策略、编译器身份／版本、后端策略和场景／材质分类。
  入口和布局由这份定义确定。成功与失败均缓存；清空、修订或内容变化后重试。
- 缓存同时限制条目数和总字节数。不可变 CPU 程序包含 HLSL 和 SPIR-V；原生对象仍由设备缓存，
  设备替换不会复用旧设备句柄。DX11／DX12 后端继续使用现有原生编译器，不在装配层查询 COM 反射。

## 执行中补齐的调度规则

- `ScriptExternal` 的 MRT 捕获把有序附件完整传给子场景和材质；无 MRT 的宿主绘制明确拒绝。
- `VIEWPORTPIXELSIZE` 保持 BeginScene／当前 offscreen 入口尺寸；中间目标缩放不重新定义该语义。
  API 半像素修正使用当前绘制目标尺寸。
- FX9 可以在较小颜色目标旁保留较大的深度面。显式关闭深度测试和写入的绘制不建立深度依赖；
  仍使用深度的 Pass 继续执行 RHI 的严格附件尺寸检查，不隐式创建另一个深度目标。
- 在最终装配中消除只有清空写入、没有绘制／采样／回读消费者的深度资源。Apple Silicon 不支持
  原生 D24S8，先前为这些无消费者的目标分配资源会导致整帧准备失败。消除死写入对所有后端生效；
  真正使用的 D24S8 保持原格式，设备不支持时明确失败，不替换为另一个深度格式。

## 可重复验收

公开夹具位于 `assets/effects/execution`，均为原创／从项目公开 CC0 夹具派生，无专用 NG 注解。

| 夹具 | 检查内容 |
|---|---|
| LayoutMaterial | 标量／向量混排、单元素及多元素数组、bool／int、非对称矩阵、可写 const、static const、十六个同时使用的纹理槽、Alpha |
| Material／Child／ReaderMaterial／OuterScene／InnerScene | 两个嵌套场景、DefaultEffect 子效果、跨效果共享目标、相对尺寸、显式非零 LOD、整轮完成后生成 mip、循环 Pass |
| LegacyMrtMaterial＋MrtScene | 双 RGBA16F 写入、分别采样、半／四分之一尺寸、HDR 合成、色调映射与最终复制 |

每种后端执行三种场景、64×48→80×64→64×48，替换设备、卸载重载，合计 27 次提交。
逐次读取原生 binary16 附件，逐分量绝对误差 ≤0.002；色调映射及已知常量图像误差 ≤1 字节。
最终原样复制（包含 Alpha 和边缘）及同帧重复提交继续逐字节比较。

原来的 `portable/LegacyMrtScene.fx` 保留不变。它在缩小 Pass 使用恰好位于 texel 边界的
点采样坐标，插值舍入会选择相邻 texel。新的确定数值参考把这些坐标移至 texel 内部；
不是修改采样器或放宽绑定、复制、初始化及生命周期检查。图像证据包含 RGBA8 原始数据／PPM
和原生 RGBA16F 数据，失败前也保存已读回的附件。

```sh
cmake -S cmake/portable-mme-render -B out/p54 -DMDS_DXC=/absolute/path/to/dxc
cmake --build out/p54 --config Release --target mme_shader_layout_test mme_render_gpu_test --parallel 3
ctest --test-dir out/p54 -C Release -L mme-execution --output-on-failure
```

Windows 的集中门禁使用 DX11／DX12 WARP，Linux 使用软件 Vulkan，macOS 使用原生 Metal。
本地 Windows Vulkan 另行执行硬件离屏对照。CI 仅手动启动 `MME shader execution`，
不构建 Qt、发行包或运行桌面交互，也不并入只编译 CPU 目标的 P5 门禁标签。

## 适用范围

沿用 P5.3 已声明的主颜色调度范围及有限脚本语义；不表示完整 FX9／Ray 兼容。
不支持的着色器、顶点纹理、资源类型、状态和技术保留明确诊断，技术选择只能使用真实有效性。
实际设备资源能力、分配和提交仍分别验证。Qt 视口、参数 UI、保存重开和导出闭环由 P5.5 验收。
