// 协议注册表：TS 类型 ↔ 协议字符串的单一声明点（协议 §3.3，SDK 设计 §4.2）。
// 前端组件永不直接碰 window.CefViewClient——只经本层类型化 API。

import { invoke, onEvent } from "./transport";

/** 无参方法标记类型（keyof 为 never，触发无参签名）。 */
export type EmptyParams = Record<never, never>;

type ParamsTuple<P extends object> = keyof P extends never ? [] : [params: P, timeoutMs?: number];

/**
 * 声明一个方法：返回类型化调用函数。
 * - 无参方法（P = EmptyParams）：调用签名为 `()` —— `scene.getNodeCount()` 合法；
 * - 有参方法：调用签名为 `(params, timeoutMs?)`，缺参在编译期报错。
 */
export function defineMethod<P extends object, R>(name: string): (...args: ParamsTuple<P>) => Promise<R> {
  return (...args: unknown[]) => {
    const [params, timeoutMs] = args as [P | undefined, number | undefined];
    return invoke<R>(name, (params ?? {}) as Record<string, unknown>, timeoutMs);
  };
}

/** 声明一个事件：返回订阅函数（listener → 退订函数）。 */
export function defineEvent<P>(name: string): (listener: (payload: P) => void) => () => void {
  return (listener) => onEvent<P>(name, listener);
}
