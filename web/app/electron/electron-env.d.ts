/// <reference types="vite-plugin-electron/electron-env" />

// vite-plugin-electron/electron-env 已声明 VITE_DEV_SERVER_URL 等；
// 这里补充本项目自有 env（进程环境变量文档化，便于 VS Code/CI 配置）。
declare namespace NodeJS {
  interface ProcessEnv {
    /** 调试开关：=0 关闭 dev 模式 DevTools；设置任意值启用构建 sourcemap（vite.config 读取）。 */
    VSCODE_DEBUG?: string;
    /** Provider 端口/token（与 Provider 同源；缺省 dev 宽松）。 */
    BAIZE_PROVIDER_PORT?: string;
    BAIZE_PROVIDER_TOKEN?: string;
    /** Godot 打开的工程路径（缺省 test-projects/provider）。 */
    BAIZE_PROJECT_PATH?: string;
  }
}
