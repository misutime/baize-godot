/**
 * Godot WS 客户端（Godot Provider = server，本类 = client）：连接 + 认证握手 + 就绪态 + 生命周期。
 *
 * 职责边界（与 @baize/godot-rpc 的分工）：
 * - rpc/ws.ts（createWsTransport）：WS 连接/重连/配对/事件下行——传输层；
 * - 本类：hello 认证握手（就绪门禁）、invoke 就绪态控制、shutdown 通知处理、epoch/state 上报。
 *
 * 契约（与 Godot Provider 侧 C++ 实现对齐）：
 * - 连接建立后首帧 hello（params { token }）；认证 deadline 3s（hello 超时）；
 * - 握手失败（错误 token / 超时 / ok=false）→ 明确日志 + 断开（由传输层重连）；
 * - 断开/失败后按退避序列重连（传输层内置，上限可配）；每次成功握手 epoch+1；
 * - Provider stop() 下行 shutdown 通知 → 立即 dispose（停止连接循环）；
 * - invoke 仅在已认证（ready）时放行；未就绪态确定性拒绝；
 * - token 明文永不落日志。
 */
import type { HelloParams, HelloResult, Transport } from "@baize/godot-rpc";
import { createWsTransport, RpcCallError, RpcTimeoutError, type TransportState } from "@baize/godot-rpc";

/** 认证 deadline（Provider 侧 3s）。 */
const DEFAULT_HELLO_TIMEOUT_MS = 3000;
/** 默认重连上限（不含首次连接）。 */
const DEFAULT_MAX_RECONNECTS = 10;

/** 连接生命周期状态（含认证门禁）。 */
export type GodotClientState = "idle" | "connecting" | "connected" | "reconnecting" | "failed" | "disposed";

export interface GodotClientOptions {
  /** Godot WS 地址；缺省读 env BAIZE_GODOT_WS_URL。 */
  url?: string;
  /** 握手 token；缺省读 env BAIZE_GODOT_TOKEN。 */
  token?: string;
  /** 项目路径（仅记录/日志）。 */
  projectPath?: string;
  /** 重连退避序列（秒）；默认 [0.5, 1, 2, 4, 8]。 */
  backoffSeconds?: number[];
  /** 重连上限（不含首次连接）；默认 10。 */
  maxReconnects?: number;
  /** 握手（hello）超时（ms）；默认 3000。 */
  helloTimeoutMs?: number;
  /** 每次握手成功后回调（含重连成功）；供上层日志/事件重订阅钩子。 */
  onReady?: (client: GodotClient, hello: HelloResult) => void;
}

export class GodotClient {
  private readonly transport: Transport;
  private readonly token: string;
  private readonly helloTimeoutMs: number;
  private readonly onReady?: (client: GodotClient, hello: HelloResult) => void;
  private readonly projectPath: string | undefined;

  private state_: GodotClientState = "idle";
  /** 已认证（握手成功）标记：invoke 门禁。 */
  private ready = false;
  /** 成功握手次数（每次成功连接 +1）。 */
  private epoch_ = 0;
  private disposed = false;

  constructor(options: GodotClientOptions = {}) {
    const url = options.url ?? process.env.BAIZE_GODOT_WS_URL ?? "";
    this.token = options.token ?? process.env.BAIZE_GODOT_TOKEN ?? "";
    if (url === "") {
      throw new Error("缺少 BAIZE_GODOT_WS_URL：GodotClient 需要 Godot WS 地址");
    }
    if (this.token === "") {
      throw new Error("缺少 BAIZE_GODOT_TOKEN：Godot 面握手需要 token（Godot spawn 时应经 env 下发）");
    }
    this.helloTimeoutMs = options.helloTimeoutMs ?? DEFAULT_HELLO_TIMEOUT_MS;
    this.onReady = options.onReady;
    this.projectPath = options.projectPath;

    this.transport = createWsTransport({
      url,
      backoffSeconds: options.backoffSeconds,
      maxReconnects: options.maxReconnects ?? DEFAULT_MAX_RECONNECTS,
      onStateChange: (state) => this.handleTransportState(state),
      log: (msg) => console.log(`[godot] ${msg}`),
    });

    // Provider stop() 下行 shutdown 通知 → 立即 dispose（停止重连循环）
    this.transport.onEvent((method) => {
      if (method === "shutdown" && !this.disposed) {
        console.log("[godot] 收到 shutdown 通知，停止连接循环");
        this.dispose();
      }
    });
  }

  /** 启动连接循环（幂等）：传输层在创建时已开始连接，本方法仅重置状态供 failed 后重试。 */
  connect(): void {
    if (this.disposed || (this.state_ !== "idle" && this.state_ !== "failed")) {
      return;
    }
    console.log(`[godot] 连接 Godot WS`);
    // 传输层构造即连；failed 后由上层重新 createWsTransport 或等待下次握手路径
    this.state_ = "connecting";
  }

  /** 代理能力调用；仅在已认证（ready）时放行，未就绪态确定性拒绝。 */
  invoke<T = unknown>(method: string, params?: unknown, timeoutMs?: number): Promise<T> {
    if (this.disposed) {
      return Promise.reject(new Error("GodotClient 已 dispose"));
    }
    if (!this.ready) {
      if (this.state_ === "failed") {
        return Promise.reject(new Error("GodotClient 连接失败（重连达上限），不可调用"));
      }
      return Promise.reject(new Error(`传输未认证（state=${this.state_}）`));
    }
    return this.transport.request<T>(method, params, timeoutMs);
  }

  /** 停止重连 + 关闭连接 + 拒绝全部 pending（幂等）。 */
  dispose(): void {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    this.ready = false;
    this.state_ = "disposed";
    this.transport.close();
  }

  get epoch(): number {
    return this.epoch_;
  }

  get state(): GodotClientState {
    return this.state_;
  }

  get isConnected(): boolean {
    return this.ready;
  }

  /** 传输层状态 → 本类状态机；connected 时触发认证握手。 */
  private handleTransportState(state: TransportState): void {
    if (this.disposed) {
      return;
    }
    switch (state) {
      case "connecting":
        this.state_ = "connecting";
        this.ready = false;
        break;
      case "connected":
        // WS 已建立 → 认证握手（每次连接/重连都要）
        this.state_ = "connected";
        void this.doHello();
        break;
      case "reconnecting":
        this.state_ = "reconnecting";
        this.ready = false;
        break;
      case "failed":
        this.state_ = "failed";
        this.ready = false;
        break;
      case "closed":
        this.state_ = "disposed";
        this.ready = false;
        break;
      default:
        break;
    }
  }

  /** 认证握手：成功 → ready + epoch+1 + onReady；失败 → 日志（传输层负责重连）。 */
  private async doHello(): Promise<void> {
    if (this.disposed) {
      return;
    }
    try {
      const hello = await this.transport.request<HelloResult>("hello", { token: this.token }, this.helloTimeoutMs);
      if (this.disposed) {
        return;
      }
      if (hello.ok !== true) {
        console.error("[godot] 握手失败: hello 返回 ok=false");
        return; // 传输层维持连接；Provider 侧会断开错误 token
      }
      this.epoch_ += 1;
      this.ready = true;
      this.state_ = "connected";
      this.transport.resetReconnectBudget?.(); // 握手成功：重连预算归零（认证失败也是失败）
      console.log(`[godot] Godot WS 已认证（epoch=${this.epoch_}）`);
      if (this.projectPath !== undefined) {
        console.log(`[godot] 项目路径: ${this.projectPath}`);
      }
      this.onReady?.(this, hello);
    } catch (err) {
      if (this.disposed) {
        return;
      }
      if (err instanceof RpcCallError) {
        const dataCode = err.data?.code;
        console.error(
          `[godot] 握手失败: hello 被拒绝（code=${err.code}${dataCode !== undefined ? `, data.code=${dataCode}` : ""}）——请核对 token`,
        );
      } else if (err instanceof RpcTimeoutError) {
        console.error(`[godot] 握手超时（${this.helloTimeoutMs}ms）: hello 无应答——Godot 侧未就绪？`);
      } else {
        console.error("[godot] 握手失败:", err instanceof Error ? err.message : String(err));
      }
      // 握手失败由传输层退避重连；此处仅记录（token 明文不落日志）
    }
  }
}
