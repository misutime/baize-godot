/**
 * 渲染进程 App（MVP）：编辑器状态显示 + 选中节点 XYZ 位置编辑。
 *
 * 链路：window.godot（preload IPC）→ createIpcTransport（godot-rpc）→ createClient（godot-sdk）
 * ——渲染进程只用 godot-sdk 的客户端 API，不直连 WS/token。
 */
import { useEffect, useState } from "react";
import { createIpcTransport } from "@baize/godot-rpc";
import { createClient, type EditorStatePayload, type Vec3 } from "@baize/godot-sdk";

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

function vec3ToString(v: Vec3): string {
  return `(${v.x.toFixed(2)}, ${v.y.toFixed(2)}, ${v.z.toFixed(2)})`;
}

export default function App(): React.JSX.Element {
  const [state, setState] = useState<EditorStatePayload | null>(null);
  const [position, setPosition] = useState<Vec3 | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [editing, setEditing] = useState<Vec3 | null>(null);

  const selectedPath = state?.selection[0] ?? null;

  const refresh = async (): Promise<boolean> => {
    try {
      const s = await client.editor.get_state();
      setState(s);
      if (s.selection.length > 0) {
        const pos = await client.scene.get_node_position({ node_path: s.selection[0] });
        setPosition(pos);
        setEditing(pos);
      } else {
        setPosition(null);
        setEditing(null);
      }
      setError(null);
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
    // 事件驱动刷新：选中/位置变化（IPC 事件通道不依赖认证，随时可订阅）
    const unsubSel = client.editor.on_selection_changed(() => void refresh());
    const unsubPos = client.editor.on_position_changed(() => void refresh());
    return () => {
      cancelled = true;
      if (timer) {
        clearTimeout(timer);
      }
      unsubSel();
      unsubPos();
    };
  }, []);

  const applyPosition = async (): Promise<void> => {
    if (!selectedPath || !editing) {
      return;
    }
    try {
      await client.scene.set_node_position({ node_path: selectedPath, position: editing });
      setPosition(editing);
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const setAxis = (axis: "x" | "y" | "z", value: string): void => {
    setEditing((prev) => (prev ? { ...prev, [axis]: Number(value) } : prev));
  };

  return (
    <div className="min-h-screen bg-background text-foreground p-6">
      <div className="mx-auto max-w-2xl space-y-6">
        <div className="flex items-center justify-between">
          <h2 className="text-xl font-semibold">Baize Editor（MVP）</h2>
          <div className="flex gap-2">
            <Button variant="outline" size="sm" onClick={() => void refresh()}>
              刷新
            </Button>
            <Button variant="outline" size="sm" onClick={() => void client.editor.select_node({ node_path: "./Cube" })}>
              选中 ./Cube
            </Button>
            <Button variant="outline" size="sm" onClick={() => void client.editor.undo()}>
              撤销
            </Button>
            <Button variant="outline" size="sm" onClick={() => void client.editor.redo()}>
              重做
            </Button>
          </div>
        </div>

        {error && <p className="text-sm text-destructive">错误: {error}</p>}

        <section className="rounded-lg border p-4">
          <h3 className="mb-2 text-sm font-medium text-muted-foreground">编辑器状态</h3>
          {state === null ? (
            <p className="text-sm">连接中…</p>
          ) : (
            <ul className="space-y-1 text-sm">
              <li>
                场景: <span className="font-medium">{state.has_scene ? "已打开" : "无"}</span>
              </li>
              <li>
                选中: <span className="font-medium">{state.selection.length > 0 ? state.selection.join(", ") : "（无）"}</span>
              </li>
              <li>
                undo: <span className="font-medium">{state.can_undo ? "可" : "不可"}</span> / redo:{" "}
                <span className="font-medium">{state.can_redo ? "可" : "不可"}</span>
              </li>
            </ul>
          )}
        </section>

        {selectedPath && position && editing && (
          <section className="rounded-lg border p-4">
            <h3 className="mb-2 text-sm font-medium text-muted-foreground">位置编辑: {selectedPath}</h3>
            <p className="mb-3 text-sm">
              当前: <span className="font-mono">{vec3ToString(position)}</span>
            </p>
            <div className="flex items-end gap-2">
              {(["x", "y", "z"] as const).map((axis) => (
                <label key={axis} className="flex flex-col gap-1 text-xs text-muted-foreground">
                  {axis.toUpperCase()}
                  <Input
                    type="number"
                    step="0.1"
                    value={editing[axis]}
                    onChange={(e) => setAxis(axis, e.target.value)}
                    className="w-24 font-mono"
                  />
                </label>
              ))}
              <Button onClick={() => void applyPosition()}>应用位置</Button>
            </div>
          </section>
        )}
      </div>
    </div>
  );
}
