/**
 * React hooks（子路径 @baize/godot-sdk/react；react 为 peerDependency）。
 * 订阅/调用均 transport 无关（函数注入式）——组件不直接接触 transport。
 */
import { useEffect, useRef, useState } from "react";

export type EventSubscription<T> = (listener: (payload: T) => void) => () => void;

/**
 * 订阅事件（自动清理 + 最新闭包）。
 *
 * @param subscribe 由事件绑定生成的订阅函数（`client.editor.on_selection_changed` 等）
 * @param handler   每次事件触发的处理器；组件重渲染后始终调用最新 handler
 */
export function useEditorEvent<T>(subscribe: EventSubscription<T>, handler: (payload: T) => void): void {
  const handlerRef = useRef(handler);
  handlerRef.current = handler; // 每次渲染更新，订阅回调永远走最新闭包
  useEffect(() => subscribe((payload) => handlerRef.current(payload)), [subscribe]);
}

/** 错误 → Error（保留 code/message，不丢失诊断信息）。 */
function toError(e: unknown): Error {
  if (e instanceof Error) {
    return e;
  }
  if (e !== null && typeof e === "object") {
    const errObj = e as { code?: unknown; message?: unknown };
    if (typeof errObj.code === "string" && typeof errObj.message === "string") {
      const err = new Error(errObj.message);
      (err as Error & { code?: string }).code = errObj.code;
      return err;
    }
  }
  return new Error(String(e));
}

/** 调用的状态封装：loading 期间防重复调用，错误显式暴露（不吞）。 */
export function useBridgeCall<TArgs extends object, TResult>(
  call: (args: TArgs) => Promise<TResult>,
): {
  run: (args: TArgs) => Promise<TResult>;
  loading: boolean;
  error: Error | null;
} {
  const busyRef = useRef(false);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<Error | null>(null);

  const run = async (args: TArgs): Promise<TResult> => {
    if (busyRef.current) {
      return Promise.reject(new Error("call in flight")); // 防并发重复调用
    }
    busyRef.current = true;
    setLoading(true);
    setError(null);
    try {
      const result = await call(args);
      return result;
    } catch (e) {
      const err = toError(e);
      setError(err);
      throw err; // 错误上抛，调用方决定处理（不静默）
    } finally {
      busyRef.current = false;
      setLoading(false);
    }
  };

  return { run, loading, error };
}
