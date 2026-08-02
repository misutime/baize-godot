# Godot 编辑器 UI 重构（TS 路线）——第一步落地计划：E0 CEF 验证

> **定位**：本文件是《Godot编辑器UI重构方案-TS-CEF嵌入与NodeSidecar-设计.md》的第一步落地执行计划（2026-08-02 编写）。回答"第一步是不是应该集成 CEF"：**是，但以 E0 GDExtension 验证形式，不是引擎模块融合形式**。本文件只规划第一步（E0 四关验证 + 并行 TS 脚手架），不扩张到后续阶段。
>
> **证据标注**：仓库事实与外部事实标注来源；推断标 `[INFERENCE]`。引用《设计》= 上文的 TS-CEF 方案设计文档。

---

## 1. 结论

- **第一步 = CEF 集成，但分两档**：
  1. **E0 验证档（即长期形态）**：godot-cef GDExtension（预编译二进制直接 drop 进 addons），目标 = 通过《设计》§7 的四条验证点。成本半天到两天，不动引擎构建链。**2026-08-02 暂定裁决：E0 形态即长期形态（保持 Rust/GDExtension，不做 C++ 模块移植）**，E0 同时验证"GDExtension + Rust 原生组件"通用路线（§9）。
  2. **C++ 模块融合档（备选）**：`modules/webview/` C++ 移植（原《设计》§3.3 裁决，2026-08-02 暂定修订降级为备选），仅在 GDExtension 暴露硬限制时启用。
- **并行轨道**：TS 侧脚手架（pnpm workspace + React shell + sidecar 骨架 + 共享类型），零引擎风险，无论 E0 成败都复用。
- 拒绝的替代方案："第一步直接引擎模块融合"——把全部未知风险压到最贵的路径上，不合算（§3）。

## 2. 为什么 CEF 先行：风险排序论证

| 论证 | 内容 | 依据 |
|---|---|---|
| 命门识别 | 整个方案的可行性命门是"OSR Chromium 嵌入 Godot Control 能否丝滑"，《设计》§5.1 自标为**最高风险区**；此路不通则 React/Node/JSON-RPC 全部失去载体 | 《设计》§5.1 |
| 其余技术成熟度 | React 19 + Vite 8 + dockview + Node sidecar + JSON-RPC 均为成熟技术，失败成本低、可替换 | [INFERENCE] |
| 文档自身顺序 | §0 实施顺序：CEF Dock Panel → React 页面 → 双向通信 → Selection API → Inspector；§7 E0 四条验证点全部要求 CEF 先跑起来 | 《设计》§0/§7 |
| 结论 | 先做难而关键的事；E0 四关 = 为命门风险设计的廉价退火阀 | — |

## 3. "集成 CEF"的两档拆分

| 维度 | E0：GDExtension 验证 | 模块融合：modules/webview/ |
|---|---|---|
| 形态 | godot-cef 预编译 release 按官方 quick start drop 进 addons（`CefTexture` 节点直接可用） | C++ 移植 godot-cef（Rust 蓝本）进引擎 `modules/webview/` |
| 成本 | 半天到两天；不动引擎构建链 | CEF 二进制 100MB+ 进构建链；CEF 消息泵与 Godot 主循环集成；221MB dev 编辑器全量重编 |
| 目的 | 验证四个未知风险点（OSR 嵌入/桥语义/IME/跨平台）+ **通用"GDExtension + Rust 原生组件"路线可行性** | （备选）仅在 GDExtension 暴露硬限制时启用 |
| 文档依据 | 《设计》§3.3："E0 可用其 GDExtension 版本快速验证" | 《设计》§3.2/§3.3 |
| 失败代价 | 一周 | 一个月 |

**关键认知（2026-08-02 更新）**：E0 的 godot-cef（Rust GDExtension）**即长期形态**，验证结论直接迁移；C++ 模块融合仅作备选（E0 暴露 GDExtension 硬限制时），届时才需重新评估移植成本。

## 4. 本 Fork 落地事实（已核实，2026-08-02）

| 事实 | 内容 | 来源 |
|---|---|---|
| Fork 版本 | `baize-godot` = **Godot 4.8.0-dev**，master 分支，工作区干净 | `version.py`、`git status` |
| 无既有 CEF 代码 | `modules/` 无 webview/cef 模块；全仓库 grep `cef|chromium` 命中均为 `interface` 子串误报 | 仓库 grep |
| 渲染驱动 | Fork 同时具备 Vulkan / D3D12 / Metal 驱动，渲染驱动裁决未定 | `drivers/` 目录 |
| 构建产物 | `bin/godot.windows.editor.dev.x86_64.exe`（221MB dev 构建）已存在；Taskfile 提供 dev/pro 预设 | `bin/`、`Taskfile.yml` |
| godot-cef 目标版本 | **Godot 4.5+**（GDExtension）；GPU 加速 OSR：Win D3D12（需 4.6 beta2+）/ Win Vulkan（hooking，x86_64 only）/ mac Metal / 各平台软件渲染回退 | godot-cef README |
| godot-cef 分发 | 预编译 release 二进制，addon 形式安装，官方 quick start 即 drop-in | godot-cef README |

**由此得出的落地含义**：

1. 本机 Win x64 + 已有 dev 构建 → **Windows 是 E0 第一腿的自然起点**；godot-cef 的 x86_64-only 限制在 Win 无影响。
2. Fork 是 4.8-dev，godot-cef 二进制面向 4.5+——GDExtension 接口漂移会导致**加载即失败**，因此 **Step 0 必须是加载冒烟测试**，其结果决定后续一切（§6）。
3. mac 腿需核实 godot-cef 对 Apple Silicon arm64 的支持（《设计》附录未覆盖此点）。
4. 渲染驱动裁决（Vulkan vs D3D12，Win 侧）不阻塞 E0——软件渲染先行，与《设计》§3.3 一致。

## 5. 第一步分解（Step 0–4 + Gate）

```
Step 0 (半天): godot-cef 预编译 GDExtension 在本 fork 加载冒烟测试
               ── 最便宜的风险门：4.8-dev 与 godot-cef 二进制的兼容性
Step 1 (并行两条轨道):
  A. TS 脚手架: pnpm workspace + ui/ React19+Vite8 shell (dockview) + runtime/ sidecar 骨架 + packages/rpc
  B. E0: CEF OSR WebPanel 跑起来；页面用 A 的真实 React shell，而非测试 HTML
Step 2: 桥 + UndoRedo 验证（React 按钮 → 引擎操作 → Undo 入栈）    ← E0 验证点 2
Step 3: IME 中文输入实测（Win）                                     ← E0 验证点 3
Step 4: mac 复验                                                   ← E0 验证点 4
Gate: 四关全过 → 确认保持 Rust/GDExtension 形态（webview 长期化 + 推广到新原生组件，§9）
      任一关失败 → 分级处理：GDExtension 硬限制 → 评估 C++ 模块融合备选；
                   集成质量/性能不达标 → 重新评估 windowed / Ultralight / C# 路线
```

**轨道 A 与 B 的关系**：A 不依赖 CEF、无论 E0 成败都复用（UI 是 UI），所以并行不亏；A 还让 E0 的桥测试更接近真实形态（真实 React 页面而非 test.html）。

### E0 四条验证点（引用《设计》§7，验收标准细化）

**进度（2026-08-02，Windows）**：Step 0–3 已完成，验证点 1/2/3 通过；验证点 4（mac 复验）待做。

| # | 验证点 | E0 验收标准（本计划细化） | 状态（2026-08-02） |
|---|---|---|---|
| 1 | CEF OSR（软件路径）嵌入 Godot Control | `CefTexture` 在 Control 容器内渲染 React 页面；窗口 resize / DPI 变化时纹理跟随，无明显撕裂 | ✅ **通过**：自编译 addon 加载（godot-rust 初始化 `API v4.5 → runtime v4.8.dev`）+ google 页面渲染、点击可交互 |
| 2 | C++↔JS 双向通信 + 引擎操作经桥执行 + UndoRedo 入栈 | React 侧按钮触发引擎操作（如创建节点/设置属性），操作经桥执行且**进入 UndoRedo 栈**，可撤销——验证桥 API 面 = 编辑操作语义，非全 API 反射 | ✅ **通过**：create → `undo_stack:1` → children 2；undo → `undone` → children 1；双向消息均到达 |
| 3 | IME 中文输入可用 | React 文本输入框在 Win IME 下 composition 序列完整（选词/候选/确认），中英文混输无吞字 | ✅ **通过**：Win 拼音输入法中文输入正常 |
| 4 | Win/mac 双平台各跑一遍 | 1–3 在双平台复验；mac 确认 arm64 支持或记录 x86_64 限制 | ⏳ 待 mac 复验（godot-cef 构建链已有 universal 产物，见 §7） |

## 6. 风险清单（E0 最可能翻车的点）

| 风险 | 说明 | 暴露时机 | 对策 |
|---|---|---|---|
| 4.8-dev 与 godot-cef 二进制的 GDExtension 接口兼容性 | Rust 预编译二进制，接口漂移 → 加载即失败 | **Step 0 第一天** | ✅ **已实测通过**（2026-08-02，自编译 api-4-5 版）；预编译 release 同 API 面，结论可迁移 [INFERENCE] |
| CEF 消息泵接入 Godot 主循环时序 | 《设计》§5.4 待核实项 | Step 1 当天 | 参照 godot-cef `cef_init.rs`/`browser_process.rs`；独立线程 + 线程安全队列 |
| IME 中文输入（Win） | 《设计》§5.2 明列 E0 必测 | Step 3 | ✅ **已通过**（Win 拼音）；mac IME 待复验 |
| 软件渲染纹理更新成本 | 每次 OnPaint 全量拷贝+上传 | Step 1（低频面板可接受） | 只迁低频面板；高频面板留 C++ 或等 GPU 版（《设计》§5.3/§5.6） |
| gdext 版本跟进 | crates.io 发布滞后 master；api-4-8 支持要等 gdext 跟进（4.8-dev 属非稳定版本，gdext 明确不背书 fork） | E0 后持续 | 长期锁定 gdext 版本；必要时自维护绑定 fork；E0 先验 api-4-5 二进制加载 |
| godot-cef 单人维护 | 社区单人维护（dsh0416），版本跟进节奏不可控 | 长期 | 策略 = 参考 + 保持扩展形态，不依赖其持续更新；上游停更则自维护或评估 C++ 移植 |

## 7. 待核实项与已核实项（2026-08-02 更新）

**已核实（来源：godot-cef `mise.toml` / `.github/workflows/build.yml`）**：

- godot-cef 当前 pin **CEF 148.0.10**（= Chromium 148，2026 年中稳定分支）——**版本不旧**，CEF API 面可放心作为参考蓝本；
- godot-cef 构建工作流产出 mac **universal（arm64 + x64）** 产物——E0 的 mac 腿可在 Apple Silicon 上跑；
- **CEF 版本策略裁决**：E0 用 godot-cef 自带 CEF 148 验证（软件渲染路径版本敏感度低，结论可迁移）；模块融合时取当时**最新稳定分支** CEF distribution build 并**固定精确版本号**（与《设计》§5.5 锁定策略一致，不继承 godot-cef 的版本锁），融合时锁一次、之后升级走评审不追新。

**已核实（2026-08-02 E0 实测，Windows）**：

- **4.8-dev 加载兼容性通过**：自编译 addon（api-4-5，CEF 148.0.10）在 4.8-dev 全链路可用——godot-rust 初始化 `API v4.5.stable.official, runtime v4.8.dev.custom_build`、`CefTexture registered: true`、渲染/点击/桥/IME 均正常；预编译 release 同 API 面，结论可迁移 [INFERENCE]；
- **fork Windows 默认渲染器 = D3D12**（`project.godot` 的 `rendering_device/driver.windows="d3d12"`）——V2 GPU 加速路径走 **D3D12 分支**（需 4.6 beta2+，fork 是 4.8 ✓），无需 Vulkan hooking 路径；
- **godot-cef res:// URL 须含目录组件**：`res://ui/bridge.html` 可用；`res://bridge.html` 会被 CEF 规范化成目录（追加 `/index.html`）——页面加载的实操约束；
- **`enable_accelerated_osr` 未测**：V1 软件路径已跑通；GPU 加速（D3D12）留待 V2 或快速开关验证。

**待核实**：

- gdext 对 Godot 4.8（dev）的绑定跟进状态（api-4-8 feature 可用性）——长期保持 GDExtension 形态的前提
- 本机 Node/pnpm 工具链现状（Node 26 是否为已装版本；《设计》§1.2 裁决 Node 26 LTS）
- E0 验证点 4：mac 复验（加载/渲染/桥/IME 四关在 mac 上重跑）

## 8. 与本计划相关的既有决策（引用，不重复论证）

- 渲染 = OSR；通信 = 三者两两直连——《设计》§0 已裁决
- ~~godot-cef 融合进引擎（模块，非插件）~~——《设计》§0 原裁决，**2026-08-02 暂定修订为保持 Rust/GDExtension 形态**（§9）
- V1 软件渲染先行、V2 GPU 加速——《设计》§3.3 已裁决
- Inspector 是第一个迁移面板；实施顺序 CEF Dock Panel → React 页面 → 双向通信 → Selection API → Inspector——《设计》§0/§5.7

## 9. Rust 融合路线（暂定裁决，2026-08-02，待 E0 确认）

**暂定**：新原生组件一律优先 **GDExtension + Rust**；引擎核心（Variant/Object/SceneTree/ClassDB/渲染服务器）**不替换**。

| 项 | 裁决 | 依据 |
|---|---|---|
| webview 集成形态 | 保持 godot-cef 原样（Rust GDExtension），**不做 C++ 模块移植** | godot-cef 已从 GDExtension 侧完成 GPU 加速 OSR/IME/DND/DevTools（《设计》§3.2，已核实）；免引擎重编循环；保持 Rust 代码库；可跟踪上游修复；GDExtension API 自 4.1 起向后兼容（gdext book，已核实） |
| 新原生组件 | GDExtension + Rust；sidecar 原生模块 = napi-rs | 《设计》§1.2 已定 napi-rs；gdext 成熟（5k★，编辑器插件/工具可用，已核实） |
| 引擎核心 | 不替换 | 本 fork 持续 merge godotengine:master，核心重写即杀死上游跟踪；核心热路径 GPU 绑定，Rust 无性能收益；Object/Variant/ClassDB 边界不清晰，拆不动 [INFERENCE] |
| E0 双重角色 | 同时验证 webview 风险点 + 通用 GDExtension+Rust 路线 | 四关中前三关（OSR 嵌入/桥/IME）即路线风险点 |

**硬性约束**（每个 Rust 组件必须满足）：

- 边界 = C ABI / GDExtension；
- panic 跨 FFI 必须 `catch_unwind`（跨 FFI panic = UB）；
- 不跨 FFI 持有引擎对象引用——走 gdext 的 `GodotClass` 生命周期封装；
- gdext 明确不背书 fork——兼容风险自担，E0 冒烟测试为首道闸门。

**与 E0 的关系**：E0 四关全过 = 本路线首个可行样本确认，后续 Phase 2（新原生工具/管线）沿用同一路径；任一关因 GDExtension 形态本身失败 = 触发 C++ 模块融合备选评估。
