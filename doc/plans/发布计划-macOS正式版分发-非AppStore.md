# 发布计划：macOS 正式版分发（非 App Store，GitHub 下载）

> **状态**：计划/参考文档（2026-08-02 编写）。当前为 dev 阶段（ad-hoc 签名），本文件记录**正式版分发**
> （GitHub 提供下载、用户手动安装）所需的签名与公证配置，**后期按需执行**。
>
> **结论先行**：非 App Store 分发需要 **Developer ID 签名 + hardened runtime + 公证（notarization）+ stapling**。
> 不涉及 App Sandbox（那是 App Store 专属）。

---

## 1. 为什么需要这些

用户从 GitHub 下载的文件带 `com.apple.quarantine` 标记 → 首次打开触发 **Gatekeeper** 检查：
- 无签名/无效签名 → "已损坏，无法打开"（或需右键→打开绕过）
- 有 Developer ID 签名但未公证 → "无法验证开发者"（macOS 10.15+ 强制公证）
- Developer ID + 公证通过 + stapled → 双击直接打开

**entitlements（allow-jit 等）与发布无关**——它是 Apple Silicon 上 Chromium 运行的硬性要求，
dev 与发布形态都有（当前已配置，保持不变）。

## 2. 前置条件

| 项 | 说明 |
|---|---|
| Apple Developer Program 账号 | 个人/组织，$99/年（获取 Developer ID 证书的前提） |
| Developer ID Application 证书 | 开发者后台创建，下载到登录钥匙串（identity 形如 `Developer ID Application: 公司名 (TEAMID)`） |
| Xcode 工具链 | `codesign` / `notarytool` / `stapler` / `spctl`（Xcode 自带） |
| 公证凭据 | Apple ID + App-specific password（不输主密码）+ Team ID |

参考：Apple 官方《Notarizing macOS software before distribution》
https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution

### 2.1 不买账号的替代方案与代价（决策参考）

| 方案 | 成本 | 用户打开体验 | 更新后 |
|---|---|---|---|
| 不签 / ad-hoc | 免费 | 首次：系统设置→隐私与安全→"仍要打开"（macOS 15/26 起**右键→打开已被移除**，只能走设置） | **每次更新重新批准**（ad-hoc 无稳定身份，每个构建=新应用；TCC 权限也重置） |
| 自签证书（非 Apple 信任） | 免费 | 同上 | 跨更新保留**无官方保证**（部分案例保留，不稳定） |
| Developer ID + 公证 | $99/年 | **双击直接打开** | 批准、权限均保留 |

结论：**不强制购买**——GitHub 内测/小众分发可用 ad-hoc 或自签，但需在发布说明写明
"首次打开需在隐私与安全中允许，更新后可能需重新允许"；正式发布要体面体验再上 Developer ID。

## 3. 需要签名的对象（本项目清单）

| 对象 | 当前状态 | 发布要求 |
|---|---|---|
| 编辑器二进制 `bin/godot.macos.editor.*`（或 .app bundle） | dev 构建 ad-hoc（链接器自动） | Developer ID 签名 |
| 5 个 CEF helper bundle（`CefViewWing*.app`） | ad-hoc + entitlements（stage 脚本做） | Developer ID 签名 + hardened runtime + 同 entitlements |
| `Chromium Embedded Framework.framework` | 链接器 ad-hoc | Developer ID 签名（含内部嵌套 dylib/Libraries） |

**签名顺序：内 → 外**（先 framework 内部，再 helper，最后编辑器主二进制）。
`codesign --deep` 仅用于 dev；正式发布逐层显式签名（`--deep` 可能漏嵌套代码导致公证失败）。

## 4. 签名配置

```sh
# 统一 identity（一个证书签全部）
IDENTITY="Developer ID Application: <公司名> (<TEAMID>)"

# helper bundle / framework（沿用现有 entitlements 三项：allow-jit、
# allow-unsigned-executable-memory、disable-library-validation）
codesign --force --sign "$IDENTITY" --options runtime --timestamp \
  --entitlements <规范化entitlements.plist> <bundle>

# 编辑器二进制
codesign --force --sign "$IDENTITY" --options runtime --timestamp <editor-binary>
```

要点：
- **`--options runtime`（hardened runtime）是公证的硬性要求**（macOS 10.14+ 公证拒绝未启用 hardened runtime 的二进制）。
- **`--timestamp` 必须**（公证需要安全时间戳）。
- **allow-jit 与 hardened runtime 的配合**：Apple 将 `com.apple.security.cs.allow-jit` 定义为 hardened runtime 的 JIT 例外（技术说明 TN3135：https://developer.apple.com/documentation/technotes/tn3135-on-mac-hardened-runtime）。Electron/Chromium 系应用（Electron 即用这组 entitlements）可正常通过公证——我们现有三项 entitlements 保持即可，无需为发布改动。
- **身份切换**：ad-hoc（`--sign -`）→ Developer ID 后，签名 DR（designated requirement）稳定——顺带消除"每次构建 CDHash 变化"问题（当前用 `--use-mock-keychain` 已不受钥匙串 ACL 影响，发布同样保留 mock keychain，与"持久化走 C++ 侧"架构决策一致）。

## 5. 公证流程（打包后）

```sh
# 1. 打包（zip 或 DMG；DMG 对用户更友好）
ditto -c -k --keepParent <app或编辑器> <产物>.zip

# 2. 提交公证（app-specific password）
xcrun notarytool submit <产物>.zip \
  --apple-id <APPLE_ID> --team-id <TEAMID> --password <APP_SPECIFIC_PASSWORD> \
  --wait   # 或 --output-format json 后 xcrun notarytool log <submission-id>

# 3. 通过后 staple（把票据钉进产物，离线可验）
xcrun stapler staple <产物>.zip

# 4. 验证
xcrun stapler validate <产物>.zip
spctl -a -vv --type execute <编辑器二进制>   # 应输出 accepted
```

## 6. 与本项目现状的差异点（实施时改动位置）

1. **stage_webview.py `patch_helper_plists`**：`codesign --sign -` → `--sign "$IDENTITY"` + 加 `--options runtime --timestamp`；identity 来源建议环境变量（如 `MAC_SIGN_IDENTITY`），未设置时保持 ad-hoc（dev 流程不变）。
2. **编辑器二进制签名**：现在 dev 构建不签（scons 输出即用）；发布构建需在打包前补一步签名。可放进 `build.py` 的发布预设或发布脚本（本项目暂无发布流水线，待建）。
3. **framework 签名**：与 helper 同批处理（stage 或发布脚本）。
4. **签名时机**：所有签名必须在**打包之前、最后完成**——公证后改动任何二进制都会使签名/票据失效。
5. **Intel (x86_64) 分发**：当前工具链拒绝交叉架构（SCsub 宿主校验），Intel 用户分发需另行处理（通用二进制或双包）——后续需求确认。
6. **CI 集成**（将来）：notarytool 凭据建议用 App Store Connect API key（免 app-specific password 存 CI）；keychain 解锁在 CI runner 上的标准做法见 Apple 文档。

## 7. 验证清单（发布前）

- [ ] `codesign --verify --deep --strict` 全部二进制（编辑器 + 5 helper + framework）通过
- [ ] `codesign -d --entitlements -` 确认 hardened runtime 下三项 entitlements 就位
- [ ] `spctl -a -vv --type execute` 输出 `accepted`
- [ ] `stapler validate` 通过
- [ ] **干净环境实测**：从 GitHub 下载 → 双击打开（验证 quarantine → Gatekeeper → 公证链路）
- [ ] 打开后 WebDock 正常（GPU、页面 200）

## 8. 注意事项 / 坑

- **公证后不可改动产物**：任何重签/修改都会失效，需重新签名+公证。
- **凭据安全**：notarytool 用 App-specific password（appleid.apple.com 生成），不泄露主密码；CI 用 API key 更稳。
- **DMG vs zip**：zip 简单；DMG 可定制布局/背景，用户观感更好，需额外工具（`hdiutil` 可做基础 DMG）。
- **allow-unsigned-executable-memory 与公证**：Electron 同款 entitlement 组合可过公证（实测生态广泛）；若公证报这类权限警告，以 notarytool log 的实际输出为准调整。
- **首次开发机自测**：本地跑 Developer ID 签名产物不受 quarantine 影响，真实验证仍需走下载/拷贝场景。
