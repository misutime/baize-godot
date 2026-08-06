import path from "node:path";
import { type ElectronApplication, _electron as electron, expect, type Page, test } from "@playwright/test";

const appRoot = path.resolve(import.meta.dirname, "..");

let electronApp: ElectronApplication;
let page: Page;

test.beforeAll(async () => {
  // 对构建产物跑（pretest: vite build --mode=test）：electron . 读 package.json main →
  // dist-electron/main/index.js，无 VITE_DEV_SERVER_URL → loadFile(dist/index.html)。
  // Linux CI 无头环境需 --no-sandbox + Xvfb（模板参考；本地 Windows 不需要）。
  const args = process.platform === "linux" ? [".", "--no-sandbox"] : ["."];
  electronApp = await electron.launch({ args, cwd: appRoot });
  page = await electronApp.firstWindow();
});

test.afterAll(async () => {
  await electronApp?.close();
});

test("窗口加载：标题 + 编辑器外壳", async () => {
  await expect(page).toHaveTitle(/Baize Editor/);
  await expect(page.getByRole("heading", { level: 2 })).toContainText("Baize Editor（M1）");
});

test("preload 桥已暴露 window.godot（contextBridge 三能力）", async () => {
  const api = await page.evaluate(() => ({
    type: typeof window.godot,
    keys: Object.keys(window.godot ?? {}).sort(),
  }));
  expect(api.type).toBe("object");
  expect(api.keys).toEqual(["onEvent", "onProcessStatus", "request"]);
});

test("视口状态面板离开初始「连接中…」（Godot 进程有确定状态）", async () => {
  const statusLoc = page.getByText(/Godot 进程：/);
  let prev = "";
  // spawn 后立即广播的"运行中"可能是瞬态（随后权限错误/立即退出）——等状态稳定（连续两次一致）
  // 再断言，避免瞬时态误通过（review PR#3 测试覆盖限制）。
  await expect
    .poll(
      async () => {
        const cur = (await statusLoc.textContent()) ?? "";
        const stable = cur === prev && cur !== "";
        prev = cur;
        return stable ? cur : "";
      },
      { timeout: 35_000, intervals: [500, 1000] },
    )
    .toMatch(/运行中|启动失败|已退出|重启中/);
});
