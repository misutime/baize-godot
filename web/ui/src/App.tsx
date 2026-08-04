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
  // 无编辑场景（[empty] 占位标签无根节点）：编辑器正常初始态，中性占位显示。
  const [noScene, setNoScene] = useState(false);
  // 输入框聚焦中：不被 node_position_changed 事件覆盖（用户编辑优先），blur 时提交。
  const editingRef = useRef(false);
  const busyRef = useRef(false);
  // 非受控输入（浏览器原生文本撤销需要 DOM 管 value；受控组件会被 React 渲染覆盖）。
  const xRef = useRef<HTMLInputElement>(null);
  const yRef = useRef<HTMLInputElement>(null);
  const zRef = useRef<HTMLInputElement>(null);

  const currentPath = selection[0] ?? null;

  // 统一错误分类：no_scene = 编辑器正常态（[empty] 占位标签无根节点）→ 中性占位；
  // 其余错误进红色横幅。除已定义的 no_scene 正常态外不静默吞错（AGENTS.md）。
  const applyBridgeError = useCallback((err: { code?: string; message?: string }): void => {
    if (err.code === "no_scene") {
      setNoScene(true);
      setNodeCount(null);
      return;
    }
    setError(`操作失败 [${err.code ?? "unknown"}]: ${err.message ?? String(err)}`);
  }, []);

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
      applyBridgeError(e as { code?: string; message?: string });
    } finally {
      busyRef.current = false;
      setBusy(false);
    }
  }, [applyBridgeError]);

  const refreshCount = useCallback((): void => {
    void runAction(async () => {
      const count = await scene.getNodeCount();
      setNodeCount(count);
      setNoScene(false); // 拉取成功 = 有场景根：自校正（覆盖原生建根等无事件路径）
    });
  }, [runAction]);

  // 场景状态同步：不经 runAction 的 busy 防并发——场景切换事件驱动的刷新不应被丢弃
  // （事件与用户动作重叠时 runAction 会静默丢弃）。只读查询，无副作用，可并发。
  const syncSceneState = useCallback((): void => {
    void scene
      .getNodeCount()
      .then((count) => {
        setNodeCount(count);
        setNoScene(false);
      })
      .catch((e) => {
        applyBridgeError(e as { code?: string; message?: string });
      });
  }, [applyBridgeError]);

  // 初始：桥探测 + 场景状态（无场景为正常态，不报错）。
  useEffect(() => {
    try {
      getBridgeClient();
      setBridge("ok");
      syncSceneState();
    } catch {
      setBridge("missing");
    }
  }, [syncSceneState]);

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

  // 编辑场景上下文变化（打开/关闭/切标签/建根删根）→ 刷新场景状态；无场景为正常态。
  useEditorEvent(editor.onSceneChanged, (payload) => {
    if (payload.has_scene) {
      setNoScene(false);
      syncSceneState(); // 场景打开/切换：重拉节点数（含新建未保存场景的根）
    } else {
      setNoScene(true);
      setNodeCount(null);
      setSelection([]);
      setPosition(null);
      setError(null); // 旧场景上下文遗留错误不再适用
    }
  });

  // 字体/界面缩放跟随 Godot 编辑器设置：视觉与原生 dock 对齐。
  // html font-size = main_font_size × display_scale（Tailwind 字号/间距全 rem 相对 root；
  // CEF 独立渲染不应用 Godot 界面缩放，4K Auto 下原生 14px 实际渲染 21px+，不乘则偏小）。
  // scale 重启生效（display_scale 需重启，页面加载拉取即可）；font_size 运行时生效（事件跟随）。
  const scaleRef = useRef(1);
  const applyUiMetrics = useCallback((fontSize: number, scale: number): void => {
    const px = Math.round(fontSize * scale * 100) / 100;
    document.documentElement.style.fontSize = `${px}px`;
    // 实际字号对照（诊断级）：html font-size 计算值，与原生 [editor-font] 实际字号对照。
    console.log(`[webdock-font] htmlFontSize=${px}px (mainFontSize=${fontSize} × scale=${scale})`);
  }, []);

  useEffect(() => {
    void Promise.all([editor.getUiFontSize(), editor.getUiScale()])
      .then(([fontSize, scale]) => {
        scaleRef.current = scale;
        applyUiMetrics(fontSize, scale);
      })
      .catch(() => {
        // 拉取失败：保持默认（index.css 14px），不阻塞面板
      });
  }, [applyUiMetrics]);

  useEditorEvent(editor.onUiFontSizeChanged, (payload) => {
    applyUiMetrics(payload.size, scaleRef.current);
  });

  // 字体族单一来源 = 编辑器：get_ui_font/get_ui_font_bold 返回实际生效路径
  // （main_font 设置优先，默认思源外部分发路径；内置回退时为空 → CSS 回退系统字体）。
  // 页面不再硬编码字体路径——换字体只改编辑器侧（文件或设置）。
  const boldFontRef = useRef(""); // 粗体路径缓存（事件刷新时重新拉取）
  // file:// URL 按路径组件编码（encodeURI 保留 #/%xx 会误解析文件名，审查 W1）。
  const toFileUrl = useCallback((p: string): string => {
    const parts = p.replace(/\\/g, "/").split("/");
    const encoded = parts
      .map((seg, i) => (i === 0 || i === 1 ? seg : encodeURIComponent(seg))) // 前两段：空/盘符 C: 不编码
      .join("/");
    return `file:///${encoded}`;
  }, []);
  const applyUiFont = useCallback(
    (regular: string, bold: string): void => {
      let styleEl = document.getElementById("baize-font-face") as HTMLStyleElement | null;
      if (!styleEl) {
        styleEl = document.createElement("style");
        styleEl.id = "baize-font-face";
        document.head.appendChild(styleEl);
      }
      if (!regular) {
        styleEl.textContent = ""; // 内置回退（无外部路径）：清除注入，回退系统字体
        document.documentElement.style.fontFamily = "";
        document.body.style.fontFamily = ""; // 恢复 index.css 的 body 字体声明
        return;
      }
      const src = (p: string): string => `url("${toFileUrl(p)}")`;
      // 无独立粗体文件（静态 custom 字体）：单 face，浏览器合成粗体（与编辑器 embolden 语义一致，审查 W3）。
      styleEl.textContent =
        regular === bold
          ? `@font-face { font-family: "baize-editor-font"; src: ${src(regular)}; font-display: swap; }`
          : `@font-face { font-family: "baize-editor-font"; src: ${src(regular)}; font-weight: 100 500; font-display: swap; }\n` +
            `@font-face { font-family: "baize-editor-font"; src: ${src(bold)}; font-weight: 600 900; font-display: swap; }`;
      document.documentElement.style.fontFamily =
        '"baize-editor-font", "Segoe UI", "Microsoft YaHei", system-ui, sans-serif';
      // body 也设（审查 P1）：index.css 的 body 显式 font-family 覆盖 html 继承，
      // 只设 html 时注入字体根本不渲染。
      document.body.style.fontFamily =
        '"baize-editor-font", "Segoe UI", "Microsoft YaHei", system-ui, sans-serif';
      // 字体诊断（产品级：宿主 consoleMessage → 编辑器 stderr，验证实际渲染字体）。
      console.log(
        `[webdock-font] bodyFamily="${getComputedStyle(document.body).fontFamily}" ` +
          `regular="${regular}" bold="${bold}"`,
      );
      // @font-face 异步加载：fonts.load 完成后再确认（立即 check 会因未加载而 false）。
      void document.fonts
        .load('1rem "baize-editor-font"')
        .then(() => {
          console.log(`[webdock-font] loaded=${document.fonts.check('1rem "baize-editor-font"')}`);
        })
        .catch((e: unknown) => {
          console.error(`[webdock-font] 字体加载失败: ${String(e)}`); // 显式暴露，不静默
        });
    },
    [toFileUrl],
  );

  useEffect(() => {
    void Promise.all([editor.getUiFont(), editor.getUiFontBold()])
      .then(([regular, bold]) => {
        boldFontRef.current = bold;
        applyUiFont(regular, bold);
      })
      .catch(() => {
        // 拉取失败：保持系统字体，不阻塞面板
      });
  }, [applyUiFont]);

  useEditorEvent(editor.onUiFontChanged, (payload) => {
    // 主字体变化时同步刷新 bold（bridge 的 bold 回退依赖 main_font，审查 W2）。
    void editor
      .getUiFontBold()
      .then((bold) => {
        boldFontRef.current = bold;
        applyUiFont(payload.path, bold);
      })
      .catch(() => {
        applyUiFont(payload.path, boldFontRef.current);
      });
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
        <h1 className="font-semibold">WebDock</h1>
        <span className={`${bridge === "ok" ? "text-[#7c7]" : "text-[#f88]"}`}>
          {bridge === "checking" && "桥连接中..."}
          {bridge === "ok" && "已连接"}
          {bridge === "missing" && "桥缺失（非 WebDock 环境）"}
        </span>
      </header>

      {bridge === "missing" && (
        <p className="text-[#f88]">CefViewClient 注入缺失——页面仅在 WebDock 内可用。</p>
      )}

      {/* 场景信息（验收 1 前置：真实数据来自桥） */}
      <section className="flex items-center justify-between gap-2">
        <span className="text-[#9ca]">
          场景节点数: <b className="text-[#cfc]">{noScene ? "未打开场景" : nodeCount ?? "—"}</b>
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
        <h2 className="text-[#9ca]">选中节点</h2>
        {currentPath ? (
          <>
            <p className="truncate font-mono" title={currentPath}>
              {currentPath}
            </p>
            <div className="grid grid-cols-3 gap-2">
              {(["x", "y", "z"] as const).map((axis) => (
                <label key={axis} htmlFor={`pos-${axis}`} className="flex items-center gap-1 text-[#9ca]">
                  <span className="w-3 uppercase">{axis}</span>
                  {numInput(axis)}
                </label>
              ))}
            </div>
            <p className="text-[#789]">改后回车/失焦提交（可撤销）；视口拖动实时跟随。</p>
          </>
        ) : noScene ? (
          <p className="text-[#789]">未打开场景（请先新建或打开一个场景）</p>
        ) : (
          <p className="text-[#789]">未选中节点（请先在场景中选中一个 Node3D）</p>
        )}
      </section>

      {error && <p className="rounded border border-[#844] bg-[#422] px-2 py-1 text-[#f88]">{error}</p>}
    </div>
  );
}
