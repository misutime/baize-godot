/**
 * @baize/godot-process：Electron 主进程宿主——Godot 进程连接与管理。
 *
 * 当前提供：GodotClient（WS 连接 + 认证握手 + 就绪态 + 生命周期）。
 * spawn 编排（launchGodot：spawn console exe --editor --headless + 端口发现 + env 注入 + 日志管道）
 * 第一阶段 M0 随 Godot Provider 一并实现（端口发现依赖 Provider 侧报端口）。
 */
export { GodotClient } from "./godot-client";
export type { GodotClientOptions, GodotClientState } from "./godot-client";
