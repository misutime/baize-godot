# WebUI 前端工程——实现文档（sdk + ui workspace）

> **状态**：2026-08-03 编写（工作项 2 完成）。衔接《WebUI架构-桥协议与前端SDK.md》（协议 §3 + SDK 设计 §4）。
> **范围**：`web/` workspace 脚手架 + `@baize/ui-sdk` 全量实现 + `@baize/ui` 骨架。
> **技术栈定案**：React 19 + Vite 8（用户裁决，2026-08-03）；其余按实现推荐落地。

---

## 1. 技术栈（2026-08 实测版本）

| 层 | 选型 | 实测版本 | 说明 |
|---|---|---|---|
| 包管理 | pnpm workspace | 11.17.0 | 单 lockfile；esbuild 构建脚本白名单（pnpm 10+ 默认阻止） |
| 语言 | TypeScript | 5.9.3 | strict + verbatimModuleSyntax |
| 构建 | Vite 8（Rolldown 内核） | 8.2.0 | 2026-03 起 Rolldown+Oxc 为默认，替代 esbuild+Rollup（[vite.dev](https://vite.dev)） |
| UI 框架 | React | 19.2.8 | 2026-07 最新稳定；React Compiler v1.0 生态（[react.dev](https://react.dev)） |
| 测试 | Vitest | 3.2.7 | node 环境 + 假桥注入（无 DOM 依赖） |
| lint/format | Biome | 2.5.6 | 绿地默认：单 Rust 二进制，preset:recommended + assist 导入整理 |
| 运行时 | Node | 24.14.0 | 当前活跃 LTS |

**决策依据**：编辑器内嵌面板（离线 `file://` 加载、体积敏感）→ SDK 零运行时依赖、产物 base:'./' 相对路径；
两包规模 → 不引入 Turborepo/Nx（Vite 8 本身快）；无远端数据 → 不引入 TanStack Query。

## 2. 工程结构

```text
web/
├── package.json            workspace 根（scripts 委派 build/test/lint/format）
├── pnpm-workspace.yaml     packages: sdk, ui；onlyBuiltDependencies: esbuild
├── biome.json              2.5.x schema；formatter(space 2)/linter(preset)/assist
├── .gitignore              node_modules/ dist/ *.tsbuildinfo
├── pnpm-lock.yaml
├── sdk/  @baize/ui-sdk     （零运行时依赖；react 为 optional peer）
│   ├── src/transport.ts    CefViewClient 桥传输层
│   ├── src/registry.ts     协议注册表（defineMethod/defineEvent）
│   ├── src/bridge.ts       协议 §3.3 方法/事件实例
│   ├── src/index.ts        主入口：scene/editor 类型化 API 对象
│   ├── src/react.ts        hooks（子路径 ./react）
│   ├── src/*.test.ts       单测（假桥）
│   └── vite.config.ts      lib mode 双入口（index + react），ESM + sourcemap
└── ui/  @baize/ui          （React 壳，工作项 3 填充）
    ├── vite.config.ts      base:'./'（file:// 相对加载）+ @vitejs/plugin-react
    ├── index.html
    └── src/{main.tsx, App.tsx, index.css}
```

## 3. SDK 分层与协议映射

### 3.1 transport（`transport.ts`）——传输层，组件不直接触碰

- **调用约定**（与 C++ 侧 `webview_core::invokeMethodNotify` 对齐，1a 实测验证）：
  `CefViewClient.invoke(method, JSON.stringify({ req_id, ...params }))`
- **应答配对**：首次 invoke 惰性订阅 `method_result` 下行 → 按 `req_id`（SDK 生成字符串；
  规避 C++ 侧 JS 数字 → double 解析陷阱）配对 → Promise resolve/reject
- **悬空防护**（协议 §3.2）：超时（默认 10s 可配）reject `{code:"timeout"}`；迟到的应答
  按未知 req_id 丢弃
- **错误透传**：`{ok:false, error:{code,message}}` → reject `BridgeError`；桥注入缺失
  （`window.CefViewClient` 形态不符）**显式抛错**，不静默回退（AGENTS.md 工程规则）
- **事件订阅**：`onEvent(type, listener)` 解析 JSON 载荷（非 JSON 原字符串透传），返回退订函数

### 3.2 registry（`registry.ts`）——类型 ↔ 协议字符串单点声明

```ts
defineMethod<{ name: string }, number>("scene.create_node")  // → 类型化调用函数
defineEvent<{ node_paths: string[] }>("editor.selection_changed") // → 订阅函数
```

### 3.3 bridge（`bridge.ts`）——协议 §3.3 全量实例

方法：`getNodeCount` / `createNode` / `getNodePosition` / `setNodePosition` / `undo` / `redo`
事件：`onSelectionChanged` / `onPositionChanged` / `onUndoStackChanged`

### 3.4 hooks（`react.ts`，子路径 `@baize/ui-sdk/react`）

- `useEditorEvent(subscribe, handler)`：订阅自动清理 + 最新闭包（重渲染后 handler 始终最新）
- `useBridgeCall(call)`：loading/error 封装，in-flight 防重复调用，错误上抛不吞

## 4. 关键机制与决策

- **零依赖体积**：SDK 构建产物 `index.js` gzip 1.13kB（Vite 8 lib mode + `external:["react"]`）；
  react 为 optional peerDependency，主包不绑框架
- **base:'./'**：ui 产物资源路径为 `./assets/...`（已实测），`file://` 下由 WebDock OSR 直接加载
- **测试方法**：vitest node 环境 + `_setBridgeClientForTest` 注入假桥（记录 invoke + 手动触发
  method_result/事件），13 用例覆盖协议字符串格式、配对、超时、错误、注入缺失
- **踩坑留档**：npm 包名 `biome` 是他人旧包（0.3.3）——正解 `@biomejs/biome`；CSS 不能用 `//`
  注释（lightningcss 报 Invalid empty selector）；Biome 2.x 弃用 `recommended` 改 `preset`、
  导入整理移入 `assist`；pnpm 10+ 需 `onlyBuiltDependencies` 放行 esbuild

## 5. 命令

```text
cd web
pnpm install                 # 安装（lockfile 冻结）
pnpm -r run test             # sdk vitest（13 用例）
pnpm -r run typecheck        # 两包 tsc --noEmit
pnpm -r run build            # sdk（vite lib + tsc d.ts）+ ui（vite）
pnpm exec biome check .      # lint + 格式（--write 自动修复）
```

## 6. 验证记录（2026-08-03，Win 实机）

- sdk 单测 13/13 通过；两包 typecheck 通过；biome check 0 错误
- sdk 构建：`dist/index.js` 2.51kB（gzip 1.13kB）+ `dist/react.js` 0.70kB + d.ts（已排除测试文件）
- ui 构建：`dist/index.html` + assets（JS gzip 60.36kB，React 基础体积）
- 产物路径实测 `./assets/...`（base:'./' 生效）

## 7. 遗留与下一步（工作项 3）

- **React 壳实质**：属性面板（MVP 验收 2/3：选中显示 X、改 X 移动可撤销）+ 场景信息展示；
  App.tsx 现为桥状态探测占位
- **task ui-build 衔接**：Taskfile 新增 `ui-build`（`pnpm --dir web/ui build` → 产物拷入
  `bin/webview/ui/`，dock 加载零改动——替换现 stage 的 bridge.html stub）
- **Tailwind CSS v4**：样式方案待确认后引入（工作项 3）
- **体积优化**（可选）：ui JS 60KB gzip 为 React 基础体积，面板复杂后可考虑分包/lazy
- 协议扩展：`inspector.set_prop` 等后续方法（架构文档 §5 后续项）
