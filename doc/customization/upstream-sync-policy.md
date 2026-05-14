# Godot 官方同步策略

这个项目会长期跟随 Godot 官方更新，但不会无条件合并所有变化。同步时要先判断更新是否影响我们的 3D 定制目标。

## 同步目标

优先合入：

- 3D 渲染、材质、Shader、光照、后处理修复。
- 3D 导入管线，尤其是 glTF、FBX、网格、骨骼、动画。
- 3D 物理、导航、碰撞、性能优化。
- 编辑器稳定性、Inspector、SceneTree、资源系统、Project Manager。
- Windows、macOS、Android、iOS、Web 平台、构建系统、调试器、崩溃修复。
- 安全修复和数据损坏修复。

默认延后或跳过：

- 纯 2D 编辑器体验。
- 2D 物理、2D 导航、CanvasItem 专用功能。
- VR/XR 相关功能。
- 平台专属可选依赖相关功能，除非影响共享代码、桌面 3D 渲染、构建系统或编辑器稳定性。
- 与 3D 开发无关的大型示例、文档或资源更新。

## 同步流程

1. 拉取官方更新到临时分支。

2. 查看变更范围。

   推荐命令：

   ```powershell
   git diff --stat HEAD..upstream/master
   git diff --name-only HEAD..upstream/master
   ```

3. 对照 `removal-ledger.md`。

   如果官方更新触及已经软禁用或删除的路径，先查台账中的“官方同步策略”。

4. 给每类更新打标签。

   - `直接合入`：明显改善 3D 或编辑器基础能力。
   - `评估合入`：可能影响共享基础设施。
   - `保持裁剪`：只服务已裁剪功能。
   - `需要重测`：改动可能破坏构建或启动。

5. 合并后验证。

   最少验证：

   ```powershell
   scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no -j8
   .\bin\godot.windows.editor.dev.x86_64.console.exe --version
   ```

   手动验证：

   - 启动 Project Manager。
   - 打开一个 3D 项目。
   - 新建 Node3D 场景。
   - 添加 MeshInstance3D、Camera3D、Light3D。
   - 运行场景。

   如果本次同步触及 macOS、Metal、MoltenVK、打包、文件系统或桌面输入相关代码，还需要在 macOS 上补充构建和启动验证。

## 官方更新触及已裁剪区域时

不要直接丢弃。按下面顺序判断：

1. 这个更新是否修复安全、崩溃、数据损坏？
2. 是否修改共享基础类、构建脚本、资源导入、编辑器 UI？
3. 是否能只合入共享部分，继续保持功能裁剪？
4. 是否需要更新 `removal-ledger.md` 的同步策略？
5. 是否需要恢复某个曾经裁剪的功能？

如果答案不清楚，先保留为评估项，不要在同步提交中混入大规模定制重写。

## 分支和提交建议

- `upstream-sync/YYYY-MM-DD`：只做官方同步和冲突解决。
- `custom/prune-xxx`：只做一个裁剪主题。
- `custom/docs-xxx`：只改规则和记录。

提交粒度：

- 官方同步一个提交或一组提交。
- 冲突解决单独提交。
- 定制裁剪单独提交。
- 文档台账更新可以和对应裁剪同提交，但不能缺失。
