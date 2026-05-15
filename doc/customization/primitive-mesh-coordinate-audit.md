# 内置 3D 网格坐标迁移记录

本文记录内置 `PrimitiveMesh` 从 Godot 旧坐标习惯迁移到当前 3D 坐标系的处理方式。

当前项目 3D 坐标语义：

- `+X`：右
- `+Y`：前
- `+Z`：上

## 已处理

- `PlaneMesh`：默认朝向改为 `FACE_Z`，新建 Plane 默认铺在 `X/Y` 平面，法线朝 `+Z`，适合作为地面。
- `QuadMesh`：默认朝向改为 `FACE_Y`，继续作为竖向面片使用。
- `Sprite3D` / `SpriteBase3D`：默认 `axis` 改为 `Vector3::AXIS_Y`，继续作为竖向 2D 贴片使用。
- `CapsuleMesh`、`CylinderMesh`、`SphereMesh`、`TorusMesh`：生成后统一从旧内置网格坐标转换到当前坐标，默认高度转到 `Z` 轴。
- `BoxMesh`、`PrismMesh`：对外 `size` 按 `X 宽、Y 深、Z 高` 理解，内部先转给旧算法，再统一转换输出顶点、法线和切线。

## 后续验证清单

每次继续推进坐标系相关改动后，至少手动检查：

- 新建 `MeshInstance3D`，分别选择 `PlaneMesh`、`QuadMesh`、`BoxMesh`、`CylinderMesh`、`CapsuleMesh`、`SphereMesh`、`TorusMesh`、`PrismMesh`。
- `PlaneMesh` 默认应水平铺在地面上。
- `QuadMesh` 和 `Sprite3D` 默认应是竖向面片。
- `CylinderMesh`、`CapsuleMesh`、非等比 `SphereMesh` 默认高度应沿 `+Z/-Z`。
- `BoxMesh(Vector3(x, y, z))` 应表现为 `x` 宽、`y` 深、`z` 高。
- 检查法线、背面剔除、贴图方向、切线方向和阴影是否跟随新坐标。

## 暂未完成

- 文档里与 `width/height/depth`、`top/bottom`、`axis` 相关的描述还需要继续全量清扫。
- `TubeTrailMesh`、`RibbonTrailMesh`、`TextMesh` 和粒子/调试辅助网格需要单独验证，它们不一定适合直接套用基础体规则。
- 导入器、碰撞形状调试网格、导航和物理可视化仍可能存在旧 `Y-up` 表述或假设，后续按现象逐项处理。
