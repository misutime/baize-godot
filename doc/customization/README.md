# 3D 定制引擎文档入口

这个目录记录我们基于 Godot 做 3D 专用定制引擎的长期规则、裁剪计划、删除台账和同步策略。

目标不是一次性把 Godot 删到最小，而是建立一套可维护的流程：每一次裁剪都知道为什么做、删了什么、怎么验证、以后官方更新时怎么判断是否合入。

## 文档结构

固定文档结构见 `document-map.md`。以后新增、删除、改名文档时，必须同步更新这个索引。

## 当前阶段

当前处于第 0 阶段：建立规则，不做大规模源码删除。

允许做的事情：

- 用 SCons 参数关闭额外依赖。
- 建立 build profile / editor feature profile。
- 记录候选裁剪清单。
- 小范围验证某个模块是否能禁用。

暂不做的事情：

- 直接删除 `scene/2d`、`servers/physics_2d`、`editor/plugins` 等大目录。
- 改 Godot 核心类型系统来隐藏类。
- 为了减少体积牺牲 3D 编辑器正常启动。

## 推荐第一条构建命令

```powershell
scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no -j8
```

这条命令适合 Windows 上的早期开发：保留编辑器和 3D 运行能力，先跳过 D3D12、AccessKit、ANGLE 的额外依赖。macOS 也是一等目标平台，后续需要补充 macOS 构建基线。
