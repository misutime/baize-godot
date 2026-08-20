/**
 * @baize/godot-process：Godot WS 客户端——连接 + 认证握手 + 生命周期（gd_provider 测试套件）。
 *
 * 当前提供：GodotClient（WS 连接 + 认证握手 + 就绪态 + 生命周期）。
 * spawn 编排（launchGodot）未实现——e2e 由 vitest 直接 spawn 编辑器进程。
 */
export { GodotClient } from "./godot-client";
export type { GodotClientOptions, GodotClientState } from "./godot-client";
