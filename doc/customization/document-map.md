# 定制文档结构

这个文件是 `doc/customization/` 的固定索引。以后新增、删除、改名文档时，必须同步更新这里。

| 路径                                   | 作用                                                         | 什么时候读                                                 |
| -------------------------------------- | ------------------------------------------------------------ | ---------------------------------------------------------- |
| `README.md`                            | 定制文档入口，说明目录目标、当前阶段和最短构建命令。         | 第一次进入 `doc/customization/` 时读。                     |
| `document-map.md`                      | 固定文档结构索引，用一句话说明每个文档的功能和目的。         | 找文档、加文档、重命名文档前读。                           |
| `customization-rules.md`               | 记录 3D 定制引擎的总目标、裁剪原则、功能分级和阶段计划。     | 准备裁剪功能、调整项目方向、判断某功能是否该保留时读。     |
| `removal-ledger.md`                    | 删除、禁用、隐藏功能的台账，记录理由、影响、回滚和同步策略。 | 每次软禁用、硬裁剪、恢复功能前后都要读和更新。             |
| `upstream-sync-policy.md`              | 记录同步 Godot 官方更新时的合入、跳过和重测规则。            | 拉取官方更新、处理冲突、判断已裁剪区域是否要合入修复时读。 |
| `godot-default-minus-z-forward-guide.md` | 用屏幕外观察者的心智模型解释 Godot 官方默认 `-Z` 前方。       | 给初学者讲官方文档、教程和旧代码里的默认坐标系时读。       |
| `left-handed-y-up-coordinate-plan.md`  | 记录 Y-Up、`+Z` 前坐标系定制的目标、范围、风险和验证方法。   | 修改 3D 坐标方向、排查前后方向兼容问题时读。               |
| `coordinate-boundaries.md`             | 记录内部 `+Z` 坐标系和外部格式、裁剪模块之间的边界策略。     | 修改 glTF/FBX、旧资源迁移、XR 裁剪相关边界时读。           |
| `build-profiles.md`                    | 记录构建 profile、软裁剪 profile、脚本 preset 和构建基线。   | 修改 SCons profile、切换构建方式、验证软裁剪影响时读。     |
| `getting-started-windows.md`           | Windows 从 clone 到第一次编译运行的操作步骤。                | 新机器配置、首次编译、排查 Windows 工具链时读。            |
| `misc/customization/build-windows.ps1` | Windows 构建脚本，用 preset 包装常用 SCons profile。         | 日常构建、避免手写长命令时使用。                           |
| `misc/customization/scons-profiles/`   | 可版本管理的 SCons 构建配置目录。                            | 新增或调整正式构建基线、软裁剪实验配置时使用。             |

## 维护规则

- 新增文档前，先确认它不能自然并入现有文档。
- 新增文档后，必须在本文件补一行。
- 如果一个文档开始承担两个以上不相干职责，优先拆分，并更新本文件。
- `doc/customization/` 放说明性文档；会参与构建的脚本和 profile 放 `misc/customization/`。
