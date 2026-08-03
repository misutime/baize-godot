// 协议注册表：TS 类型 ↔ 协议字符串的单一声明点（协议 §3.3，SDK 设计 §4.2）。
// 前端组件永不直接碰 window.CefViewClient——只经本层类型化 API。

import { invoke, onEvent } from "./transport";

/** 声明一个方法：返回类型化调用函数（参数对象 → Promise<result>）。 */
export function defineMethod<P extends object, R>(
  name: string,
): (params: P, timeoutMs?: number) => Promise<R> {
  return (params, timeoutMs) => invoke<R>(name, params as Record<string, unknown>, timeoutMs);
}

/** 声明一个事件：返回订阅函数（listener → 退订函数）。 */
export function defineEvent<P>(name: string): (listener: (payload: P) => void) => () => void {
  return (listener) => onEvent<P>(name, listener);
}
