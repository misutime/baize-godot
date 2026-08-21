# baize-godot 排障记录（Troubleshooting）

> 本文件是 baize-godot fork 的**故障排查积累**，按时间倒序追加。
> 每条：现象 → 排查 → 根因 → 修复 → 验证。持续积累，方便以后快速定位同类问题。
> 原则：每次解决一个真问题，把"排查思路 + 根因 + 修复"记下来，不重复踩坑。

---

## 2026-08-21：GUI 编辑器弹窗 "Failed to load .NET runtime"

### 现象
双击 `bin/godot.windows.editor.dev.x86_64.mono.exe`（GUI 编辑器）→ 弹窗：
"Failed to load .NET runtime, no compatible version was found. Please install the .NET SDK 8.0 or later"。
但**命令行启动同一 exe 正常**（headless / GUI + --path 都成功）。

### 排查链
1. **先查配置**：mono 程序集已切 net11.0，runtimeconfig.json 的 version 和 rollForward 是否正确？
   - 发现 `GodotPlugins.runtimeconfig.json` 是 `rollForward: LatestMajor` + `version: 11.0.0-preview.7`；
2. **怀疑 preview 版本匹配**：改 `RollForward LatestMajor → latestPatch`（FORK-CUSTOM，[.NET 文档](https://github.com/dotnet/runtime/blob/main/docs/design/features/roll-forward-on-no-candidate-fx.md)：LatestMajor 跳过 preview 版本）→ 命令行测试**仍正常**，但用户双击**仍弹窗**；
3. **COREHOST_TRACE 诊断**：命令行启动 trace 显示 runtime 解析成功（`Framework reference resolved to 11.0.0-preview.7`）→ 配置没问题；
4. **矛盾点**：同一 exe 命令行成功、双击失败 → 怀疑**环境差异**（工作目录/环境变量/参数）；
5. **查环境变量**：用户级 `DOTNET_ROOT = C:\Users\Misu\AppData\Local\dnvm\dn`（DNVM 目录，只装了 .NET 10.0.11）→ **根因确认**。

### 根因
- **用户级 `DOTNET_ROOT` 指向 DNVM（.NET Version Manager）目录**，里面只有 .NET 10 运行时；
- **Explorer 双击继承用户级环境变量**，Godot 的 `find_hostfxr()` 优先用 `DOTNET_ROOT` 指定的根（DNVM），
  加载 hostfxr 10.0.11，在该根下找不到 `Microsoft.NETCore.App 11.0.0-preview.7` → hostfxr 初始化失败（错误码 -2147450730）→ 弹窗；
- **命令行启动不继承用户级 DOTNET_ROOT**（shifu 测试环境无此变量），走系统 `C:\Program Files\dotnet` → 找到 .NET 11 → 成功。

### 修复
1. 清除用户级 `DOTNET_ROOT` / `DOTNET_ROOT_X64`：
   ```powershell
   [Environment]::SetEnvironmentVariable('DOTNET_ROOT', $null, 'User')
   [Environment]::SetEnvironmentVariable('DOTNET_ROOT_X64', $null, 'User')
   ```
2. **删除 DNVM**（不需要它管理 .NET 版本，删 778.7 MB）：
   ```powershell
   Remove-Item "$env:USERPROFILE\AppData\Local\dnvm" -Recurse -Force
   ```
3. **注销并重新登录 Windows**（Explorer 缓存的旧环境变量需重启才刷新）。

### 验证
- 系统 dotnet 完好（.NET 11 preview + 10 SDK）；
- 无 DNVM 残留（PATH/其他位置）；
- 用户级 DOTNET_ROOT 已清空；
- （用户注销重登后双击应正常）。

### 经验
- **同一程序命令行成功、双击失败 → 优先查用户级环境变量**（Explorer 继承用户级，命令行不继承）；
- **Godot 的 .NET 运行时探测**：`find_hostfxr()` 优先用 `DOTNET_ROOT`，其次 nethost 逻辑找系统安装——
  用户级 DOTNET_ROOT 指向错误位置会劫持运行时加载；
- **COREHOST_TRACE=1 + COREHOST_TRACEFILE=路径** 是 .NET 运行时加载的黄金诊断工具；
- **preview 版本的 rollForward 匹配**：`LatestMajor` 跳过 preview，需 `latestPatch`（见 `.NET-11-Preview-升级正式版清单.md` 2b）。

---

## 模板（新问题用）

### 现象
（描述用户看到什么）

### 排查链
1. （逐步排查步骤 + 工具）

### 根因
（一句话根因）

### 修复
（可执行命令/步骤）

### 验证
（如何确认已修复）

### 经验
（可复用的排查思路/工具）
