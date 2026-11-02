#!/usr/bin/env python3
"""把目录镜像到目标位置，优先使用硬链接。

用于打包时复制 Python 运行时：同一 NTFS 卷上创建硬链接几乎不耗时间，
也不会重复占用磁盘；跨卷或文件系统不支持硬链接时自动回退为复制。

用法:
  python hardlink_tree.py <src> <dst> [--exclude 相对路径]...
      [--exclude-name 目录名]... [--exclude-ext .lib]...
"""

import argparse
import os
import shutil
import sys
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description="硬链接镜像目录")
    parser.add_argument("src")
    parser.add_argument("dst")
    parser.add_argument("--exclude", action="append", default=[], help="排除的相对目录")
    parser.add_argument("--exclude-name", action="append", default=[], help="按名称排除目录")
    parser.add_argument("--exclude-ext", action="append", default=[], help="排除的文件扩展名")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    src = Path(args.src).resolve()
    dst = Path(args.dst)
    if not src.is_dir():
        print(f"源目录不存在: {src}", file=sys.stderr)
        return 2

    excluded = [(src / item).resolve() for item in args.exclude]
    excluded_names = set(args.exclude_name)
    excluded_exts = {item.lower() for item in args.exclude_ext}

    linked = 0
    copied = 0
    skipped_dirs = 0
    skipped_files = 0

    for root, dirs, files in os.walk(src):
        root_path = Path(root)
        kept = []
        for name in dirs:
            full = (root_path / name).resolve()
            if name in excluded_names or any(full == item or item in full.parents for item in excluded):
                skipped_dirs += 1
                continue
            kept.append(name)
        dirs[:] = kept

        relative = root_path.relative_to(src)
        target_dir = dst / relative
        target_dir.mkdir(parents=True, exist_ok=True)

        for name in files:
            if Path(name).suffix.lower() in excluded_exts:
                skipped_files += 1
                continue
            source_file = root_path / name
            target_file = target_dir / name
            try:
                os.link(source_file, target_file)
                linked += 1
            except OSError:
                shutil.copy2(source_file, target_file)
                copied += 1

    print(
        f"镜像完成: 硬链接 {linked}，复制 {copied}，"
        f"跳过目录 {skipped_dirs}，跳过文件 {skipped_files}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
