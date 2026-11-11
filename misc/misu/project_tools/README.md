# GDScript 项目检查与格式化

这套命令行工具使用 Godot 自带的 GDScript 警告和编译检查，并用固定版本的 gdtoolkit 统一格式。对已有项目先保存并关闭编辑器，再显式应用配置。游戏运行只读取项目自己的 `project.godot`。

在本目录使用 Python 3.10 或更新版本执行以下命令。Windows 上 `$Engine` 可指向编辑器的 `.console.exe`，便于收集原生诊断。

```powershell
.\setup.ps1
$Python = Join-Path $PWD '.venv/Scripts/python.exe'
$Engine = 'D:/path/to/godot.windows.editor.x86_64.console.exe'
$Project = 'D:/path/to/game'

& $Python .\baize.py adopt --project $Project --engine $Engine
& $Python .\baize.py check --project $Project --engine $Engine
& $Python .\baize.py format --project $Project
```

- `adopt` 把 [policy.json](policy.json) 中的警告值写入 `project.godot`。无类型声明、从 Variant 推断和不安全的访问、转换及调用会作为编译错误，`:=` 的静态类型推断仍可使用。Godot 原有的目录警告规则照常生效，默认规则排除 `res://addons`。Godot 的 ConfigFile 会重新序列化配置文件。
- `check` 先执行原生项目导入，可能更新导入文件和缓存。随后检查实际生效的警告配置，用 Godot `--check-only` 逐个检查脚本，再检查格式。报告包含目录警告规则。检查失败返回非零退出码，可通过 `--output report.json` 保存结果，或从 CI 调用 `ci_check.ps1`。
- `format` 使用 gdtoolkit 4.5.0，以 100 列、Tab、UTF-8 和 LF 写回 GDScript。工具会先检查所有目标文件的语法树、注释和格式稳定性。任一文件格式化失败时，本次不会写回脚本。格式化和逐脚本检查会跳过隐藏目录、`.gdignore` 目录和嵌套的 Godot 项目。
