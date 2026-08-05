// CefViewClient 桥传输层：invoke 请求/响应配对（req_id ↔ method_result 下行）+ 事件订阅。
// 协议规范见《doc/plans/Godot编辑器UI重构方案-TS路线-WebUI架构-桥协议与前端SDK.md》§3。
//
// 调用约定（与 C++ 侧 webview_core::invokeMethodNotify 对齐，1a 实测验证）：
//   CefViewClient.invoke(method, JSON.stringify({ req_id, ...params }))
//   CefViewClient.addEventListener(type, (payloadJson: string) => void)
// CefViewClient 为 CefViewCore 注入的桥对象；注入缺失必须显式报错，不静默回退。

export interface BridgeError {
  code: string;
  message: string;
}

/** CefViewCore 注入桥对象的最小接口（可测试替身）。 */
export interface CefViewClientLike {
  invoke(method: string, argsJson: string): void;
  addEventListener(type: string, listener: (payloadJson: string) => void): void;
  removeEventListener(type: string, listener: (payloadJson: string) => void): void;
}

declare global {
  interface Window {
    CefViewClient?: CefViewClientLike;
  }
}

let client: CefViewClientLike | null = null;
let transportStarted = false;
let reqSeq = 0;

interface PendingCall {
  resolve: (value: unknown) => void;
  reject: (error: BridgeError) => void;
  timer: ReturnType<typeof setTimeout>;
}

const pending = new Map<string, PendingCall>();

/** 探测注入桥对象；缺失或形态不对显式报错（不静默回退，AGENTS.md 工程规则）。 */
export function getBridgeClient(): CefViewClientLike {
  if (!client) {
    const c = typeof window !== "undefined" ? window.CefViewClient : undefined;
    if (
      !c ||
      typeof c.invoke !== "function" ||
      typeof c.addEventListener !== "function" ||
      typeof c.removeEventListener !== "function"
    ) {
      throw new Error("CefViewClient bridge not available: webview 注入缺失或形态不符");
    }
    client = c;
  }
  return client;
}

/** 测试注入替身（vitest node 环境无 window）。 */
export function _setBridgeClientForTest(c: CefViewClientLike | null): void {
  client = c;
  transportStarted = false;
  pending.clear();
}

/** 重置传输状态（测试用）。 */
export function _resetTransportForTest(): void {
  transportStarted = false;
  pending.clear();
}

interface MethodResultMessage {
  req_id?: unknown;
  ok?: unknown;
  result?: unknown;
  error?: { code?: unknown; message?: unknown };
}

/** method_result 下行处理：按 req_id 配对。坏载荷/未知 req_id 防御性丢弃（协议保证合法，此处兜底）。 */
export function handleMethodResult(payloadJson: string): void {
  let msg: MethodResultMessage;
  try {
    msg = JSON.parse(payloadJson) as MethodResultMessage;
  } catch {
    return; // 非 JSON：非 method_result 语义，丢弃
  }
  if (typeof msg.req_id !== "string") {
    return;
  }
  const call = pending.get(msg.req_id);
  if (!call) {
    return; // 已超时/重复应答
  }
  pending.delete(msg.req_id);
  clearTimeout(call.timer);
  if (msg.ok) {
    call.resolve(msg.result);
  } else {
    call.reject({
      code: typeof msg.error?.code === "string" ? msg.error.code : "unknown",
      message: typeof msg.error?.message === "string" ? msg.error.message : "",
    });
  }
}

/** 惰性初始化：首次 invoke 前订阅 method_result 下行通道。 */
function ensureTransport(): void {
  if (transportStarted) {
    return;
  }
  transportStarted = true;
  getBridgeClient().addEventListener("method_result", (payloadJson) => handleMethodResult(payloadJson));
}

/**
 * 调用桥方法并等待应答。参数对象含 req_id（SDK 生成，字符串；C++ 侧 double 解析陷阱规避）。
 * 超时（默认 10s，可配）reject { code: "timeout" }——悬空防护（协议 §3.2）。
 */
export function invoke<T>(method: string, params: Record<string, unknown>, timeoutMs = 10000): Promise<T> {
  ensureTransport();
  const reqId = String(++reqSeq);
  const bridge = getBridgeClient();
  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(reqId);
      reject({ code: "timeout", message: `invoke ${method} 超时 (${timeoutMs}ms)` });
    }, timeoutMs);
    pending.set(reqId, { resolve: resolve as (value: unknown) => void, reject, timer });
    try {
      // params 先展开、req_id 最后赋值：防调用方参数覆盖 SDK 生成的请求 ID（配对破坏/串线，审查 P1）。
      bridge.invoke(method, JSON.stringify({ ...params, req_id: reqId }));
    } catch (e) {
      pending.delete(reqId);
      clearTimeout(timer);
      reject({ code: "invoke_failed", message: String(e) });
    }
  });
}

/** 测试辅助：当前 pending 调用数（迟到应答丢弃的可观测断言）。 */
export function _pendingCountForTest(): number {
  return pending.size;
}

/** 事件载荷 JSON 解析；失败显式抛错（§3.3 事件载荷均为 JSON 对象，不静默透传）。 */

/** 订阅事件下行（TriggerEvent → renderer → JS 监听器，参数为字符串载荷）。返回退订函数。 */
export function onEvent<T>(type: string, listener: (payload: T) => void): () => void {
  const bridge = getBridgeClient();
  const raw = (payloadJson: string): void => {
    let payload: unknown;
    try {
      payload = JSON.parse(payloadJson);
    } catch (e) {
      // 载荷非法：显式上报（不静默吞，不把字符串断言成 T 传给业务监听器）。
      console.error(`[ui-sdk] 事件 ${type} 载荷解析失败:`, e);
      return;
    }
    listener(payload as T);
  };
  bridge.addEventListener(type, raw);
  return () => {
    bridge.removeEventListener(type, raw);
  };
}
