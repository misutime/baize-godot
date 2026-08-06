import type { GodotBridge } from "../../shared/ipc";

// preload（contextBridge）暴露的 window.godot——类型与 preload 实现同源（src/shared/ipc.ts）。
declare global {
  interface Window {
    godot: GodotBridge;
  }
}
