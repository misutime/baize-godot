/**
 * 方法/事件声明层：TS 类型 ↔ 协议字符串的单一声明机制。
 * transport 由调用方注入（ws / ipc / inproc）——SDK 自身不绑定任何传输实现。
 */
import type { Transport } from "@baize/godot-rpc";

/** 无参方法标记类型（keyof 为 never，触发无参签名）。 */
export type EmptyParams = Record<never, never>;

type ParamsTuple<P extends object> = keyof P extends never ? [] : [params: P, timeoutMs?: number];

/**
 * 声明一个方法：绑定到给定 transport，返回类型化调用函数。
 * - 无参方法（P = EmptyParams）：调用签名为 `()`；
 * - 有参方法：调用签名为 `(params, timeoutMs?)`，缺参在编译期报错。
 */
export function defineMethod<P extends object, R>(
  transport: Transport,
  name: string,
): (...args: ParamsTuple<P>) => Promise<R> {
  return (...args: unknown[]) => {
    const [params, timeoutMs] = args as [P | undefined, number | undefined];
    return transport.request<R>(name, params ?? {}, timeoutMs);
  };
}

/** 声明一个事件：绑定到给定 transport，返回订阅函数（listener → 退订函数）。 */
export function defineEvent<P>(transport: Transport, name: string): (listener: (payload: P) => void) => () => void {
  return (listener) =>
    transport.onEvent((method, params) => {
      if (method === name) {
        listener(params as P);
      }
    });
}
