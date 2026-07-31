# Normalized Skeleton Graph

第一阶段工具：从带骨骼的 GLB/GLTF 中提取真实 joints，正确计算完整父子 Transform，并输出归一化 Skeleton Graph。

## 当前目标

```text
GLB/GLTF
  ↓
Khronos glTF Validator
  ↓
pygltflib
  ↓
完整 Matrix/TRS 世界 Transform
  ↓
skins[].joints 骨骼提取
  ↓
Normalized Skeleton Graph
```

FBX 使用 Blender headless 转换为临时 GLB，后续完全复用同一套 GLTF Transform、joints 提取和归一化逻辑：

```text
FBX
  ↓
Blender CLI 临时转换 GLB
  ↓
Khronos glTF Validator
  ↓
同一套 Normalized Skeleton Graph 流程
```

原始 FBX 不会被修改，报告的 `source` 仍然是原始 FBX 路径。

本阶段不做：

- AI 骨骼语义判断；
- 最终 BoneMap；
- 修改原始模型或 Godot 导入配置；
- 从裸 Mesh 生成新骨架；
- 强行补齐 Godot 56 根实际骨骼。

Godot `SkeletonProfileHumanoid` 的 56 个槽位是后续匹配目标，不等于模型实际骨骼数量。

## 运行

默认使用官方 Khronos Validator 预检。

GLB/GLTF 输入：

```bash
python tools/easy_bonemap/extract_skeleton.py \
  /path/to/character.glb \
  -o tools/easy_bonemap/output/character.skeleton.normalized.json
```

FBX 输入需要可用的 Blender：

```bash
python tools/easy_bonemap/extract_skeleton.py \
  /path/to/character.fbx \
  -o tools/easy_bonemap/output/character.skeleton.normalized.json
```

Blender 查找顺序：`EASY_BONEMAP_BLENDER`、PATH 中的 `blender`、macOS 默认应用路径。

如果明确需要跳过 Validator：

```bash
python tools/easy_bonemap/extract_skeleton.py \
  /path/to/character.glb \
  --skip-validator \
  -o /tmp/character.skeleton.normalized.json
```

## 输出契约

顶层字段：

```text
format
source
bone_count
bones
roots
degeneracies
warnings
```

每根骨骼包含：

```text
index                 # GLTF node index
name                  # 仅用于来源追溯，不参与计算
parent                # joint 子集中的父节点，根为 -1
local                 # matrix 或 TRS
world_position
normalized_position
local_to_world
parent_edge
child_edges
depth
world_rotation
world_scale
```

骨骼集合只来自 `skins[].joints`。普通 Mesh、Camera、Light、Attachment 节点不会自动变成骨骼。支持多根 joints；`skin.skeleton` 只作为来源提示，不覆盖实际 parent 图。

## 归一化规则

- 原点：单根使用根节点位置，多根使用根节点几何中心；
- 尺度：所有 root-to-leaf 路径长度中的最大值；
- 位置：`(world_position - origin) / scale`；
- 长度：`raw_length / scale`；
- 方向：父子世界位置差的单位向量；
- 根节点没有伪造的方向和长度；
- 退化骨架输出 `degeneracies`，不使用静默的 `scale = 1` 回退。

整体平移和 uniform scale 不应改变归一化结果。

## 依赖

Python：

```bash
python -m pip install -r tools/easy_bonemap/requirements.txt
```

当前核心依赖：

- `pygltflib`：GLB/GLTF 结构读取；
- `numpy`：矩阵、向量和数值计算；
- Blender 5.x：FBX headless 导入和临时 GLB 导出。

Node 开发/CI 预检依赖：

```bash
cd tools/easy_bonemap
npm install
```

- `gltf-validator`：Khronos 官方 glTF 2.0 Validator。

## 参考

- Khronos glTF Skin Tutorial：<https://github.com/KhronosGroup/glTF-Tutorials/blob/main/gltfTutorial/gltfTutorial_020_Skins.md>
- Khronos glTF Validator：<https://github.com/KhronosGroup/glTF-Validator>
- glTF skeleton root 讨论：<https://github.com/KhronosGroup/glTF/issues/1270>
- Godot SkeletonProfileHumanoid：<https://docs.godotengine.org/en/4.7/classes/class_skeletonprofilehumanoid.html>
- Godot BoneMap：<https://docs.godotengine.org/en/4.7/classes/class_bonemap.html>

后续阶段再实现 Godot 56 Profile 的匿名几何匹配、候选映射、验证和 BoneMap 输出。
