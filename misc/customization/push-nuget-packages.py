#!/usr/bin/env python3
"""
把 Godot C# 构建出来的 NuGet 包推送到指定源。

注意：nuget.org 不允许重复上传同一个包版本，也不允许普通账号上传已归属他人的包 ID。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from zipfile import ZipFile


DEFAULT_SOURCE = "https://api.nuget.org/v3/index.json"
DEFAULT_MANIFEST = "doc/customization/nuget-publish-manifest.json"


def load_dotenv(path: Path) -> None:
    if not path.is_file():
        return

    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip().strip('"').strip("'")

        # 环境变量优先，.env 只补本地默认值。
        if key and key not in os.environ:
            os.environ[key] = value


def collect_packages(package_dir: Path, include_symbols: bool, package_prefix: str) -> list[Path]:
    patterns = ["*.nupkg"]
    if include_symbols:
        patterns.append("*.snupkg")

    packages: list[Path] = []
    for pattern in patterns:
        packages.extend(package_dir.glob(pattern))

    if package_prefix:
        packages = [package for package in packages if package.name.startswith(package_prefix)]

    return sorted(packages)


def file_sha256(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as package_file:
        for chunk in iter(lambda: package_file.read(1024 * 1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def should_skip_payload_entry(name: str) -> bool:
    return (
        name == ".signature.p7s" or
        name == "_rels/.rels" or
        name.startswith("package/services/metadata/core-properties/")
    )


def package_payload_sha256(path: Path) -> str:
    sha = hashlib.sha256()
    with ZipFile(path) as package_file:
        names = sorted(name for name in package_file.namelist() if not should_skip_payload_entry(name))
        for name in names:
            sha.update(name.encode("utf-8"))
            sha.update(b"\0")
            sha.update(hashlib.sha256(package_file.read(name)).digest())
    return sha.hexdigest()


def parse_package_id_and_version(package: Path) -> tuple[str, str]:
    suffixes = [".nupkg", ".snupkg"]
    name = package.name
    for suffix in suffixes:
        if name.endswith(suffix):
            name = name[: -len(suffix)]
            break

    marker = ".4."
    index = name.rfind(marker)
    if index == -1:
        raise ValueError(f"无法从包名解析版本：{package.name}")

    return name[:index], name[index + 1 :]


def load_manifest(path: Path) -> dict:
    if not path.is_file():
        return {"packages": []}
    return json.loads(path.read_text(encoding="utf-8"))


def save_manifest(path: Path, manifest: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def build_package_records(packages: list[Path]) -> list[dict]:
    records = []
    for package in packages:
        package_id, version = parse_package_id_and_version(package)
        records.append(
            {
                "id": package_id,
                "version": version,
                "file": package.name,
                "sha256": file_sha256(package),
                "payload_sha256": package_payload_sha256(package),
            }
        )
    return records


def validate_manifest(manifest: dict, records: list[dict]) -> str:
    published = {
        (package["id"], package["version"], package["file"]): package
        for package in manifest.get("packages", [])
    }

    has_error = False
    all_same = True

    for record in records:
        key = (record["id"], record["version"], record["file"])
        old_record = published.get(key)
        if old_record is None:
            all_same = False
            continue

        old_payload_sha256 = old_record.get("payload_sha256", old_record.get("sha256"))
        if old_payload_sha256 != record["payload_sha256"]:
            print(
                "包内容已变化，但包名和版本没有变化："
                f"{record['file']}\n"
                "请先递增 version.py 里的 status，例如 baize1 -> baize2，"
                "重新构建后再上传。",
                file=sys.stderr,
            )
            has_error = True

    if has_error:
        return "error"

    if all_same and records:
        print("当前包和发布清单记录完全一致，无需重复上传。")
        return "noop"

    return "upload"


def update_manifest(manifest: dict, records: list[dict]) -> dict:
    published = {
        (package["id"], package["version"], package["file"]): package
        for package in manifest.get("packages", [])
    }

    published_at = datetime.now(timezone.utc).isoformat()
    for record in records:
        key = (record["id"], record["version"], record["file"])
        published[key] = {**record, "published_at": published_at}

    manifest["packages"] = sorted(
        published.values(),
        key=lambda package: (package["id"], package["version"], package["file"]),
    )
    return manifest


def main() -> int:
    load_dotenv(Path(".env"))

    parser = argparse.ArgumentParser(description="Push Godot C# NuGet packages.")
    parser.add_argument(
        "--package-dir",
        default="bin/GodotSharp/Tools/nupkgs",
        help="nupkg 所在目录，默认是 bin/GodotSharp/Tools/nupkgs。",
    )
    parser.add_argument(
        "--source",
        default=DEFAULT_SOURCE,
        help=f"NuGet 源地址，默认是 {DEFAULT_SOURCE}。",
    )
    parser.add_argument(
        "--api-key",
        default=os.environ.get("NUGET_API_KEY", ""),
        help="NuGet API key。也可以用环境变量 NUGET_API_KEY。",
    )
    parser.add_argument(
        "--include-symbols",
        action="store_true",
        help="同时推送 .snupkg 符号包。默认只推送 .nupkg。",
    )
    parser.add_argument(
        "--package-prefix",
        default="Baize.",
        help="只上传指定前缀的包，默认是 Baize.。传空字符串可关闭过滤。",
    )
    parser.add_argument(
        "--skip-duplicate",
        action="store_true",
        help="遇到已存在的包版本时跳过。nuget.org 不允许覆盖已上传版本。",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="只打印将要上传的包，不实际执行 dotnet nuget push。",
    )
    parser.add_argument(
        "--manifest",
        default=DEFAULT_MANIFEST,
        help=f"发布清单路径，默认是 {DEFAULT_MANIFEST}。",
    )
    parser.add_argument(
        "--no-manifest-check",
        action="store_true",
        help="跳过发布清单校验。只在手动排查问题时使用。",
    )

    args = parser.parse_args()

    package_dir = Path(args.package_dir).resolve()
    if not package_dir.is_dir():
        print(f"找不到包目录：{package_dir}", file=sys.stderr)
        return 1

    packages = collect_packages(package_dir, args.include_symbols, args.package_prefix)
    if not packages:
        print(f"没有找到可上传的 NuGet 包：{package_dir}", file=sys.stderr)
        if args.package_prefix:
            print(f"当前包名前缀过滤：{args.package_prefix}", file=sys.stderr)
        return 1

    if not args.dry_run and not args.api_key:
        print("缺少 NuGet API key。请设置 NUGET_API_KEY，或传入 --api-key。", file=sys.stderr)
        return 1

    manifest_path = Path(args.manifest)
    manifest = load_manifest(manifest_path)
    records = build_package_records(packages)

    if not args.no_manifest_check:
        validate_result = validate_manifest(manifest, records)
        if validate_result == "error":
            return 1
        if validate_result == "noop":
            return 0

    print(f"NuGet 源：{args.source}")
    print(f"包目录：{package_dir}")
    if args.package_prefix:
        print(f"包名前缀过滤：{args.package_prefix}")
    print("将处理这些包：")
    for package in packages:
        print(f"  {package.name}")

    if args.dry_run:
        return 0

    for package in packages:
        command = [
            "dotnet",
            "nuget",
            "push",
            str(package),
            "--source",
            args.source,
            "--api-key",
            args.api_key,
        ]
        if args.skip_duplicate:
            command.append("--skip-duplicate")

        print(f"\n上传：{package.name}")
        subprocess.run(command, check=True)

    save_manifest(manifest_path, update_manifest(manifest, records))
    print(f"\n已更新发布清单：{manifest_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
