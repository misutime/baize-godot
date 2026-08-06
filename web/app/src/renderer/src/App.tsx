/**
 * 渲染进程 App（M1）：工具栏 + 三栏布局（场景树 / 视口占位 / Inspector）。
 *
 * 链路：window.godot（preload IPC）→ createIpcTransport（godot-rpc）→ createClient（godot-sdk）
 * ——渲染进程只用 godot-sdk 的客户端 API，不直连 WS/token。
 *
 * - 工具栏：Undo / Redo / Save + 节点类型下拉 + 名称输入 + Add / Delete；
 * - 左栏：scene.get_tree + scene.changed 驱动的场景树（选中高亮、可折叠）；
 * - 中栏：M1 视口策略 A（Godot 独立窗口）占位；
 * - 右栏：scene.get_props / set_prop 驱动的 Inspector（标量 + Vector/Color 轴输入，
 *   本地编辑态草稿防止事件回写覆盖未提交输入）。
 */
import { useEffect, useState } from "react";
import { createIpcTransport } from "@baize/godot-rpc";
import { createClient, type EditorStatePayload, type PropInfo, type TreeNode } from "@baize/godot-sdk";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";

declare global {
  interface Window {
    godot: {
      request: (method: string, params?: unknown) => Promise<unknown>;
      onEvent: (listener: (method: string, params: unknown) => void) => () => void;
    };
  }
}

const client = createClient(
  createIpcTransport({
    request: (method, params) => window.godot.request(method, params),
    onEvent: window.godot.onEvent,
  }),
);

/** 工具栏可创建的节点类型（Godot 内置类，ClassDB 可直接实例化）。 */
const NODE_TYPES = ["Node3D", "Camera3D", "MeshInstance3D", "Node", "Sprite2D"] as const;

/** Inspector 可编辑的标量类型（对应值编码表的 BOOL/INT/FLOAT/STRING 族）。 */
const SCALAR_TYPES = new Set(["bool", "int", "float", "string", "StringName", "NodePath"]);

/** 按轴编辑的复合类型 → 轴名列表（对应值编码表的 VECTOR2/3/4、COLOR）。 */
const VECTOR_AXES: Record<string, string[]> = {
  Vector2: ["x", "y"],
  Vector2i: ["x", "y"],
  Vector3: ["x", "y", "z"],
  Vector3i: ["x", "y", "z"],
  Vector4: ["x", "y", "z", "w"],
  Color: ["r", "g", "b", "a"],
};

function formatValue(v: unknown): string {
  if (v === null || v === undefined) {
    return "—";
  }
  if (typeof v === "object") {
    return JSON.stringify(v);
  }
  return String(v);
}

function parseNumber(text: string): number {
  const n = Number(text);
  if (Number.isNaN(n)) {
    throw new Error("无效数字");
  }
  return n;
}

/** 将本地编辑草稿解析为 set_prop 可发送的值（与值编码表互逆）；缺失轴回退到当前属性值。 */
function parseDraft(prop: PropInfo, draft: Record<string, string>): unknown {
  const axis = (name: string): number => {
    const raw = draft[name];
    if (raw !== undefined) {
      return parseNumber(raw);
    }
    const v = (prop.value as Record<string, number> | null) ?? {};
    return v[name] ?? 0;
  };
  switch (prop.type) {
    case "int":
      return Math.trunc(parseNumber(draft.value ?? ""));
    case "float":
      return parseNumber(draft.value ?? "");
    case "string":
    case "StringName":
    case "NodePath":
      return draft.value ?? "";
    case "Vector2":
    case "Vector2i":
      return { x: axis("x"), y: axis("y") };
    case "Vector3":
    case "Vector3i":
      return { x: axis("x"), y: axis("y"), z: axis("z") };
    case "Vector4":
      return { x: axis("x"), y: axis("y"), z: axis("z"), w: axis("w") };
    case "Color":
      return { r: axis("r"), g: axis("g"), b: axis("b"), a: axis("a") };
    default:
      throw new Error(`不支持的属性类型 ${prop.type}`);
  }
}

// ---- 场景树行 ----

interface TreeNodeRowProps {
  node: TreeNode;
  depth: number;
  selectedPath: string | null;
  collapsed: ReadonlySet<string>;
  onToggle: (path: string) => void;
  onSelect: (path: string) => void;
}

function TreeNodeRow({
  node,
  depth,
  selectedPath,
  collapsed,
  onToggle,
  onSelect,
}: TreeNodeRowProps): React.JSX.Element {
  const hasChildren = node.children.length > 0;
  const isCollapsed = collapsed.has(node.path);
  const isSelected = selectedPath === node.path;
  return (
    <div>
      <div
        className={`flex cursor-pointer select-none items-center gap-1 rounded px-1 py-0.5 ${
          isSelected ? "bg-primary/15 text-primary" : "hover:bg-muted"
        }`}
        style={{ paddingLeft: `${depth * 12 + 4}px` }}
        onClick={() => onSelect(node.path)}
        title={node.path}
      >
        {hasChildren ? (
          <button
            type="button"
            className="w-4 shrink-0 text-muted-foreground"
            onClick={(e) => {
              e.stopPropagation();
              onToggle(node.path);
            }}
          >
            {isCollapsed ? "▸" : "▾"}
          </button>
        ) : (
          <span className="w-4 shrink-0 text-center text-muted-foreground/50">•</span>
        )}
        <span className="truncate">{node.name}</span>
        <span className="truncate text-xs text-muted-foreground">{node.type}</span>
      </div>
      {hasChildren && !isCollapsed
        ? node.children.map((child) => (
            <TreeNodeRow
              key={child.path}
              node={child}
              depth={depth + 1}
              selectedPath={selectedPath}
              collapsed={collapsed}
              onToggle={onToggle}
              onSelect={onSelect}
            />
          ))
        : null}
    </div>
  );
}

// ---- Inspector 属性行 ----

interface PropRowProps {
  prop: PropInfo;
  draft: Record<string, string> | undefined;
  onDraftChange: (name: string, axis: string, value: string) => void;
  onCommit: (prop: PropInfo, explicitValue?: unknown) => void;
}

function PropRow({ prop, draft, onDraftChange, onCommit }: PropRowProps): React.JSX.Element {
  const editable = prop.editable && (SCALAR_TYPES.has(prop.type) || prop.type in VECTOR_AXES);
  if (!editable) {
    return (
      <li className="flex items-center justify-between gap-2 py-0.5 text-sm">
        <span className="shrink-0 text-muted-foreground">{prop.name}</span>
        <span className="truncate font-mono text-xs text-muted-foreground/70">{formatValue(prop.value)}</span>
      </li>
    );
  }
  // bool：复选框即时提交（不进入本地草稿）
  if (prop.type === "bool") {
    return (
      <li className="flex items-center justify-between gap-2 py-0.5 text-sm">
        <span className="text-muted-foreground">{prop.name}</span>
        <input
          type="checkbox"
          checked={prop.value === true}
          onChange={() => onCommit(prop, !(prop.value === true))}
          className="h-4 w-4"
        />
      </li>
    );
  }
  // 标量：文本/数字输入，Enter 或失焦提交
  if (SCALAR_TYPES.has(prop.type)) {
    const isText = prop.type === "string" || prop.type === "StringName" || prop.type === "NodePath";
    return (
      <li className="flex items-center justify-between gap-2 py-0.5 text-sm">
        <span className="shrink-0 text-muted-foreground">{prop.name}</span>
        <Input
          type={isText ? "text" : "number"}
          step={isText || prop.type === "float" ? "any" : "1"}
          value={draft?.value ?? (prop.value === null || prop.value === undefined ? "" : String(prop.value))}
          onChange={(e) => onDraftChange(prop.name, "value", e.target.value)}
          onBlur={() => onCommit(prop)}
          onKeyDown={(e) => {
            if (e.key === "Enter") {
              e.currentTarget.blur();
            }
          }}
          className="h-7 w-36 font-mono text-xs"
        />
      </li>
    );
  }
  // Vector2/2i/3/3i/4、Color：按轴数字输入，失焦提交
  const axes = VECTOR_AXES[prop.type];
  const valueObj = (prop.value ?? {}) as Record<string, number>;
  return (
    <li className="flex flex-col gap-1 py-1 text-sm">
      <span className="text-muted-foreground">
        {prop.name}
        <span className="ml-1 text-xs text-muted-foreground/70">{prop.type}</span>
      </span>
      <div className="flex gap-1.5">
        {axes.map((axis) => (
          <label key={axis} className="flex flex-1 flex-col gap-0.5 text-xs text-muted-foreground">
            {axis}
            <Input
              type="number"
              step="any"
              value={draft?.[axis] ?? (valueObj[axis] === undefined ? "" : String(valueObj[axis]))}
              onChange={(e) => onDraftChange(prop.name, axis, e.target.value)}
              onBlur={() => onCommit(prop)}
              className="h-7 px-2 font-mono text-xs"
            />
          </label>
        ))}
      </div>
    </li>
  );
}

// ---- App ----

export default function App(): React.JSX.Element {
  const [state, setState] = useState<EditorStatePayload | null>(null);
  const [tree, setTree] = useState<TreeNode | null>(null);
  const [collapsed, setCollapsed] = useState<ReadonlySet<string>>(new Set());
  const [props, setProps] = useState<PropInfo[] | null>(null);
  const [propDrafts, setPropDrafts] = useState<Record<string, Record<string, string>>>({});
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [nodeType, setNodeType] = useState<string>("Node3D");
  const [nodeName, setNodeName] = useState<string>("");

  const selectedPath = state?.selection[0] ?? null;

  const refreshProps = async (path: string | null): Promise<void> => {
    if (path === null) {
      setProps(null);
      return;
    }
    try {
      const p = await client.scene.get_props({ node_path: path });
      setProps(p);
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      if (/invalid_node/.test(msg)) {
        // 选中节点已被删除（事件竞态）：静默清空，不报错
        setProps(null);
        return;
      }
      setError(msg);
    }
  };

  const refresh = async (): Promise<boolean> => {
    try {
      const s = await client.editor.get_state();
      setState(s);
      setError(null);
      try {
        // get_tree 成功直接返回树根（无打开场景时抛 no_scene 错误，属正常状态）
        setTree(await client.scene.get_tree());
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        if (/no_scene/.test(msg)) {
          setTree(null);
        } else {
          setError(msg);
        }
      }
      await refreshProps(s.selection[0] ?? null);
      return true;
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      if (/未认证|未就绪|未连接/.test(msg)) {
        return false; // Godot 未就绪（启动竞态）：调用方安排轮询重试，不显示为错误
      }
      setError(msg);
      return false;
    }
  };

  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | null = null;
    const poll = async (): Promise<void> => {
      if (cancelled) {
        return;
      }
      const ok = await refresh();
      if (!cancelled && !ok) {
        timer = setTimeout(() => void poll(), 2000); // Godot 未就绪：2s 后重试
      }
    };
    void poll();
    // 事件驱动刷新：选中/位置/场景变化重拉状态 + 树 + 属性（本地草稿保护未提交输入）
    const unsubSel = client.editor.on_selection_changed(() => void refresh());
    const unsubPos = client.editor.on_position_changed(() => void refresh());
    const unsubUndo = client.editor.on_undo_stack_changed((p) => {
      // 轻量更新 undo/redo 可用性（避免整树刷新打断操作）
      setState((prev) => (prev ? { ...prev, can_undo: p.can_undo, can_redo: p.can_redo } : prev));
    });
    const unsubScene = client.editor.on_scene_changed(() => void refresh());
    return () => {
      cancelled = true;
      if (timer) {
        clearTimeout(timer);
      }
      unsubSel();
      unsubPos();
      unsubUndo();
      unsubScene();
    };
  }, []);

  const runAction = async (action: () => Promise<unknown>): Promise<void> => {
    try {
      await action();
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const saveScene = async (): Promise<void> => {
    try {
      const { path } = await client.editor.save_scene();
      setError(null);
      setNotice(`已保存: ${path}`);
      window.setTimeout(() => setNotice(null), 3000);
    } catch (e) {
      setNotice(null);
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const addNode = async (): Promise<void> => {
    try {
      const trimmed = nodeName.trim();
      const { node_path } = await client.scene.create_node({
        type: nodeType,
        ...(trimmed !== "" ? { name: trimmed } : {}),
      });
      setNodeName("");
      await client.editor.select_node({ node_path });
      setError(null);
      await refresh();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const removeSelected = async (): Promise<void> => {
    if (!selectedPath || selectedPath === ".") {
      return; // 根节点禁止删除
    }
    try {
      await client.scene.remove_node({ node_path: selectedPath });
      setError(null);
      await refresh();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const toggleCollapsed = (path: string): void => {
    setCollapsed((prev) => {
      const next = new Set(prev);
      if (next.has(path)) {
        next.delete(path);
      } else {
        next.add(path);
      }
      return next;
    });
  };

  const setDraft = (name: string, axis: string, value: string): void => {
    setPropDrafts((prev) => ({ ...prev, [name]: { ...(prev[name] ?? {}), [axis]: value } }));
  };

  const commitProp = async (prop: PropInfo, explicitValue?: unknown): Promise<void> => {
    if (!selectedPath) {
      return;
    }
    let value = explicitValue;
    if (value === undefined) {
      const draft = propDrafts[prop.name];
      if (draft === undefined) {
        return; // 无本地修改（如只聚焦未输入）：不提交
      }
      try {
        value = parseDraft(prop, draft);
      } catch {
        setError(`属性 ${prop.name} 的值无效`);
        return;
      }
    }
    try {
      await client.scene.set_prop({ node_path: selectedPath, prop: prop.name, value });
      setPropDrafts((prev) => {
        const next = { ...prev };
        delete next[prop.name];
        return next;
      });
      setError(null);
      await refreshProps(selectedPath);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  return (
    <div className="flex min-h-screen flex-col bg-background text-foreground">
      {/* 工具栏 */}
      <header className="border-b px-6 py-3">
        <div className="flex flex-wrap items-center gap-2">
          <h2 className="mr-4 text-xl font-semibold">Baize Editor（M1）</h2>
          <Button
            variant="outline"
            size="sm"
            disabled={!state?.can_undo}
            onClick={() => void runAction(() => client.editor.undo())}
          >
            撤销
          </Button>
          <Button
            variant="outline"
            size="sm"
            disabled={!state?.can_redo}
            onClick={() => void runAction(() => client.editor.redo())}
          >
            重做
          </Button>
          <Button variant="outline" size="sm" onClick={() => void saveScene()}>
            保存
          </Button>

          <div className="mx-3 h-5 w-px bg-border" />

          <select
            value={nodeType}
            onChange={(e) => setNodeType(e.target.value)}
            className="h-9 rounded-md border border-input bg-transparent px-2 text-sm outline-none focus-visible:border-ring"
          >
            {NODE_TYPES.map((t) => (
              <option key={t} value={t}>
                {t}
              </option>
            ))}
          </select>
          <Input
            placeholder="节点名称（可选）"
            value={nodeName}
            onChange={(e) => setNodeName(e.target.value)}
            className="w-40"
          />
          <Button variant="outline" size="sm" onClick={() => void addNode()}>
            Add
          </Button>
          <Button
            variant="outline"
            size="sm"
            disabled={!selectedPath || selectedPath === "."}
            onClick={() => void removeSelected()}
          >
            Delete
          </Button>

          <div className="ml-auto flex items-center gap-2">
            <Button variant="outline" size="sm" onClick={() => void refresh()}>
              刷新
            </Button>
          </div>
        </div>
        {error && <p className="mt-2 text-sm text-destructive">错误: {error}</p>}
        {notice && <p className="mt-2 text-sm text-muted-foreground">{notice}</p>}
      </header>

      {/* 三栏主区 */}
      <main className="flex min-h-0 flex-1 gap-4 p-6">
        {/* 左：场景树 */}
        <aside className="flex w-64 shrink-0 flex-col rounded-lg border">
          <h3 className="border-b px-3 py-2 text-sm font-medium text-muted-foreground">场景树</h3>
          <div className="flex-1 overflow-auto p-2 text-sm">
            {state === null ? (
              <p className="px-1 py-1 text-muted-foreground">连接中…</p>
            ) : tree === null ? (
              <p className="px-1 py-1 text-muted-foreground">（无打开场景）</p>
            ) : (
              <TreeNodeRow
                node={tree}
                depth={0}
                selectedPath={selectedPath}
                collapsed={collapsed}
                onToggle={toggleCollapsed}
                onSelect={(path) => void runAction(() => client.editor.select_node({ node_path: path }))}
              />
            )}
          </div>
        </aside>

        {/* 中：视口占位（M1 视口策略 A：Godot 独立窗口） */}
        <section className="flex min-h-0 flex-1 items-center justify-center rounded-lg border text-sm text-muted-foreground">
          M1 视口策略 A：Godot 独立窗口
        </section>

        {/* 右：Inspector */}
        <aside className="flex w-80 shrink-0 flex-col rounded-lg border">
          <h3 className="border-b px-3 py-2 text-sm font-medium text-muted-foreground">Inspector</h3>
          <div className="flex-1 overflow-auto p-3">
            {selectedPath === null ? (
              <p className="text-sm text-muted-foreground">未选中节点</p>
            ) : props === null ? (
              <p className="text-sm text-muted-foreground">加载中…</p>
            ) : props.length === 0 ? (
              <p className="text-sm text-muted-foreground">无属性</p>
            ) : (
              <ul className="space-y-2">
                {props.map((prop) => (
                  <PropRow
                    key={prop.name}
                    prop={prop}
                    draft={propDrafts[prop.name]}
                    onDraftChange={setDraft}
                    onCommit={commitProp}
                  />
                ))}
              </ul>
            )}
          </div>
        </aside>
      </main>
    </div>
  );
}
