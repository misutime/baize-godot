// WebDock React 壳占位（工作项 3 填充：属性面板/场景树）。
// 当前仅验证 sdk 依赖链路与桥状态探测。

import { getBridgeClient } from "@baize/ui-sdk";
import { useEffect, useState } from "react";

type BridgeState = "checking" | "ok" | "missing";

export default function App() {
  const [state, setState] = useState<BridgeState>("checking");

  useEffect(() => {
    try {
      getBridgeClient();
      setState("ok");
    } catch {
      setState("missing");
    }
  }, []);

  return (
    <main style={{ padding: 16 }}>
      <h1 style={{ fontSize: 16, margin: "0 0 8px" }}>WebDock</h1>
      <p style={{ margin: 0, color: state === "ok" ? "#cfc" : "#f88" }}>
        {state === "checking" && "检查桥连接..."}
        {state === "ok" && "CefViewClient 桥已连接"}
        {state === "missing" && "CefViewClient 注入缺失（非 WebDock 环境）"}
      </p>
    </main>
  );
}
