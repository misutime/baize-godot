# 实施记录——AI FIRST P1/P2：语义 UI 树与语义操作 + MCP HTTP server

> **时间**：2026-08-03（Win 实机；worktree `baize-godot-ai-first`，分支 `feature/ai-first`）
> **范围**：AI FIRST 方案的 P1（语义 UI 树 + 语义操作 + JSON-RPC 传输）与
> P2（MCP HTTP server + 能力面共享）
> **衔接**：方案《D:\MisuNotes\Godot游戏开发\面向未来的游戏编辑器\AI FIRST\》
> （README + 01 调研 + 02 方案）。基于 `64cf2388c1`（事件源提交）新建 worktree。

---

## 1. 目标与验收

**问题**：AI 开发/调试编辑器 UI 时靠「截图 + 像素估算 + 系统级鼠标模拟」定位节点，
不可靠（2026-08-03 事件源验证实证：同一 3dMain 节点被视觉模型三次报出三个坐标，
点击全部落空，最终人工点击）。

**P1 验收**：AI 经命令行/脚本读 `ui.get_tree` 精确拿到「3dMain selected=false」→
`editor.select_node` 后 selected=true——无需截图。

**P2 验收**：任何 MCP 客户端（Claude Code / Cursor）经 `http://127.0.0.1:47653/mcp`
完成「创建节点 → 计数 → 选中 → 设属性（undo 入栈）→ 查询 → 撤销」全流程。

### 1.1 两层分层（P1 能力面 / P2 暴露层）

P2 是 P1 的**延续**而非能力语义的升级：P1 定义「编辑器能向 AI 提供什么」，
P2 定义「外部客户端怎么调用它」。

| 层 | 职责 | 依赖 | 验收形态 | 文件 |
|---|---|---|---|---|
| **P1 能力面**（内功） | 语义 UI 树导出 + 语义操作 + 查询方法——纯 fork 内 C++，无网络/协议 | 无（纯 C++） | 脚本/命令行读 `ui.get_tree` → `editor.select_node`，无需截图 | `editor_ui_tree` / `semantic_ops` |
| **P2 暴露层**（接口） | MCP server：把 P1 能力面包装成标准 MCP 工具/资源/事件，供 Claude Code / Cursor / 本 harness 即插即用；**不新增能力语义**（事件面除外，见 §5） | P1 + MCP SDK（路线图依赖列） | MCP 客户端完成「创建→选中→设属性→撤销」全流程 | `ai_bridge` |
| **接缝**（P1↔P2 共用） | 能力面注册表：方法名/描述/JSON Schema（含 required）/handler 唯一事实源 | — | tools/list 与直连分发均由注册表生成 | `semantic_registry` |

要点：
- **延续关系**：P2 依赖 P1（路线图依赖列：P1 + MCP SDK），两者是同一能力面的两种视图——
  P1 实现并注册能力，P2 通过注册表生成 `tools/list` 并分发调用。
- **能力面共享**（方案 §3.3）：接缝处就是 `SemanticRegistry`。P1 的能力注册一次，
  P2 的 MCP 工具面与未来的 WebBridge 委托都从同一注册表取——若没有这层接缝，
  分发表与元数据表双份维护必然漂移（`scene.create_node` 默认名曾在 WebBridge/MCP 间分叉）。
- **交付说明**：本次把 P1+P2 合并进同一模块一次实现（标题即「P1/P2」），并非严格两阶段；
  文件归属见上表。P2 事件面（notifications/SSE）仍是暂缓项（`emit_event` 仅留接口）。

## 2. 实现（`modules/ai/`，新模块，11 文件）

| 文件 | 分层 | 职责 |
|---|---|---|
| `config.py` / `SCsub` | 构建 | 模块注册（`env.editor_build` 门控） |
| `register_types.cpp/h` | 构建 | EDITOR 级别延迟启动 AiBridge（MessageQueue 第一帧） |
| `editor_ui_tree.h/cpp` | **P1 能力面** | **语义 UI 树导出**：遍历 EditorNode 的 Control 树 → role/name/state/items；TreeItem 递归（root item 即场景根时也要导出）；语义 ID（ai_name meta 优先段） |
| `semantic_ops.h/cpp` | **P1 能力面** | **语义操作**：ui.activate（Button→ui_accept 真实路径 / TreeItem→选中）+ set_text/focus（只读拒绝）+ editor.select_node/set_prop（类型转换 + 路径守卫 + Inspector 联动 undo）/get_state/undo/redo + scene.get_node_count/create_node（返回最终 path） |
| `semantic_registry.h/cpp` | **接缝**（P1↔P2） | **能力面注册表**（方案 §3.3）：方法名/描述/JSON Schema（含 required）/handler 唯一事实源；AiBridge 的 tools/list 与分发均由此生成 |
| `ai_bridge.h/cpp` | **P2 暴露层** | **MCP HTTP server**：NetSocket 自管 accept/recv + 字节级 HTTP 帧解析（Content-Length 大小写不敏感）+ 每连接输出队列跨帧 flush + 连接/输入/空闲上限 + 仅绑 127.0.0.1 + JSON-RPC 数值错误码 + MCP 分发（initialize/ping/tools/list/call/resources/list/read） |

**启动**：`AI_BRIDGE_PORT=47653` 环境变量（设 0 关闭；默认 47653）。全主线程
（SceneTree::process_frame 帧泵，与 WebBridge 同线程）。

**能力面共享**：`SemanticRegistry` 为共享能力面注册表（方法名/描述/schema/handler 唯一事实源），
MCP 工具面（tools/list + tools/call）与直连 JSON-RPC 分发均由注册表驱动；WebBridge
委托迁移列为后续。

## 3. 验证（端到端，日志证据）

### P1（TCP JSON-RPC → P2 改为 HTTP）

```
ui.get_tree        → 场景树 3dMain（id=/item/0:3dMain, selected=false）✓ 语义数据，无截图
editor.select_node → 3dMain selected → true（AI 操作精确反映到编辑器 UI）✓
editor.set_prop    → undo 入栈（can_undo:true，AI 与人工同栈）✓
editor.get_state   → 场景/选中/undo 状态 ✓
```

### P2（MCP HTTP，标准协议）

```
initialize         → baize-godot-ai / protocol 2026-07-28（无状态规范）✓
tools/list         → 11 工具（ui.* / editor.* / scene.*）✓
tools/call scene.create_node("AINode") → instance_id；get_node_count 1→2 ✓
tools/call editor.select_node("AINode") / set_prop(position) / undo ✓
resources/read ai://ui/tree → 语义树含 3dMain ✓
```

## 3.5 评审修复轮（2026-08-03，reviewer 全量评审 + 修复 + 实机验证）

修复清单（详见评审结论）：
- **P0**：编辑器退出空指针崩溃——Main::cleanup 先删 SceneTree 后反初始化编辑器级模块，
  `stop_frame_pump` 必须判 singleton 空 + `is_connected()`（`--quit-after 120` 实机验证 EXIT=0）。
- 传输：仅绑 `127.0.0.1`（无鉴权控制面禁止外部可达）；HTTP 帧改字节级解析
  （Content-Length 大小写不敏感；中文 body 跨 recv 块正确）；响应走每连接输出队列
  跨帧 flush（780KB 树快照不再截断）；非瞬时 recv 错误即关连接；连接/输入/输出/空闲上限。
- 协议：JSON-RPC 错误码改数值（-32601/-32602/-32002，内部码入 data）；parse-error
  响应带 `id:null`；通知（无 id）不响应；工具 schema 合法（无 null，含 required）。
- 语义操作：`set_prop` 按 PropertyInfo 类型转换（JSON 数组→Vector/Color/Transform，
  "abc"→拒绝而非静默 0）+ 脚本属性存在性校验 + anchors_preset/layout_mode 联动状态
  undo（与 Inspector 一致）+ 只读拒绝 + 路径守卫（禁绝对/..，目标须在场景内）；
  `create_node` 返回最终 name/path；Button 激活改走 BaseButton 的 ui_accept 真实输入路径
  （toggle/ButtonGroup/弹层语义完整）；SpinBox 单次 value_changed；TreeItem 激活→选中。
- 语义树：Tree 的 `items` 移至控件字典兄弟字段（方案 §2.2）；TreeItem ID 带树控件
  前缀（全局可解，根 item = "…/item"）；LineEdit 密码字段只出 `secret:true`；
  editable 状态如实导出；OptionButton 无选中项不再报错；语义 ID 段优先 ai_name meta。
- 架构：新增 `semantic_registry`（能力面注册表，消除分发表/元数据双份维护）。

### 3.6 评审修复轮 2（shifu 关键逻辑复审后）

shifu 结论：常规退出崩溃已修复（无残留 P0），但上轮修复后有 7 个 P1 需在作为默认开启的
编辑器控制面前处理。已修复并实机验证：
- **传输鉴权**：仅 POST /mcp + Content-Type: application/json + 拒所有 Origin 头（阻断
  浏览器 CSRF/DNS rebinding）+ 可选 `AI_BRIDGE_TOKEN` Bearer 鉴权（401/403/404/405/
  411/415 均实机验证）；重复 Content-Length/Transfer-Encoding 拒绝；完整超大头部（含
  分隔符后）直接拒绝。
- **EOF 解耦**：读侧 FIN 后仍处理缓冲中完整请求并应答（half-close 实机验证）。
- **非有限/精度**：`_arr_num`/同类型 FLOAT 拒绝 Inf/NaN；FLOAT→INT 要求整值 + 范围检查；
  Vector*i 用 int32 组件校验；数学类型要求精确元素个数（[1,2,3] 不再截断成 Vector2）。
- **hint 校验**：enum/flags 属性值越界拒绝（不再被 setter 静默吞掉仍报 ok）。
- **属性策略**：仅允许 Inspector 可见属性（要求 EDITOR 位，拒 READ_ONLY/INTERNAL；
  注意 NO_EDITOR == STORAGE 是 bit0，不能按位与判断——scene_file_path 等已拒）。
- **联动 undo 完整化**：移植 EditorInspector::_edit_set 的 ClassDB 联动属性 +
  `_get_linked_undo_properties` 动态联动撤销（Range::set_min 等不再只恢复单属性）。
- **注册表 required 强制**：分发前校验参数为对象 + 必需键齐全（缺 name 不再静默建
  AINode；无必需参数的方法允许省略 params）。
- **TreeItem 根路由**：`<树控件>/item`（无尾斜杠）根 item 可激活（叶子根场景节点选中
  实机验证 selection=["."]）；前缀非 Tree 时回退普通控件激活。
- **杂项**：scene 路径拒 subname（`Player:garbage` 不再静默解析到 Player）；Button 激活
  改 BaseButton（覆盖 LinkButton/TextureButton）；LineEdit 补发截断后真实值；BaseButton
  直承控件 role 归类为 button。

## 3.7 多实例与端口策略（决策 E：显式端口配置）

调研结论（2026-08-03，业界 MCP 服务多实例做法）：本地编辑器类 MCP（godot-mcp、
blender-mcp、unity-mcp）均按**单实例假设** + **显式端口配置**（blender 的 `BLENDER_PORT`
环境变量、unity 的每项目设置），客户端在配置文件中写明地址；stdio 是本地主流传输但
要求服务端是客户端可 spawn 的子进程（长驻 GUI 编辑器不适用）；服务多时用 MCP gateway。

据此采用：
- **默认端口 47653 + 默认开启**（`AI_BRIDGE_PORT` 未设即默认），每实例可用环境变量显式指定——
  即 blender 同款机制；harness 多开时给每个实例分配不同端口即可。
- **去掉 `set_reuse_address_enabled(true)`**：Windows 的 SO_REUSEADDR 允许第二个 socket
  绑定同一端口（端口劫持），多开时会把 MCP 连接发到错误实例——宁可干净 bind 失败。
- **bind 失败清晰报错**：显示失败端口 + 端口来源（默认/环境变量）+ 改法
  （`AI_BRIDGE_PORT=<其他端口>`，0 关闭，`netstat -ano | findstr :<端口>` 查占用），
  不静默禁用、不自动回退。

**实测**（2026-08-03）：实例 B 与实例 A 同端口时，B 打印上述三段清晰报错、桥不启动
（编辑器其余功能正常）。另发现：**本 fork 双开编辑器本身已被 WebView/CEF 挡住**——
CEF cache 目录固定为 `%APPDATA%/baize-godot/cef`（`webview_core.cpp:868`，按应用名
非按项目），第二个实例 CefInitialize 报 "terminal state / existing browser session" 并以
exit=0 退出，且会连累第一个实例的 CEF 会话。**该问题由同事在 webview 模块解决中
（尚未合并）**——本模块不涉及；合并后双开编辑器即可，AI Bridge 的端口策略（§3.7）
已就绪。

## 4. 排坑记录（后续维护必需）

1. **本 fork `Variant::is_null()` 对非 OBJECT 类型返回 true**（`core/variant/variant.cpp:1094`
   自定义实现：`if (type == OBJECT && _get_obj().obj) return false; else return true`）——
   JSON 数字 id（解析为 FLOAT）被误判 null → 响应永不发送。**id 存在性必须用
   `Dictionary::has()`，不可用 `is_null()`**。
2. **`StreamPeerSocket::poll` 的"空缓冲即 FIN"误判**：数据读空后的瞬时空缓冲被
   `if (get_available_bytes() == 0) disconnect_from_host()` 当成对端关闭，持续连接被断开。
   → 改用 `NetSocket` 自管 accept/recv。
3. **本环境 select 对 accept 连接数据不可见**（poll 恒 ERR_BUSY，FIONREAD 恒 0，
   三个独立客户端栈复现）→ 直接非阻塞 `recv` 轮询（`ERR_BUSY` 语义准确，每帧一次系统调用）。
4. **场景树 root item 即场景根**（叶子节点）——Tree items 导出若只遍历 root 的**子**
   会漏掉叶子根；需 root 自身也导出。
5. **`export_tree` 非 Control 子节点分支覆盖 root**：`_export_control(child, node)`
   把容器内容写进 root 节点——应统一 append 到 children。
6. **`NetSocket` 需先 `open()` 再 bind**（`create()` 只建对象）；`open` 第三参
   `IP::Type &` 为输出参数（决定地址族，与 TCPServer::listen 同序）。
7. **Vector `operator[]` 返回 const**——写访问用 `ptrw()[i]`。
8. **本 fork `EditorNode` 直接继承 Node**（非 Control）——`cast_to<Control>(EditorNode)`
   编译失败；语义树根只能是容器（id "." 不可作为操作目标）。
9. **本 fork `Variant` 无 `convert()` 成员**（只有 can_convert）——简单类型互转需手工
   （INT/FLOAT/BOOL/String/StringName/NodePath）；Vector/Color/Transform 用
   `Variant` 的转换运算符显式构造（Basis 有 (V3,V3,V3) 与 (V3,real,V3) 重载歧义，
   必须显式 `(Vector3)` 强转）。
10. **Button 语义激活不能直接 emit `pressed`**——BaseButton::gui_input 对鼠标事件要求
    `status.hovering`（真实鼠标悬停）；合成 `InputEventAction("ui_accept")` 走无障碍
    激活同路，toggle/ButtonGroup/弹层语义完整。
11. **`Vector` 无 `push_front`**（本 fork）——自底向上收集段后用 `reverse()`。
12. **引擎清理顺序**：`Main::cleanup` 先 `delete_main_loop()`（SceneTree singleton 置空）
    后 `uninitialize_modules(EDITOR)`——帧泵拆除必须判空，否则桥启动后退出必崩。
13. **`Main::cleanup` 也回收接受连接**：stop() 中 `clients_.clear()` 释放 NetSocket
    Ref 即关闭；无需逐个显式 close。
14. **Windows `SO_REUSEADDR` ≠ Unix 语义**：允许第二个 socket 绑定同一地址端口
    （端口劫持，MSDN 文档化行为）——监听 socket 一律不设 reuse_address，宁可干净
    失败；Unix 上重启快速恢复所需的 reuse 语义在 Windows 监听场景并不必要。

## 5. 遗留

- MCP 事件推送（SSE 事件流）P2 暂缓（`emit_event` 接口已留）
- `ai://ui/tree` 快照 ~780KB（全编辑器 UI）——AI token 消耗优化（深度/过滤参数）列 P5
- 关键控件语义名（`meta "ai_name"`，如场景树 = "scene_tree"）尚未挂——当前靠
  role+路径定位，跨会话稳定性由 P5 的语义名补齐
- WebBridge 方法委托到 SemanticOps（能力面统一注册表）列为后续
- 协议类型自动生成（C++ 方法表 → TS/MCP schema）可选后话
