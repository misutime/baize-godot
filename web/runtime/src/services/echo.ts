/**
 * S0 示例服务：echo。handler 返回语义 = { ok,result } / { ok:false,error }（§5.1 映射在分派层）。
 * params 校验失败返回业务码 invalid_params（→ -32000 + error.data.code）。
 */
import type { Handler } from "../jsonrpc";

export const echoHandler: Handler = (params) => {
  const text = (params as { text?: unknown } | null | undefined)?.text;
  if (typeof text !== "string") {
    return { ok: false, error: { code: "invalid_params", message: "params.text 必须为字符串" } };
  }
  return { ok: true, result: { text, ts: Date.now() } };
};
