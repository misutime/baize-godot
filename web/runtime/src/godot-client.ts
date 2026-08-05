/**
 * Godot WS 客户端（sidecar = client，Godot = server）：S1 主路径连接器。
 *
 * 契约（《doc/plans/Godot编辑器UI重构方案-TS路线-NodeSidecar落地-方案.md》§4.3/§4.4/§5.1）：
 * - 连接建立后首帧 sidecar.hello（params { token }）；认证 deadline 3s（hello 超时）；
 * - 握手失败（错误 token → RpcCallError / 超时 / ok=false）→ 明确日志 + 断开；
 * - 断开/失败后按退避序列 0.5/1/2/4/8s（单次封顶）重连，上限 10 次；每次成功握手 epoch+1；
 * - 旧连接的 message/close 回调经闭包 generation 守卫，不得喂当前 RpcClient（§4.4 崩溃恢复）；
 * - 服务端 stop() 下行 sidecar.shutdown 通知 → 立即 dispose（停止重连循环；§4.4 审查修订 P1-4）；
 * - invoke 仅在传输就绪（connected）时登记 pending 并发帧；未就绪态确定性拒绝（§4.4 审查修订 P1-5）；
 * - 重试达上限 → failAllPending 确定性拒绝全部 pending（§4.4 审查修订 P2-6）；
 * - token 明文永不落日志（§4.3 日志约束）。
 *
 * 复用 jsonrpc.ts 的 RpcClient：send 注入 ws 发送；ws message 事件喂 handleFrame；
 * 断线/关闭调 failAllPending（确定性拒绝 pending）；线级合同（string id、batch 拒绝）由 handleFrame 保证。
 */
import type { SidecarHelloParams, SidecarHelloResult } from "@baize/rpc";
import { WebSocket } from "ws";

import { RpcCallError, RpcClient, RpcTimeoutError } from "./jsonrpc";

/** 默认重连退避序列（秒）：0.5/1/2/4/8，之后按 8s 单次封顶（§4.4）。 */
const DEFAULT_BACKOFF_SECONDS = [0.5, 1, 2, 4, 8] as const;
/** 默认重连上限（§4.4：上限 10 次；不含首次连接）。 */
const DEFAULT_MAX_RECONNECTS = 10;
/** 认证 deadline（§4.4：sidecar 侧 3s）。 */
const DEFAULT_HELLO_TIMEOUT_MS = 3000;

/** 连接生命周期状态（§4.4）。 */
export type GodotClientState = "idle" | "connecting" | "connected" | "reconnecting" | "failed" | "disposed";

export interface GodotClientOptions {
  /** Godot WS 地址；缺省读 env BAIZE_GODOT_WS_URL。 */
  url?: string;
  /** 握手 token；缺省读 env BAIZE_GODOT_TOKEN。 */
  token?: string;
  /** 项目路径（S1 仅记录，不参与握手）。 */
  projectPath?: string;
  /** 重连退避序列（秒，单次封顶）；测试可注入更短序列。默认 [0.5, 1, 2, 4, 8]。 */
  backoffSeconds?: number[];
  /** 重连上限（不含首次连接）；默认 10。 */
  maxReconnects?: number;
  /** 握手（sidecar.hello）超时（ms）；默认 3000（认证 deadline 3s）。 */
  helloTimeoutMs?: number;
  /** 每次握手成功后回调（含重连成功）；供上层日志/事件重订阅钩子。 */
  onReady?: (client: GodotClient, hello: SidecarHelloResult) => void;
}

export class GodotClient {
  private readonly rpc: RpcClient;
  private readonly url: string;
  private readonly token: string;
  private readonly projectPath: string | undefined;
  private readonly backoffSeconds: number[];
  private readonly maxReconnects: number;
  private readonly helloTimeoutMs: number;
  private readonly onReady?: (client: GodotClient, hello: SidecarHelloResult) => void;

  private ws: WebSocket | null = null;
  private state_: GodotClientState = "idle";
  /** 连接尝试代际：每次尝试 +1；闭包守卫（旧连接的 message/close 回调不得喂当前 RpcClient）。 */
  private generation = 0;
  /** 成功握手次数：每次成功连接 +1（epoch，§4.4）。 */
  private epoch_ = 0;
  /** 本次断连期已重连次数；握手成功后归零（预算按会话重置）。 */
  private reconnectCount = 0;
  private backoffTimer: NodeJS.Timeout | null = null;
  private disposed = false;

  constructor(options: GodotClientOptions = {}) {
    this.url = options.url ?? process.env.BAIZE_GODOT_WS_URL ?? "";
    this.token = options.token ?? process.env.BAIZE_GODOT_TOKEN ?? "";
    if (this.url === "") {
      throw new Error("缺少 BAIZE_GODOT_WS_URL：GodotClient 需要 Godot WS 地址");
    }
    if (this.token === "") {
      throw new Error("缺少 BAIZE_GODOT_TOKEN：Godot 面握手需要 token（Godot spawn 时应经 env 下发）");
    }
    this.backoffSeconds = options.backoffSeconds ?? [...DEFAULT_BACKOFF_SECONDS];
    this.maxReconnects = options.maxReconnects ?? DEFAULT_MAX_RECONNECTS;
    this.helloTimeoutMs = options.helloTimeoutMs ?? DEFAULT_HELLO_TIMEOUT_MS;
    this.onReady = options.onReady;
    this.projectPath = options.projectPath;
    this.rpc = new RpcClient((text) => this.sendFrame(text));
    // §4.4 审查修订 P1-4：订阅 sidecar.shutdown（服务端 stop() 下行，sidecar_server.cpp:130）→
    // 立即 dispose（关 ws + failAllPending + 停退避 timer）；dispose 后 close 回调经 disposed 守卫不再调度重连。
    this.rpc.onNotification((method) => {
      if (method === "sidecar.shutdown" && !this.disposed) {
        console.log("[sidecar] 收到 sidecar.shutdown，停止连接循环");
        this.dispose();
      }
    });
  }

  /** 启动连接循环（幂等；failed 后可再次调用，重置预算重试）。 */
  connect(): void {
    if (this.disposed || (this.state_ !== "idle" && this.state_ !== "failed")) {
      return;
    }
    console.log(`[sidecar] 连接 Godot WS: ${this.url}`);
    this.reconnectCount = 0;
    this.openAndHello();
  }

  /**
   * 代理 RpcClient.invoke；仅在传输就绪（connected）时登记 pending 并发帧。
   * 未就绪态（idle/connecting/reconnecting）确定性拒绝，不登记 pending、不发帧（§4.4 审查修订 P1-5）：
   * 重连只发 hello，不会重放请求，静默丢帧会让调用者等到自身超时。
   */
  invoke<T = unknown>(method: string, params?: unknown, timeoutMs?: number): Promise<T> {
    // 复审 P2（CLOSING 竞态）：state=connected 但 ws 已进入 CLOSING（close 事件尚未触发）时
    // sendFrame 会静默丢帧——以实际 socket 就绪为准，避免 pending 挂到超时。
    if (this.state_ === "connected" && this.ws !== null && this.ws.readyState === WebSocket.OPEN) {
      return this.rpc.invoke<T>(method, params, timeoutMs);
    }
    switch (this.state_) {
      case "disposed":
        return Promise.reject(new Error("GodotClient 已 dispose"));
      case "failed":
        return Promise.reject(new Error("GodotClient 连接失败（重连达上限），不可调用"));
      default:
        return Promise.reject(new Error(`传输未就绪（state=${this.state_}）`));
    }
  }

  /** 停止重连 + 关闭 WS + 拒绝全部 pending（幂等）。 */
  dispose(): void {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    this.state_ = "disposed";
    if (this.backoffTimer !== null) {
      clearTimeout(this.backoffTimer);
      this.backoffTimer = null;
    }
    const ws = this.ws;
    this.ws = null;
    if (ws !== null && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
      ws.close();
    }
    this.rpc.dispose();
  }

  /** 成功握手次数（每次成功连接 +1；测试断言 epoch 递增）。 */
  get epoch(): number {
    return this.epoch_;
  }

  get state(): GodotClientState {
    return this.state_;
  }

  get isConnected(): boolean {
    return this.state_ === "connected";
  }

  private sendFrame(text: string): void {
    const ws = this.ws;
    if (ws !== null && ws.readyState === WebSocket.OPEN) {
      ws.send(text);
    }
    // 未连接/连接中：帧静默丢弃——pending 由超时或断线 failAllPending 兜底
  }

  private openAndHello(): void {
    const gen = ++this.generation;
    this.state_ = "connecting";
    const ws = this.createSocket(gen);
    if (ws === null) {
      return; // 创建失败已记日志并调度重连
    }
    this.ws = ws;

    ws.on("message", (data) => {
      if (gen !== this.generation || this.disposed) {
        return; // 旧连接闭包守卫：不得喂当前 RpcClient
      }
      this.rpc.handleFrame(data.toString());
    });

    ws.on("open", () => {
      if (gen !== this.generation || this.disposed) {
        return;
      }
      void this.doHello(gen, ws);
    });

    ws.on("error", (err) => {
      if (gen !== this.generation || this.disposed) {
        return;
      }
      console.error(`[sidecar] Godot WS 错误: ${err.message}`);
    });

    ws.on("close", (code, reason) => {
      if (gen !== this.generation || this.disposed) {
        return;
      }
      this.ws = null;
      this.rpc.failAllPending(`Godot WS 连接断开（code=${code}）`);
      if (this.state_ === "connected") {
        const reasonText = reason.length > 0 ? `, reason=${reason.toString()}` : "";
        console.warn(`[sidecar] Godot WS 断开（code=${code}${reasonText}），进入退避重连`);
      }
      this.scheduleReconnect(gen);
    });
  }

  private createSocket(gen: number): WebSocket | null {
    try {
      return new WebSocket(this.url);
    } catch (err) {
      console.error("[sidecar] 创建 Godot WS 失败:", err instanceof Error ? err.message : String(err));
      this.scheduleReconnect(gen);
      return null;
    }
  }

  private async doHello(gen: number, ws: WebSocket): Promise<void> {
    let hello: SidecarHelloResult;
    try {
      hello = await this.rpc.invoke<SidecarHelloResult>(
        "sidecar.hello",
        { token: this.token } satisfies SidecarHelloParams,
        this.helloTimeoutMs,
      );
    } catch (err) {
      if (gen !== this.generation || this.disposed) {
        return;
      }
      if (err instanceof RpcCallError) {
        const dataCode = err.data?.code;
        console.error(
          `[sidecar] 握手失败: sidecar.hello 被拒绝（code=${err.code}${dataCode !== undefined ? `, data.code=${dataCode}` : ""}）——请核对 BAIZE_GODOT_TOKEN`,
        );
      } else if (err instanceof RpcTimeoutError) {
        console.error(
          `[sidecar] 握手超时（${this.helloTimeoutMs}ms）: sidecar.hello 无应答——Godot 侧未就绪？`,
        );
      } else {
        console.error("[sidecar] 握手失败:", err instanceof Error ? err.message : String(err));
      }
      this.closeForReconnect(ws, gen);
      return;
    }
    if (gen !== this.generation || this.disposed) {
      return;
    }
    if (hello.ok !== true) {
      console.error("[sidecar] 握手失败: sidecar.hello 返回 ok=false");
      this.closeForReconnect(ws, gen);
      return;
    }
    this.epoch_ += 1;
    this.reconnectCount = 0;
    this.state_ = "connected";
    console.log(`[sidecar] Godot WS 已连接（epoch=${this.epoch_}）: ${this.url}`);
    if (this.projectPath !== undefined) {
      console.log(`[sidecar] 项目路径: ${this.projectPath}`);
    }
    this.onReady?.(this, hello);
  }

  /** 握手失败/被拒后主动断开：统一走 close 事件 → failAllPending + scheduleReconnect。 */
  private closeForReconnect(ws: WebSocket, gen: number): void {
    if (gen !== this.generation || this.disposed) {
      return;
    }
    if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
      ws.close();
    } else {
      this.scheduleReconnect(gen); // 已关闭：close 事件不会再触发
    }
  }

  /** 退避调度（幂等：close 事件与 closeForReconnect 双路径只生效一次）。 */
  private scheduleReconnect(gen: number): void {
    if (gen !== this.generation || this.disposed || this.backoffTimer !== null) {
      return;
    }
    if (this.reconnectCount >= this.maxReconnects) {
      // §4.4 审查修订 P2-6：先确定性拒绝全部 pending（不留给各自超时兜底，避免不一致的迟到失败），再置 failed
      this.rpc.failAllPending("重连达上限");
      this.state_ = "failed";
      console.error(
        `[sidecar] 重连达上限（${this.maxReconnects} 次），停止自动重连——请检查 Godot 进程或手动重启`,
      );
      return;
    }
    this.state_ = "reconnecting";
    const attempt = this.reconnectCount + 1;
    const delayMs = this.backoffSeconds[Math.min(this.reconnectCount, this.backoffSeconds.length - 1)] * 1000;
    console.warn(`[sidecar] 计划重连 ${attempt}/${this.maxReconnects}: ${delayMs}ms 后重试`);
    this.backoffTimer = setTimeout(() => {
      this.backoffTimer = null;
      this.reconnectCount += 1;
      this.openAndHello();
    }, delayMs);
  }
}
