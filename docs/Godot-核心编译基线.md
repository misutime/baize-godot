# Godot 核心编译基线(EditorNative 共享库)

> 目的:固话 EditorNative.dll 的**标准构建链路**,为后续"精简 Godot 核心编译(只编所需功能)"提供基线。
> 关联:EditorNative.dll 是 EditorHost(宿主)加载的引擎侧 C ABI 动态库,承载场景渲染。
> 位置:baize-godot fork(`feature/object-components-engine`)

---

## 1. 为什么需要这份文档

- 本轮遇到的问题:**手动跑 pro 构建(mono 开启)会刷新 `register_module_types.gen.obj` 为"引用 mono 符号"版本,
  而 EditorNative.dll 是无 mono 共享库 → 链接 LNK2019 全套失败**,一次误构建可破坏整个产物。
- 未来要**经常改引擎代码**(嵌入子窗、Z-order、渲染链路),必须有一套**不会漂移**的一键重建流程。
- 后续还要**精简 Godot**:只编核心/所需模块,去掉 C#/mono、编辑器 UI、未用驱动,缩小链接体积与编译时间。

---

## 2. 标准构建命令(必须固定使用,勿混用其它 preset)

```bash
# ① 共享库构建(整个引擎以 .dll 形式产物)—— nomono 是 EditorNative 固定形态
scons platform=windows target=editor \
      library_type=shared_library \
      module_mono_enabled=no \
      dev_build=no \
      d3d12=no accesskit=no angle=no \
      debug_symbols=no

# ② 重链 EditorNative.dll(提取共享库 link 命令 → 换 def/去掉 exe 项 → 链 wrapper)
powershell -ExecutionPolicy Bypass -File .tmp/editor-native/s1-link-wrapper.ps1
```

- **等效产物**:`bin/godot.windows.editor.x86_64.dll`(全引擎共享库)+ `.tmp/editor-native/EditorNative.dll`(瘦身导出面,仅 engine_create/engine_destroy)。
- **EDITOR_NATIVE_DLL 宏**:已固化进 `SConstruct`(library_type=shared_library 时自动注入),驱动:
  - `display_server_windows.cpp`:嵌入窗口 = 真 `WS_CHILD|WS_CLIPSIBLINGS`(创建即子窗)
  - `os_windows.cpp`:GPU 符号/NV 优化不导出,`get_executable_path` 用模块句柄
  - `libgodot.h`:`LIBGODOT_API` 置空(libgodot_* 仅 DLL 内部)

---

## 3. 为什么 nomono(不编 C#/mono)

| 依据 | 说明 |
|---|---|
| EditorHost 是 .NET 进程 | Godot 引擎是**宿主加载的子 DLL**,不需要自己的 mono 运行时 |
| 导出面是纯 C ABI | `engine_create/engine_destroy` 只要 C 符号,不依赖 C#/GDExtension |
| 构建复杂度 | mono 开启会引入 CSharp 模块符号(initialize_mono_module 等),与瘦 DLL 链接冲突(本轮踩坑) |
| 精简方向 | 后续"只编核心"默认就应无 mono/gdscript |

> 若未来需要 GDScript 脚本执行(场景逻辑),可评估 `module_gdscript_enabled=yes`,但仍建议无 mono 优先。

---

## 4. 当前已测试验证的配置

| 参数 | 值 | 说明 |
|---|---|---|
| platform | windows | 目标平台 |
| target | editor | 编辑器版(tools=true;库模式也走 editor 标记) |
| library_type | shared_library | **触发 LIBGODOT_ENABLED + EDITOR_NATIVE_DLL** |
| module_mono_enabled | no | 关 C#,避免 mono 符号依赖 |
| module_gdscript_enabled | no | 关 GDScript(profile 默认,已验证) |
| dev_build | no | 非 dev(匹配 .editor.x86_64 而非 .editor.dev) |
| d3d12/accesskit/angle | no | 关闭未用渲染驱动/辅助(减少依赖) |
| debug_symbols | no | 不产 PDB,加快链接 |

**构建耗时**:全量 ~6-7 分钟 / 增量(单文件改动)~几十秒~2 分钟(scons 本身会增量)。

---

## 5. 后续「精简 Godot 核心编译」待办(按价值排序)

### 5.1 明确"需要 vs 不需要"的功能清单
- **需要(Engine Native 核心)**:core/scene/servers/rendering(Vulkan/RD)/physics(main 2D/3D)/audio 基础;
  Windows display/window/input/OS 层;main;libgodot(C ABI 入口)。
- **明确不需要**:
  - `module_mono`(C#)— 永远关
  - `module_gdscript`(脚本)— 场景逻辑走宿主侧载荷,暂不需要;如需可后开
  - 编辑器 UI(editor/ 与 toolchain):`target=editor` 本身带 tools;精简到 `target=template_release`?⚠️ 需验证库模式 + 无 tools 时 EditorNative 功能是否完整(编辑模式需要 tools 级能力)
  - 未用渲染驱动:GLES3/ANGLE/D3D12(仅 Vulkan)
  - 文件格式模块:fbx/gltf/svg/theora/vorbis 等(按场景资产需求裁剪,谨慎:编辑器要导入)
  - 网络/AI/nav 等按需

### 5.2 建立独立的精简构建 profile
在 `misc/customization/scons-profiles/` 新增 `windows_3d_editor_embed.py`:
```
module_mono_enabled = no
module_gdscript_enabled = no
library_type = shared_library
# 按 5.1 逐项裁剪 modules/features
```
> 模板可基于当前 windows_3d_pro.py 改。

### 5.3 验证精简后功能完整性(每砍一项做一次回归)
- 场景加载(BoxMesh/Camera/Light)
- 嵌入:WS_CHILD + Z-order + resize + attach/detach(EditorNative 全套)
- PrintWindow 能抓到渲染帧、宿主 tick 循环健康
- 退出码/熔断逻辑不受影响

### 5.4 产物瘦身目标(参考)
| 当前 | 精简后(目标) |
|---|---|
| EditorNative.dll ~221MB | <100MB(去 mono/gdscript/editor 链路明显下降) |
| 编译时间 6-7min 全量 | 目标 <3min 全量 |

### 5.5 其他
- 把 s1-link-wrapper.ps1 固化为 repo 内脚本(不在 .tmp),进 version control;
- 打印每次构建的 commit + DLL hash 到日志(证据链,供验收)。

---

## 6. 构建链路图

```
scons (library_type=shared_library, nomono, EDITOR_NATIVE_DLL#SConstruct自动)
  ├─ ─> bin/godot.windows.editor.x86_64.dll     (全引擎共享库,中间产物)
  └─ ─> bin/obj/.../display_server_windows.obj  (含 WS_CHILD fork)
                            │
s1-link-wrapper.ps1         │ 提取 link 命令 + /def:EditorNative.def
  ├─ 改写 /OUT/DEF → EditorNative.dll
  ├─ /WHOLEARCHIVE:module_mono 占位(无 mono 时无效,保留兼容)
  └─ 追加 wrapper.obj(EditorNative.cpp C ABI) → 链出 .tmp/editor-native/EditorNative.dll
                            │
EditorHost(.NET) ── LoadLibrary ──┬─ engine_create(argv: --wid/--path/...)
                                  ├─ AbiV1{tick,attach,detach,resize,shutdown,...}
                                  └─ engine_destroy
```

---

## 7. 踩坑备忘(勿再犯)

1. **勿用 `scons profile=windows_3d_pro.py`(mono=yes)重建引擎**——会刷新 obj 为 mono 引用→
   EditorNative(无 mono)链接 LNK2019 全套失败;必须用 §2 命令。
2. `dev_build=yes` 会产出 `.editor.dev.` 变体,与 link.rsp 的 `.editor.` 不匹配;
   统一 `dev_build=no`。
3. `d3d12=no` 是硬前提(缺 D3D12 SDK 依赖 scons 直接报错)。
4. 改 `EditorNative.cpp`(wrapper)重编**:只需 s1-link-wrapper(快)**;改引擎源码需**先 §2 的 scons 重建 obj 再 link**。
5. `register_module_types.gen.obj` 是 mono/gdscript 开关的敏感点——它的内容必须与 EditorNative 的
   mono 设置一致(nomono 构建会自动一致)。