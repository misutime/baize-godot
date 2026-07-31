# EasyBoneMap

`EasyBoneMap` 是一个独立的离线骨骼分析工具层，当前用于分析已经带有骨骼和蒙皮的 GLB 模型。

当前阶段不接入 Pi/OMP，不修改 Godot 导入配置，也不生成最终 BoneMap。目标是先把模型事实和骨骼结构分析清楚。

## 当前流程

```text
GLB
  ↓
analyze_skeleton.py
  ↓
analyzer.pipeline
  ├── read_glb_facts()
  │     ↓
  │   analyzer.glb_reader.read_glb()
  │
  └── analyze_facts()
        ├── analyzer.skeleton_graph.analyze_skeleton_graph()
        └── 蒙皮影响汇总
  ↓
紧凑骨骼分析报告
```

## 目录结构

```text
easy_bonemap/
├── analyze_skeleton.py       # 主入口：输出紧凑骨骼分析报告
├── analyzer/
│   ├── pipeline.py           # 分析流程和函数编排
│   ├── glb_reader.py         # 纯事实 GLB 读取，不做语义判断
│   ├── skeleton_graph.py     # 父子关系、分支和深度分析
│   └── __init__.py
├── output/                   # 分析输出
└── README.md
```

## 运行紧凑分析

在 `baize-godot` 根目录运行：

```bash
python tools/easy_bonemap/analyze_skeleton.py \\
  D:/misutime/104_game/hades/entities/characters/hero/visual/SpiderGwen.glb \\
  -o tools/easy_bonemap/output/SpiderGwen.json
```

Windows PowerShell 也可以写成一行：

```powershell
python tools/easy_bonemap/analyze_skeleton.py "D:/misutime/104_game/hades/entities/characters/hero/visual/SpiderGwen.glb" -o "tools/easy_bonemap/output/SpiderGwen.json"
```

当前 SpiderGwen 输出约为：

```text
217 行
4922 字节
```

这个报告适合人工查看或作为后续 AI 判断的输入，不包含完整逐顶点蒙皮数组。

## 生成身体与四肢候选

使用 `--candidates` 输出方案 A2 的确定性候选报告：

```powershell
python tools/easy_bonemap/analyze_skeleton.py "D:/misutime/104_game/hades/entities/characters/hero/visual/SpiderGwen.glb" `
  -o "tools/easy_bonemap/output/SpiderGwen.json" `
  --candidates "tools/easy_bonemap/output/SpiderGwen.candidates.json"
```

当前候选范围：

```text
Root / Hips / Spine / Chest / Neck / Head
左右 UpperArm / LowerArm / Hand
左右 UpperLeg / LowerLeg / Foot
```

候选报告只保留拓扑、位置、方向、长度、对称和蒙皮特征分数，并输出证据、置信度和 `Unknown/Ambiguous` 状态。它不会生成最终 BoneMap，也不会修改模型或 Godot 导入配置。

候选生成实现位于 `analyzer/candidate_generation.py`，主流程通过 `--candidates` 写出独立 JSON。

## 输出内容

当前报告包含：

```text
format
source
asset
mesh_count
skin_count
skeleton
skinning
next_stage
facts_retained_in_memory
```

`skeleton` 部分包括：

```text
node_count
root_nodes
leaf_count
branch_nodes
max_depth
```

`branch_nodes` 只表示结构事实，例如：

```json
{
  "name": "Bone016",
  "parent_index": 33,
  "children": [15, 19, 23, 27, 31],
  "child_count": 5,
  "depth": 8
}
```

这表示 `Bone016` 有五个子分支，但当前工具不会直接断言它是手掌或手部骨骼。

## 调试完整事实数据

如果需要排查 GLB Reader，可以使用主入口的 `--debug-facts`：

```powershell
python tools/easy_bonemap/analyze_skeleton.py "D:/misutime/104_game/hades/entities/characters/hero/visual/SpiderGwen.glb" `
  --debug-facts "tools/easy_bonemap/output/SpiderGwen.debug.json" `
  -o "tools/easy_bonemap/output/SpiderGwen.json"
```

该模式会在一次读取中完成：

```text
read_glb_facts()
    ├── 写出完整事实调试文件
    └── 使用内存中的 facts 继续生成紧凑分析报告
```

完整事实文件可能较大，只用于调试，不应作为常规 AI 输入。

## 纯事实读取原则

`analyzer/glb_reader.py` 只负责读取 GLB 中存在的数据：

```text
GLB header/chunks
node name
parent/children
local transform
skin joints
inverse bind matrices
mesh attributes
JOINTS_0
WEIGHTS_0
per-joint skinning summary
```

它不负责：

```text
识别 Hips
识别手掌
识别手指
判断左右
生成 BoneMap
修改模型
```

语义分析应放在后续的 candidate generation 阶段。

## 当前 SpiderGwen 事实

当前输入：

```text
D:/misutime/104_game/hades/entities/characters/hero/visual/SpiderGwen.glb
```

已读取到：

```text
Mesh: 1
Skin: 1
Node: 77
Animation: 0
```

结构分析中可见：

```text
Bone016 有 5 个子分支
Bone016(mirrored) 有 5 个子分支
Spine upper 是多个身体分支的汇合点
root hips 同时连接脊柱和骨盆方向的分支
```

这些只是后续语义判断的结构证据，尚未生成任何 BoneMap。

## 下一步

当前已完成身体主干和四肢候选生成，下一阶段按方案 A3 独立处理手掌和五指：

```text
candidate generation
    ↓
hand analysis
    ├── palm
    ├── thumb
    └── index / middle / ring / little chains
```

之后再处理眼睛、Jaw、辅助骨、映射验证和 BoneMap 输出。
