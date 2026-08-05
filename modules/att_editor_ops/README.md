# att_editor_ops — 编辑器领域能力面

> 本 fork 特有模块（`att_` 前缀 = 别于上游 Godot）。实施原则见
> 《doc/plans/实施原则-编辑器领域能力统一editor_ops.md》。

## 职责（一句话）

**所有编辑器领域操作与状态查询（读写皆算）的唯一事实源**——能力注册表 + 操作实现 + UI 树导出。
任何对外通道（CEF WebBridge、Node sidecar WS、未来 Node MCP）只做协议适配，不实现能力。

## 边界

| 进（本模块） | 留通道模块 |
|---|---|
| 编辑器领域操作/状态：场景/节点/属性/选中/undo/UI 语义树/主题状态 | 协议握手（`sidecar.hello`）、订阅协议 |
| 判据：能否被"当前通道之外"的调用方合理使用 | 通道专属渲染细节 |

读写一视同仁：查询（get_*）与操作（set_*）同等注册。

## 文件结构

| 文件 | 角色 |
|---|---|
| `registry.{h,cpp}` | 能力注册表（唯一事实源）：方法名/描述/JSON Schema（含 required）/handler 集中注册；`find`/`validate_args`/`methods` |
| `ops.{h,cpp}` | 操作实现：把引擎既有 API（EditorUndoRedoManager/EditorSelection/Node/Control）组合成语义操作，含类型转换/路径守卫/只读拒绝 |
| `ui_tree.{h,cpp}` | UI 语义树导出：遍历 EditorNode 的 Control 树 → role/name/state/children 结构化快照 |

**引擎仍是唯一执行者**：ops 只做组合/契约/防护，不重写引擎功能（见 `ops.cpp` 中各方法对 `EditorUndoRedoManager`/`EditorSelection` 等的调用）。

## 依赖

零依赖（`env.editor_build` 门控）。被消费方：`att_nodejs_sidecar`（分派）、`att_webview`（WebBridge 委托）。

## 扩展指南（新能力三步）

1. **实现**：`Ops` 加方法（undo 语义、类型转换、路径守卫、只读拒绝——沿用既有规范）；
2. **注册**：`Registry::register_method`（方法名/描述/schema/handler）——唯一事实源；
3. **通道自动获得**：sidecar 透传 + WebBridge 委托已就位；返回形状差异（如 `create_node` 三元组 → WebUI 裸 instance_id）在**通道适配层**处理，不污染注册表。

**禁止**：通道侧新增 if/else 能力实现（一律 `Registry::find` + `validate_args` + handler）。

## 决策记录

- 2026-08-05：原 `modules/ai` 拆分，能力面独立为 `modules/editor_ops`（后加 `att_` 前缀）；
  类名去 `Semantic*` 前缀（`SemanticRegistry`→`Registry` 等），放弃 AI 历史命名。
- 线程：全部 handler 主线程（编辑器帧泵）；返回统一 `{ ok, result } / { ok:false, error:{code,message} }`。

## 相关文档

- 《实施原则-编辑器领域能力统一editor_ops.md》（边界判据/落地规则）
- 《实施记录-AI-FIRST-P1-P2-语义接口与MCP.md》（能力面起源/排坑知识，历史归档）
