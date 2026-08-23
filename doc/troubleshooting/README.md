# sola3d-godot 排障记录（Troubleshooting）

> 本文件是 sola3d-godot fork 的**故障排查积累**，按时间倒序追加。
> 每条：现象 → 排查 → 根因 → 修复 → 验证。持续积累，方便以后快速定位同类问题。
> 原则：每次解决一个真问题，把"排查思路 + 根因 + 修复"记下来，不重复踩坑。

---

## 2026-08-22：Godot 编辑器 F5 报 MSB4236，命令行构建正常

### 现象
`godot-slice` 在命令行执行完整的 `dotnet build` 成功，但编辑器 F5 调用同一命令时，
`Godot.NET.Sdk/4.8.0-dev/Sdk/Sdk.props(40)` 无法解析 `Microsoft.NET.Sdk`；无 `ProjectReference` 的 `p15-check` 曾能通过。

### 根因
失败发生在根项目导入 `Microsoft.NET.Sdk` 时，早于 `ProjectReference` 图求值，因此与 Sola3d.Ecs、Shooter.Gameplay、
`GodotFloat64` 和 net11 项目引用无关。失败编辑器进程中的 `MSBuildSDKsPath` 与 `MSBUILD_EXE_PATH`
分别缺少目录分隔符（如 `...26381.103Sdks`）；GodotTools 启动 `dotnet` 子进程时原样继承了这些为进程内
MSBuild 项目求值设置的变量，覆盖了 dotnet CLI 自身的 SDK 定位。

### 修复
`BuildSystem` 启动 `dotnet build/publish` 前，按大小写不敏感方式移除 `MSBUILD_EXE_PATH`、
`MSBuildExtensionsPath`、`MSBuildSDKsPath`，让 dotnet CLI 按自己的 SDK 选择结果重新设置；不修改任何 csproj、
ProjectReference、Friflo 或 Godot.NET.Sdk 逻辑。

### 验证
- 在故意注入同样错误 MSBuild 变量的环境中，`--build-solutions` 对 `godot-slice` 和 `p15-check` 均 exit 0；
- 编辑器实际命令包含 `-p:GodotTargetPlatform=windows -p:GodotFloat64=true`，构建成功；
- `shooter-poc`、`ecsworld-smoke` 均 `failures=0`，`run_godot_slice_e2e.ps1` 通过。

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
## 2026-08-21：C# EditorPlugin 插件加载失败 "无法加载附加插件脚本"

### 现象
项目设置 → 插件 → 勾选启用 C# EditorPlugin → 报错：
"无法从路径 res://addons/p15_plugin/plugin.cs 加载附加插件脚本：该脚本可能存在代码错误。正在禁用...以防止进一步出错"。
但 `dotnet build` 成功（插件代码本身编译无错）。

### 排查链
1. **先查编译**：dotnet build 通过——代码没问题；
2. **查 headless 行为**：headless 打开编辑器，插件 `_EnterTree` 不打印（未加载）；
3. **查上游类查找逻辑**：`modules/mono/csharp_script.cpp:394` 发现
   `String class_name = p_path.get_file().get_basename()`——**C# 脚本的类名必须匹配文件名**；
4. **根因确认**：`plugin.cs` 里的类名是 `P15Plugin` ≠ 文件名 `plugin` → 按文件名找不到类 → 加载失败。

### 根因
**Godot C# 按文件名找类**（`csharp_script.cpp` 用 `p_path.get_file().get_basename()` 做类名）——
`plugin.cs` 必须含 `class plugin`，类名 `P15Plugin` 不匹配导致无法实例化。

### 修复
改类名匹配文件名：`public partial class plugin : EditorPlugin`（不是 `P15Plugin`）。
上游模板的 `_CLASS_` 占位符就是文件名，天然匹配。

### 验证
headless 打开编辑器 → `p15-plugin: EditorPlugin 加载成功 (P1.5)` + 卸载（完整生命周期）。

### 经验
- **C# 脚本/插件类名必须与文件名完全一致**（大小写敏感）——这是 Godot C# 的核心约定；
- 创建 EditorPlugin 时用上游模板（类名=文件名），不要自定义类名；
- headless 模式**能**验证 EditorPlugin 生命周期（_EnterTree/_ExitTree 会打印）——之前以为 headless 不加载插件是误判。

---

## 2026-08-21：Android 条件 TFM（net9）被编辑器自动删除

### 现象
项目 csproj 里的 Android 条件 `TargetFramework net9.0`（H6 硬约束：Android 导出模板 jar 库未对齐 net11 前保持 net9）
在编辑器打开项目后被自动删除，只剩主 TFM net11.0。

### 排查链
1. **发现**：p15-check.csproj 的 android net9 条件消失（git diff 显示被删）；
2. **定位**：`GodotSharpEditor.ApplyNecessaryChangesToSolution`（csproj 存在时每次打开调用）→
   `ProjectUtils.UpgradeProjectIfNeeded` → `EnsureTargetFrameworkMatchesMinimumRequirement`；
3. **根因确认**：该方法遍历条件 TargetFramework，`tfmVersion <= minTfmVersion`（net9 ≤ net11）时收集到
   `propertiesToChange`；主 TFM 不高于最小要求时**删除**条件属性——上游假设"主 TFM 够新就不需要条件 TFM"，
   与我们的 H6（Android 必须 net9）冲突。

### 根因
**上游 `EnsureTargetFrameworkMatchesMinimumRequirement` 会删除版本 ≤ 最小要求的条件 TFM**，
破坏 H6 硬约束（Android 保持 net9 是主动决策，不是"该升级"）。

### 修复
`ProjectUtils.cs`：收集 `propertiesToChange` 时**跳过 Android 条件属性**
（`property.Condition.Contains("android")` → continue），主 TFM 升级到更新版本时才评估。

### 验证
编辑器打开项目后，csproj 的 `android net9.0` 条件保留。

### 经验
- **上游"自动升级"逻辑可能破坏我们的主动决策**（H1-H6 硬约束）——凡涉及 TFM/版本自动修改处需审查；
- 排查"配置被改动"类问题：`git diff` 定位改动 → 回溯编辑器打开时的 `ApplyNecessaryChangesToSolution` 链。

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
