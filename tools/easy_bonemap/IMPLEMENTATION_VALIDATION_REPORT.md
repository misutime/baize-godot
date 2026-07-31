# Normalized Skeleton Graph 实现与验证报告

> 用途：记录第一阶段实现、自动化测试、真实 GLB 数据校验和 Blender 独立核验结果，供后续 PR 审查。
>
> 阶段：Normalized Skeleton Graph
>
> 结论：通过。两个真实人体 GLB 的骨骼数量、父子拓扑、世界位置和归一化空间数据均通过独立校验。

---

## 1. 验证范围

本阶段只验证：

```text
GLB/GLTF
  ↓
真实 joints 提取
  ↓
完整父子 Transform
  ↓
Skeleton Graph
  ↓
Root-relative + 尺度归一化
```

不在本阶段验证：

- Godot 56 Profile 的语义匹配；
- BoneMap 自动生成；
- 动画重定向结果；
- 从裸 Mesh 生成新骨架；
- AI 语义判断。

输出的是模型真实骨骼图，不是强行补齐的 56 根标准骨骼。

---

## 2. 实现内容

### 2.1 Transform 数学

文件：`analyzer/transform_math.py`

关键接口：

- `trs_matrix()`：构造 GLTF `T * R * S` 矩阵；
- `local_matrix()`：`node.matrix` 存在时优先使用，否则使用 TRS；
- `world_matrices()`：递归计算完整父子世界矩阵；
- `decompose_matrix()`：提取世界位置、旋转和缩放；
- `unit_vector()`：计算父子方向；
- `json_vector()`：稳定 JSON 数值输出。

关键约束：

```text
world_matrix[node] = world_matrix[parent] * local_matrix[node]
```

不能用：

```text
parent_position + local_translation
```

作为一般世界位置算法。

### 2.2 GLTF 读取

文件：`analyzer/gltf_reader.py`

使用依赖：

```text
pygltflib 1.16.5
numpy 2.5.1
```

职责：

- 读取 `.glb` / `.gltf`；
- 构造原始节点父关系；
- 保留 GLTF source index；
- 不做人体语义判断。

### 2.3 Skeleton Graph

文件：`analyzer/skeleton_graph.py:51`

接口：

```python
extract_normalized_skeleton_graph(document, source="")
```

骨骼集合只来自：

```text
skins[].joints
```

输出包含：

```text
index
name                         # 仅作为 provenance
parent
children / child_edges
local
world_position
normalized_position
local_to_world
depth
world_rotation
world_scale
parent_edge
```

普通 Mesh、Camera、Light 和 Attachment 不会自动进入骨骼集合。

支持：

- 多个 Skin 的 joints union；
- 多根骨骼；
- joint 子集父子关系；
- 非 joint 祖先参与世界 Transform；
- `skin.skeleton` 作为提示而不是强制根节点；
- 无 Skin、空 joints、越界 joints 的诊断输出。

### 2.4 归一化

归一化规则：

```text
origin = 单根 root 位置，或多根 root 的几何中心
scale  = 最大 root-to-leaf 路径长度
position_normalized = (world_position - origin) / scale
length_normalized   = raw_length / scale
```

保证：

- 整体平移不影响输出；
- 整体 uniform scale 不影响输出；
- 根节点归一化位置为 `[0, 0, 0]`；
- 最大 root-to-leaf 路径为 1；
- 根节点不伪造方向和长度；
- 退化骨架输出 diagnostics，不静默使用 `scale = 1`。

### 2.5 Godot Profile 元数据

文件：`analyzer/humanoid_profile.py:36`

已加入：

```text
SkeletonProfileHumanoid
bone_count = 56
root_bone = Root
scale_base_bone = Hips
Body / Face / LeftHand / RightHand 分组
父子关系
required 标记
```

该 Profile 当前只作为后续匹配的标准数据，不改变第一阶段真实骨骼图。

### 2.6 Validator

文件：`analyzer/validator.py:21`

使用官方 npm 包：

```text
gltf-validator@2.0.0-dev.3.10
```

CLI：

```text
extract_skeleton.py
```

默认执行 Validator；只有显式传入 `--skip-validator` 时才跳过。

---

## 3. 自动化测试

测试文件：

```text
tools/easy_bonemap/tests/test_skeleton_graph.py
```

执行命令：

```bash
python -m unittest discover -s tools/easy_bonemap/tests
```

结果：

```text
Ran 44 tests
OK
```

覆盖内容：

### Transform

- identity Transform；
- translation；
- quaternion rotation；
- uniform scale；
- non-uniform scale；
- 父节点旋转；
- 父节点非均匀缩放；
- 多级父子矩阵链；
- matrix 优先于 TRS；
- 不修改输入文档；
- Transform 输出可序列化。

### Skeleton Graph

- `skins[].joints` 精确筛选；
- 普通节点排除；
- 多 Skin union 和去重；
- 空 Skin；
- 无 Skin；
- 越界 joint index；
- joint 子集父子关系；
- 多根；
- 根节点不伪造 edge/length；
- parent/children 关系稳定。

### Normalization

- 全局平移不变；
- uniform scale 不变；
- 平移和 uniform scale 同时变化不变；
- 非 uniform 形状变化会被正确保留；
- 名称变化不影响几何数据；
- root-to-leaf 最大路径为 1；
- 根节点归一化位置为原点。

---

## 4. 真实 GLB 输入

### 4.1 SpiderGwen

输入：

```text
/Users/misu/misutime/102_games/kai-hades/entities/characters/hero/visual/SpiderGwen.glb
```

输出：

```text
tools/easy_bonemap/output/validation/SpiderGwen.normalized.json
```

结果：

```text
node_count: 77
skin_count: 1
joint_count: 75
roots: [74]
degeneracies: []
warnings: []
```

### 4.2 hero

输入：

```text
/Users/misu/misutime/102_games/kai-hades/source_assets/hero/hero.glb
```

输出：

```text
tools/easy_bonemap/output/validation/hero.normalized.json
```

结果：

```text
node_count: 274
skin_count: 1
joint_count: 272
roots: [271]
degeneracies: []
```

hero 存在三条零长度 joint edge：

```text
root → root_foot
root → root_hand
Bip001 → Bip001-Pelvis
```

这些边在源 GLB 的世界位置确实重合。它们被保留在 Graph 中，并作为 warning 输出：

```text
zero_length_joint_edge:271:267
zero_length_joint_edge:271:270
zero_length_joint_edge:260:259
```

这不是静默删除或伪造长度。它们可能是辅助/挂点骨骼，但语义判断留给后续 Profile Matcher。

---

## 5. Khronos Validator 结果

| 输入 | Errors | Warnings | Infos |
|---|---:|---:|---:|
| SpiderGwen | 0 | 1 | 1 |
| hero | 0 | 1 | 70 |

主要 Warning：

```text
NODE_SKINNED_MESH_NON_ROOT
```

含义：skinned mesh 节点不是场景根节点，父节点 Transform 不会影响实际蒙皮。该信息不表示 joints 或 inverse bind matrices 无效。

Info 主要是：

```text
UNUSED_OBJECT
```

涉及未使用的 `TEXCOORD_*` 属性，不影响骨骼图。

Validator 没有发现：

- GLB 格式错误；
- joint index 越界；
- inverse bind matrix accessor 错误；
- skin 资源引用错误；
- 二进制 buffer 错误。

---

## 6. 独立 Python Transform 校验

校验方法：

1. 使用 `pygltflib` 重新读取原始 GLB；
2. 在独立校验代码中重新实现 node.matrix/TRS 读取；
3. 独立计算所有 node 的世界矩阵；
4. 对比 Graph 中的 `world_position` 和 `local_to_world`。

结果：

| 模型 | 最大世界位置误差 | 最大世界矩阵误差 |
|---|---:|---:|
| SpiderGwen | `4.99e-9` | `4.99e-9` |
| hero | `5.00e-9` | `5.00e-9` |

误差处于浮点计算和 JSON 舍入范围内。

---

## 7. inverseBindMatrices 校验

对每个 skin joint 计算：

```text
global_joint_transform * inverseBindMatrix
```

理想结果为单位矩阵。

| 模型 | 最大残差 | 中位数残差 |
|---|---:|---:|
| SpiderGwen | `1.02e-6` | `4.10e-7` |
| hero | `2.12e-6` | `3.10e-7` |

这证明 Graph 计算出的 joint 全局 Transform 与 GLB 自身 Bind Pose 数据一致。

---

## 8. 归一化不变量校验

### SpiderGwen

```text
max root-to-leaf path: 0.9999999999999999
root normalized position: [0.0, 0.0, 0.0]
zero-length edges: 0
finite world positions: true
max direction unit error: 5.63e-9
```

### hero

```text
max root-to-leaf path: 0.99999999
root normalized position: [0.0, 0.0, 0.0]
zero-length edges: 3, all reported as warnings
finite world positions: true
max direction unit error: 6.32e-9
```

---

## 9. Blender 独立核验

Blender：

```text
/Applications/Blender.app/Contents/MacOS/Blender
Blender 5.1.0
```

Blender 使用自己的 glTF Importer 重新读取原始 GLB，读取：

```text
Armature.data.bones
bone.head_local
bone.parent
```

GLTF 与 Blender 坐标空间转换：

```text
GLTF [x, y, z] → Blender [x, -z, y]
```

两边各自做 root-relative 和最大 root-to-leaf 尺度归一化后，再比较同名骨骼。

### SpiderGwen

```text
Blender bones: 75
Graph bones: 75
Shared bones: 75
Missing in Blender: 0
Extra in Blender: 0
Parent mismatches: 0
Max normalized head error: 4.924e-7
```

### hero

```text
Blender bones: 272
Graph bones: 272
Shared bones: 272
Missing in Blender: 0
Extra in Blender: 0
Parent mismatches: 0
Max normalized head error: 3.590e-7
```

### 可视化结果

三栏图含义：

```text
左栏：Blender Imported，蓝色
中栏：Normalized Graph，橙色
右栏：Overlay，蓝色 + 橙色
```

右栏的橙色 Graph 线加入了极小的深度偏移，仅用于让重合的两层颜色同时可见；数值比较没有使用这个偏移。

判断标准：

- 如果父子关系或位置错误，右栏会出现明显的分叉、双重骨骼或平行偏移；
- 当前两张图中，右栏没有可见的结构分叉；
- 左栏和中栏的头、脊柱、双臂、双手、双腿和手指结构一致；
- 数值最大误差同时小于 `5e-7`，因此视觉一致不是偶然的缩放效果。

图片：

- `tools/easy_bonemap/output/validation/SpiderGwen.skeleton.threepanel.png`
- `tools/easy_bonemap/output/validation/hero.skeleton.threepanel.png`

图片目录被 `.gitignore` 忽略，重新生成命令见下一节。

---

## 10. 复现命令

### 自动化测试

```bash
python -m unittest discover -s tools/easy_bonemap/tests
```

### 生成 SpiderGwen Graph

```bash
python tools/easy_bonemap/extract_skeleton.py \
  "/Users/misu/misutime/102_games/kai-hades/entities/characters/hero/visual/SpiderGwen.glb" \
  -o tools/easy_bonemap/output/validation/SpiderGwen.normalized.json
```

### 生成 hero Graph

```bash
python tools/easy_bonemap/extract_skeleton.py \
  "/Users/misu/misutime/102_games/kai-hades/source_assets/hero/hero.glb" \
  -o tools/easy_bonemap/output/validation/hero.normalized.json
```

### 使用 Blender 生成视觉核验图

```bash
/Applications/Blender.app/Contents/MacOS/Blender \
  --background \
  --python tools/easy_bonemap/scripts/verify_skeleton_blender.py \
  -- \
  "/path/to/model.glb" \
  "/path/to/model.normalized.json" \
  "/path/to/model.skeleton.threepanel.png"
```

### 使用 Blender 比较骨骼拓扑

```bash
/Applications/Blender.app/Contents/MacOS/Blender \
  --background \
  --python tools/easy_bonemap/scripts/compare_blender_topology.py \
  -- \
  "/path/to/model.glb" \
  "/path/to/model.normalized.json"
```

该脚本输出 Blender/Graph 骨骼数量、同名数量、根节点和 parent mismatch 数量。

Blender 核验的核心内容是：

- Blender 独立导入骨骼数量；
- 同名骨骼数量；
- parent 名称关系；
- root 数量；
- 每根骨骼归一化 head position 误差；
- 三栏视觉对照图。

---

## 11. PR 审查清单

### 实现

- [x] 使用 `pygltflib` 读取 GLB/GLTF；
- [x] 使用 NumPy 做矩阵和向量计算；
- [x] `node.matrix` 优先于 TRS；
- [x] 完整父子 Transform；
- [x] 只提取 `skins[].joints`；
- [x] 支持多根；
- [x] 不把普通节点当骨骼；
- [x] 输出 Root-relative 和尺度归一化数据；
- [x] Godot 56 Profile 元数据独立保存；
- [x] 输入异常显式报告。

### 自动化测试

- [x] 44 个行为测试通过；
- [x] 覆盖旋转和非均匀缩放；
- [x] 覆盖 matrix/TRS 优先级；
- [x] 覆盖 joints 筛选和多根；
- [x] 覆盖平移/uniform scale 不变量；
- [x] 覆盖退化数据。

### 外部数据核验

- [x] 两个真实 GLB Validator 无错误；
- [x] 独立 Python 世界矩阵误差小于 `5e-9`；
- [x] inverseBindMatrices 残差小于 `2.2e-6`；
- [x] Blender 骨骼数量完全一致；
- [x] Blender parent mismatch 为 0；
- [x] Blender 归一化 head 最大误差小于 `5e-7`；
- [x] 生成全身三栏可视化核验图。

---

## 12. 尚未验证的内容

本报告没有声称以下内容已完成：

- Godot Editor 导入后的 `Skeleton3D` 与 BoneMap 行为；
- Godot 动画重定向后姿势质量；
- 56 个 Profile 槽位的自动语义映射准确率；
- A-Pose/T-Pose 自动识别；
- 模型左右方向的语义判定；
- 裸 Mesh 自动生成骨架；
- 多种导出器和非 GLTF 格式兼容性。

这些属于下一阶段 Profile Matcher、BoneMap Emitter 和 Retargeting 验证范围。

当前阶段的结论仅是：

> **Normalized Skeleton Graph 已正确反映这两个 GLB 的真实骨骼拓扑和 Rest 空间关系，并且通过了独立 Python、Khronos Validator 和 Blender glTF Importer 三层验证。**
