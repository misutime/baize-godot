// WebDock React 壳（MVP 验收 2/3/4：选中显示 X、改 X 移动可撤销、视口拖动实时跟随）。
// 单选语义（协议事件 node_position_changed 以 node_id 标识，面板按当前选中显示；
// 多选场景位置显示可能串——MVP 验收为单选，多选支持留后续）。
//
// 快捷键语义（用户定案 2026-08-03）：
// - 焦点在输入框（编辑未确认）：Ctrl+Z = 浏览器文本撤销（非受控 input，原生撤销可靠）
// - 焦点在输入框外（已确认/点击面板其他区域）：Ctrl+Z/Y/Shift+Z = 编辑器 undo/redo（桥方法）

import { editor, getBridgeClient, scene } from "@baize/ui-sdk";
import { useEditorEvent } from "@baize/ui-sdk/react";
import { useCallback, useEffect, useRef, useState } from "react";

type BridgeState = "checking" | "ok" | "missing";

interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export default function App() {
  const [bridge, setBridge] = useState<BridgeState>("checking");
  const [nodeCount, setNodeCount] = useState<number | null>(null);
  const [selection, setSelection] = useState<string[]>([]);
  const [position, setPosition] = useState<Vec3 | null>(null);
  const [canUndo, setCanUndo] = useState(false);
  const [canRedo, setCanRedo] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  // 输入框聚焦中：不被 node_position_changed 事件覆盖（用户编辑优先），blur 时提交。
  const editingRef = useRef(false);
  const busyRef = useRef(false);
  // 非受控输入（浏览器原生文本撤销需要 DOM 管 value；受控组件会被 React 渲染覆盖）。
  const xRef = useRef<HTMLInputElement>(null);
  const yRef = useRef<HTMLInputElement>(null);
  const zRef = useRef<HTMLInputElement>(null);

  const currentPath = selection[0] ?? null;

  const runAction = useCallback(async (action: () => Promise<unknown>): Promise<void> => {
    if (busyRef.current) {
      return; // 防并发
    }
    busyRef.current = true;
    setBusy(true);
    setError(null);
    try {
      await action();
    } catch (e) {
      const err = e as { code?: string; message?: string };
      setError(`操作失败 [${err.code ?? "unknown"}]: ${err.message ?? String(e)}`);
    } finally {
      busyRef.current = false;
      setBusy(false);
    }
  }, []);

  const refreshCount = useCallback((): void => {
    void runAction(async () => {
      setNodeCount(await scene.getNodeCount());
    });
  }, [runAction]);

  // 初始：桥探测 + 场景节点数。
  useEffect(() => {
    try {
      getBridgeClient();
      setBridge("ok");
      void runAction(async () => {
        setNodeCount(await scene.getNodeCount());
      });
    } catch {
      setBridge("missing");
    }
  }, [runAction]);

  // 选中变化 → 显示路径 + 拉取位置（getNodePosition 用场景相对路径，与事件对齐）。
  useEditorEvent(editor.onSelectionChanged, (payload) => {
    setSelection(payload.node_paths);
    const path = payload.node_paths[0];
    if (path) {
      setPosition(null); // 新选中：先清空防显示旧值
      void runAction(async () => {
        setPosition(await scene.getNodePosition({ node_path: path }));
      });
    } else {
      setPosition(null);
    }
  });

  // 视口拖动 → 位置实时跟随（验收 4）；输入框聚焦时不覆盖（用户编辑优先）。
  useEditorEvent(editor.onPositionChanged, (payload) => {
    if (!editingRef.current) {
      setPosition(payload.position);
    }
  });

  // undo 栈状态 → 按钮可用性。
  useEditorEvent(editor.onUndoStackChanged, (payload) => {
    setCanUndo(payload.can_undo);
    setCanRedo(payload.can_redo);
  });

  // 外部位置变化（选中拉取/拖动/撤销）→ 同步非受控输入框 DOM 值（不触发提交）。
  // 任一轴编辑中跳过（审查 P1）：X 提交回写 setPosition 时若用户已在输入 Y，
  // 无条件写 DOM 会重置 Y 的编辑文本——聚焦中不碰 DOM，blur 提交后恢复同步。
  useEffect(() => {
    if (editingRef.current) {
      return;
    }
    if (xRef.current) {
      xRef.current.value = position ? String(position.x) : "";
    }
    if (yRef.current) {
      yRef.current.value = position ? String(position.y) : "";
    }
    if (zRef.current) {
      zRef.current.value = position ? String(position.z) : "";
    }
  }, [position]);

  // 提交：失焦/回车 → 读非受控值 → setNodePosition（undo 入栈）；非法输入跳过。
  const commitPosition = useCallback((): void => {
    if (!currentPath) {
      return;
    }
    const nx = Number.parseFloat(xRef.current?.value ?? "");
    const ny = Number.parseFloat(yRef.current?.value ?? "");
    const nz = Number.parseFloat(zRef.current?.value ?? "");
    if (Number.isNaN(nx) || Number.isNaN(ny) || Number.isNaN(nz)) {
      return; // 非法输入不提交（防写入 NaN；输入框保留用户编辑）
    }
    const newPos: Vec3 = { x: nx, y: ny, z: nz };
    void runAction(async () => {
      await scene.setNodePosition({ node_path: currentPath, position: newPos });
      setPosition(newPos); // 乐观同步显示（撤销/后续事件可再覆盖）
    });
  }, [currentPath, runAction]);

  // 快捷键：输入框外 Ctrl+Z/Y/Shift+Z → 编辑器 undo/redo。
  // 输入框聚焦（未确认编辑）时不接管——浏览器文本撤销（非受控 input 原生可靠）。
  // 背景：web_panel 键盘转发无条件（面板聚焦即进页面 + accept_event 阻断 Godot 快捷键），
  // 不接管则 Ctrl+Z 总被浏览器吃掉；空栈按 Ctrl+Z 不报错（nothing_to_undo 为正常态）。
  // 不经过 runAction（审查 P2）：busy 时 runAction 静默丢弃动作——快捷键是瞬时期望，
  // 被防抖吞掉等于丢失；此处直接调用桥方法，错误仅非 nothing_* 时显示。
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent): void => {
      if (!(e.ctrlKey || e.metaKey)) {
        return;
      }
      const key = e.key.toLowerCase();
      if (key !== "z" && key !== "y") {
        return;
      }
      const target = e.target;
      if (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement) {
        return; // 输入框：浏览器默认文本撤销（用户未确认的编辑）
      }
      const runShortcut = (action: () => Promise<unknown>): void => {
        e.preventDefault();
        void action().catch((err) => {
          const e2 = err as { code?: string; message?: string };
          if (e2.code !== "nothing_to_undo" && e2.code !== "nothing_to_redo") {
            setError(`操作失败 [${e2.code ?? "unknown"}]: ${e2.message ?? String(err)}`);
          }
        });
      };
      if (key === "z" && e.shiftKey) {
        runShortcut(() => editor.redo());
      } else if (key === "z") {
        runShortcut(() => editor.undo());
      } else {
        runShortcut(() => editor.redo()); // y
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, []);

  const createNodeClick = useCallback((): void => {
    void runAction(async () => {
      await scene.createNode({ name: "WebNode" });
      setNodeCount(await scene.getNodeCount());
    });
  }, [runAction]);

  const undoClick = useCallback((): void => {
    void runAction(async () => {
      try {
        await editor.undo();
      } catch (err) {
        const e2 = err as { code?: string };
        if (e2.code !== "nothing_to_undo") {
          throw err;
        }
      }
    });
  }, [runAction]);

  const redoClick = useCallback((): void => {
    void runAction(async () => {
      try {
        await editor.redo();
      } catch (err) {
        const e2 = err as { code?: string };
        if (e2.code !== "nothing_to_redo") {
          throw err;
        }
      }
    });
  }, [runAction]);

  const numInput = (axis: "x" | "y" | "z") => (
    <input
      id={`pos-${axis}`}
      ref={axis === "x" ? xRef : axis === "y" ? yRef : zRef}
      type="number"
      step="any"
      defaultValue={position?.[axis] ?? ""}
      disabled={!position}
      onFocus={() => {
        editingRef.current = true;
      }}
      onBlur={() => {
        editingRef.current = false;
        commitPosition();
      }}
      onKeyDown={(e) => {
        if (e.key === "Enter") {
          (e.target as HTMLInputElement).blur();
        }
      }}
      className="w-full rounded border border-[#567] bg-[#334] px-1 py-0.5 text-right font-mono text-[#cfc] outline-none focus:border-[#8ab] disabled:opacity-40"
    />
  );

  return (
    <div className="flex h-full flex-col gap-3 p-3">
      <header className="flex items-center justify-between">
        <h1 className="text-sm font-semibold">WebDock</h1>
        <span className={`text-xs ${bridge === "ok" ? "text-[#7c7]" : "text-[#f88]"}`}>
          {bridge === "checking" && "桥连接中..."}
          {bridge === "ok" && "已连接"}
          {bridge === "missing" && "桥缺失（非 WebDock 环境）"}
        </span>
      </header>

      {bridge === "missing" && (
        <p className="text-xs text-[#f88]">CefViewClient 注入缺失——页面仅在 WebDock 内可用。</p>
      )}

      {/* 场景信息（验收 1 前置：真实数据来自桥） */}
      <section className="flex items-center justify-between gap-2">
        <span className="text-xs text-[#9ca]">
          场景节点数: <b className="text-[#cfc]">{nodeCount ?? "—"}</b>
        </span>
        <button type="button" onClick={refreshCount} disabled={busy} className="btn">
          刷新
        </button>
      </section>

      {/* 操作按钮 */}
      <section className="flex gap-2">
        <button type="button" onClick={createNodeClick} disabled={busy} className="btn flex-1">
          创建节点
        </button>
        <button type="button" onClick={undoClick} disabled={busy || !canUndo} className="btn flex-1">
          撤销
        </button>
        <button type="button" onClick={redoClick} disabled={busy || !canRedo} className="btn flex-1">
          重做
        </button>
      </section>

      {/* 选中节点属性（验收 2/3/4） */}
      <section className="flex flex-col gap-1.5 rounded border border-[#334] bg-[#2a2a2e] p-2">
        <h2 className="text-xs text-[#9ca]">选中节点</h2>
        {currentPath ? (
          <>
            <p className="truncate font-mono text-xs" title={currentPath}>
              {currentPath}
            </p>
            <div className="grid grid-cols-3 gap-2">
              {(["x", "y", "z"] as const).map((axis) => (
                <label
                  key={axis}
                  htmlFor={`pos-${axis}`}
                  className="flex items-center gap-1 text-xs text-[#9ca]"
                >
                  <span className="w-3 uppercase">{axis}</span>
                  {numInput(axis)}
                </label>
              ))}
            </div>
            <p className="text-[10px] text-[#789]">改后回车/失焦提交（可撤销）；视口拖动实时跟随。</p>
          </>
        ) : (
          <p className="text-xs text-[#789]">未选中节点（请先在场景中选中一个 Node3D）</p>
        )}
      </section>

      {error && (
        <p className="rounded border border-[#844] bg-[#422] px-2 py-1 text-xs text-[#f88]">{error}</p>
      )}
    </div>
  );
}
