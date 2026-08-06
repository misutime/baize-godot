# 实施原则——编辑器领域能力统一归集 `editor_ops`

> **时间**：2026-08-05（确立于模块架构拆分同日）
> **范围**：仓库级实施原则——所有编辑器领域操作与状态查询统一注册在 `modules/att_editor_ops`，
> 任何对外通道只做协议适配，不实现能力。
> **衔接**：《Godot编辑器UI重构方案-TS路线-NodeSidecar落地-方案.md》（通道架构）、
> 《实施记录-AI-FIRST-P1-P2-语义接口与MCP.md》（漂移教训/排坑）、
> 《实施记录-NodeSidecar-S1-通道落地.md》（模块拆分）。
> **状态**：已确立（2026-08-05）；6 个未委托 WebBridge 方法已迁移完成（2026-08-05），能力合流闭环。

---

## 1. 原则（一句话）

**所有编辑器领域操作与状态查询（读写皆算）统一注册在 `modules/att_editor_ops` 的
`Registry`（能力注册表，唯一事实源）；任何对外通道——CEF WebBridge、Node sidecar
WS/JSON-RPC、未来 Node MCP server 等——只做协议适配（信封/req_id/返回形状），不实现能力。**

```
modules/att_editor_ops（能力唯一事实源：registry + ops + ui_tree）
      ▲                ▲                ▲
      │ 委托            │ 透传            │ 未来
   WebBridge        sidecar_server    Node MCP
   （CEF 通道）      （WS 通道）         （协议通道）
```

## 2. 动机（为什么）

| # | 理由 | 证据 |
|---|---|---|
| 1 | **消灭双份实现漂移** | 历史教训：WebBridge 与 Registry 双实现导致 `scene.create_node` 默认名分叉（《实施记录-AI-FIRST》§5）；shifu 审查 P1-7 判定"三端语义一致"不成立，S1 据此委托 4 个重叠方法 |
| 2 | **新通道零成本** | 能力已注册 → 新协议只需一个薄适配层（S1 的 sidecar_server 透传零改动获得全部能力） |
| 3 | **能力清单可审计** | `Registry::methods()` 全量枚举编辑器对外能力——盘点/文档/权限审计均基于它 |
| 4 | **校验/错误码统一** | 参数 schema（JSON Schema + required）、内部错误码只写一次，通道不重复实现 |

## 3. 边界判据（什么进、什么留）

**进 `editor_ops` 的判据：该操作/查询能否被"当前通道之外的调用方"合理使用。**
能 → 作为编辑器领域能力注册；不能 → 属通道专属，留在对应通道模块。

| 类别 | 例子 | 归属 |
|---|---|---|
| 编辑器领域操作（写） | 创建/选中节点、设属性、undo/redo、激活 UI 控件、设文本 | ✅ `editor_ops` |
| 编辑器领域状态（读） | 场景节点计数、位置读写、UI 语义树、编辑器状态、UI 主题（字体/缩放） | ✅ `editor_ops` |
| 通道协议握手 | `sidecar.hello`/`health`/`subscribe`、WS 订阅协议 | ❌ 通道模块（`nodejs_sidecar`）——非编辑器能力 |
| 通道专属渲染细节 | WebUI 渲染管道内部同步（若存在仅当前通道使用） | ❌ 通道模块（`webview`） |

**读写一视同仁**：读能力（get_*）与写能力同等注册——查询也是对外能力，同样可能被多通道消费（AI 排查 UI、脚本读状态）。

## 4. 现状盘点（2026-08-05）

| 通道 | 现状 | 与原则的差距 |
|---|---|---|
| `editor_ops` | 11 个方法注册（ui.* 4 + editor.* 5 + scene.* 2） | — |
| Node sidecar（`nodejs_sidecar`） | Registry 全量透传（S1），方法面完整 | ✅ 已符合 |
| WebBridge（`webview`） | 10 个桥方法：4 个已委托 Registry（create_node/get_node_count/undo/redo），**6 个未委托**（`scene.set/get_node_position` + `editor.get_ui_font_size/scale/font/font_bold`） | ⚠️ 半统一（在途待办，§5） |
| ~~ai_bridge~~ | 已删除（2026-08-05 架构拆分） | — |

## 5. 落地规则（新增能力的标准流程）

1. **实现**：进 `Ops`（undo 语义、类型转换、路径守卫、只读拒绝——沿用既有规范）；
2. **注册**：`Registry::register_method`（方法名/描述/JSON Schema 含 required/handler）——唯一事实源；
3. **通道自动获得**：WebBridge 委托 + sidecar 透传已就位；返回形状差异（如 `create_node` 三元组 → WebUI 裸 instance_id）在**通道适配层**处理，不污染注册表；
4. **禁止**：通道侧新增 if/else 能力实现（一律走 `Registry::find` + `validate_args` + handler）。

### 在途待办：WebBridge 剩余 6 方法迁移

| 方法 | 性质 | 处置 |
|---|---|---|
| `scene.set_node_position` / `get_node_position` | 场景操作（写+读，undo 语义） | 补进 `editor_ops`（Ops 实现 + 注册）→ WebBridge 委托 |
| `editor.get_ui_font_size` / `get_ui_scale` / `get_ui_font` / `get_ui_font_bold` | 编辑器 UI 主题状态（读） | 补进 `editor_ops`（读能力）→ WebBridge 委托；或明确为 host 能力留 `webview`（须显式标注，不悬置） |

完成后 WebBridge 收敛为**纯协议适配层**（req_id 包装 + 返回形状适配），不再含任何能力实现。

## 6. 延伸：事件面（S2 起适用）

- **事件声明**（`editor.selection_changed` / `scene_changed` / `undo_stack_changed` 等名称、payload schema）归 `editor_ops`（与 `@baize/rpc` 类型对齐，防双端漂移）；
- **事件源实现**（帧轮询 diff、fan-out 注册表）留在 `webview`（事件源现位于 `web_bridge.cpp`）或按 S2 裁决；
- **订阅协议**（subscribe/unsubscribe、通知方向白名单）属通道层（`nodejs_sidecar`/`webview`）。

## 7. 决策记录

- **2026-08-05**：原 `modules/ai` 架构拆分——能力面 → `modules/att_editor_ops`（零依赖），sidecar 通道 → `modules/att_nodejs_sidecar`，`ai_bridge` 删除；同日确立本原则（编辑器领域能力统一归集 editor_ops）。
- 本原则的裁决边界：**能力实现只写一份、注册一次；通道只做协议适配**。新增能力/新增通道时以此为准，不另行讨论。
