// WebDock React 壳（MVP 验收 2/3/4：选中显示 X、改 X 移动可撤销、视口拖动实时跟随）。
// 单选语义（协议事件 node_position_changed 以 node_id 标识，面板按当前选中显示；
// 多选场景位置显示可能串——MVP 验收为单选，多选支持留后续）。

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

  // WebDock 聚焦时 Ctrl+Z/Y/Shift+Z 接管为编辑器撤销（MVP 验收 3：改 X 后直接撤销）。
  // 背景：web_panel 键盘转发无条件（面板聚焦即进页面，accept_event 阻断 Godot 快捷键），
  // 不接管的话 Ctrl+Z 会被浏览器文本撤销吃掉，编辑器 undo 栈收不到。
  // 输入框内文本撤销被接管（数字输入场景价值低）；空栈按 Ctrl+Z 不报错（nothing_to_undo 是正常态）。
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent): void => {
      if (!(e.ctrlKey || e.metaKey)) {
        return;
      }
      const key = e.key.toLowerCase();
      const doRedo = (): void => {
        e.preventDefault();
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
      };
      const doUndo = (): void => {
        e.preventDefault();
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
      };
      if (key === "z" && e.shiftKey) {
        doRedo();
      } else if (key === "z") {
        doUndo();
      } else if (key === "y") {
        doRedo();
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
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

  const commitPosition = useCallback((): void => {
    if (!currentPath || !position) {
      return;
    }
    void runAction(async () => {
      await scene.setNodePosition({ node_path: currentPath, position });
    });
  }, [currentPath, position, runAction]);

  const createNodeClick = useCallback((): void => {
    void runAction(async () => {
      await scene.createNode({ name: "WebNode" });
      setNodeCount(await scene.getNodeCount());
    });
  }, [runAction]);

  const undoClick = useCallback((): void => {
    void runAction(async () => {
      await editor.undo();
    });
  }, [runAction]);

  const redoClick = useCallback((): void => {
    void runAction(async () => {
      await editor.redo();
    });
  }, [runAction]);

  const updateAxis = useCallback((axis: keyof Vec3, value: string): void => {
    const n = Number.parseFloat(value);
    setPosition((prev) => (prev ? { ...prev, [axis]: Number.isNaN(n) ? prev[axis] : n } : prev));
  }, []);

  const numInput = (axis: keyof Vec3) => (
    <input
      id={`pos-${axis}`}
      type="number"
      step="any"
      value={position?.[axis] ?? ""}
      disabled={!position}
      onFocus={() => {
        editingRef.current = true;
      }}
      onChange={(e) => updateAxis(axis, e.target.value)}
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
