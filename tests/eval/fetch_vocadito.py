#!/usr/bin/env python3
"""按 HTTP Range 从 Zenodo 的 vocadito.zip 中取测试片段，不下载完整压缩包。

vocadito 由 Bittner et al. 发布，采用 CC BY 4.0 许可：
  https://zenodo.org/records/5557945
  https://arxiv.org/abs/2110.05580

每个片段会取出：
  vocadito_N.wav
  vocadito_N_notesA1.csv
  vocadito_N_notesA2.csv

用法:
  python tests/eval/fetch_vocadito.py
  python tests/eval/fetch_vocadito.py --clips 1-8,10,14 --out-dir tests/real/vocadito
"""

import argparse
import concurrent.futures
import http.client
import os
import struct
import sys
import time
import urllib.error
import urllib.request
import zlib
from pathlib import Path

URL = "https://zenodo.org/record/5557945/files/vocadito.zip"
DEFAULT_CLIPS = "1-8,10,14"
RETRIES = 8


def parse_clips(text: str):
    clips = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            start, end = part.split("-", 1)
            clips.extend(range(int(start), int(end) + 1))
        else:
            clips.append(int(part))
    return sorted(set(clips))


def fetch(start: int, end: int, timeout: int = 300) -> bytes:
    """下载闭区间 [start, end]，失败时从已收到的位置续传。"""
    expected = end - start + 1
    data = bytearray()
    attempt = 0
    while len(data) < expected:
        attempt += 1
        if attempt > RETRIES:
            raise RuntimeError(
                f"range {start}-{end}: 重试 {RETRIES} 次仍失败，已收到 {len(data)}/{expected}"
            )
        current = start + len(data)
        request = urllib.request.Request(
            URL,
            headers={
                "Range": f"bytes={current}-{end}",
                "User-Agent": "tab_tool-eval/1.0",
            },
        )
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                while True:
                    chunk = response.read(65536)
                    if not chunk:
                        break
                    data.extend(chunk)
        except http.client.IncompleteRead as exc:
            data.extend(exc.partial or b"")
        except (urllib.error.URLError, TimeoutError, ConnectionError, OSError) as exc:
            print(f"  retry {attempt}/{RETRIES} at {len(data)}/{expected}: {exc}", flush=True)
            time.sleep(min(2 * attempt, 15))
    return bytes(data)


def read_central_directory():
    request = urllib.request.Request(URL, method="HEAD", headers={"User-Agent": "tab_tool-eval/1.0"})
    with urllib.request.urlopen(request, timeout=120) as response:
        total = int(response.headers["Content-Length"])
    tail_len = min(131072, total)
    tail = fetch(total - tail_len, total - 1)
    eocd = tail.rfind(b"PK\x05\x06")
    if eocd < 0:
        raise RuntimeError("找不到 ZIP EOCD")
    values = struct.unpack_from("<4s4H2LH", tail, eocd)
    cd_size, cd_offset = values[5], values[6]
    if cd_offset >= total - tail_len:
        begin = cd_offset - (total - tail_len)
        cd = tail[begin : begin + cd_size]
    else:
        cd = fetch(cd_offset, cd_offset + cd_size - 1)

    entries = {}
    pos = 0
    while pos + 46 <= len(cd) and cd[pos : pos + 4] == b"PK\x01\x02":
        values = struct.unpack_from("<4s6H3L5H2L", cd, pos)
        method, crc, comp_size = values[4], values[7], values[8]
        name_len, extra_len, comment_len = values[10], values[11], values[12]
        local_offset = values[16]
        name = cd[pos + 46 : pos + 46 + name_len].decode("utf-8", "replace")
        entries[name] = (method, crc, comp_size, local_offset)
        pos += 46 + name_len + extra_len + comment_len
    return entries


def extract(name, method, crc, comp_size, local_offset):
    header = fetch(local_offset, local_offset + 29)
    local = struct.unpack("<4s5H3L2H", header)
    if local[0] != b"PK\x03\x04":
        raise RuntimeError(f"bad local header for {name}")
    data_offset = local_offset + 30 + local[9] + local[10]
    payload = fetch(data_offset, data_offset + comp_size - 1)
    if method == 8:
        content = zlib.decompress(payload, -15)
    elif method == 0:
        content = payload
    else:
        raise RuntimeError(f"unsupported compression method {method}")
    if zlib.crc32(content) & 0xFFFFFFFF != crc:
        raise RuntimeError(f"CRC mismatch for {name}")
    return content


def main() -> int:
    parser = argparse.ArgumentParser(description="从 Zenodo 按需提取 vocadito 测试片段")
    parser.add_argument("--clips", default=DEFAULT_CLIPS, help=f"片段列表，默认 {DEFAULT_CLIPS}")
    parser.add_argument("--out-dir", default="tests/real/vocadito", help="输出目录")
    parser.add_argument("--workers", type=int, default=4, help="并发下载数")
    parser.add_argument("--force", action="store_true", help="重新下载已存在的文件")
    args = parser.parse_args()

    clips = parse_clips(args.clips)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    print("读取 zip 目录 ...", flush=True)
    entries = read_central_directory()
    jobs = []
    for clip in clips:
        for remote, local in [
            (f"Audio/vocadito_{clip}.wav", f"vocadito_{clip}.wav"),
            (f"Annotations/Notes/vocadito_{clip}_notesA1.csv", f"vocadito_{clip}_notesA1.csv"),
            (f"Annotations/Notes/vocadito_{clip}_notesA2.csv", f"vocadito_{clip}_notesA2.csv"),
        ]:
            if remote not in entries:
                print(f"压缩包中缺少: {remote}", file=sys.stderr)
                return 1
            jobs.append((remote, out_dir / local, entries[remote]))

    def worker(job):
        remote, path, (method, crc, comp_size, local_offset) = job
        if path.exists() and path.stat().st_size > 0 and not args.force:
            return path.name, path.stat().st_size, "cached"
        content = extract(remote, method, crc, comp_size, local_offset)
        temp = path.with_name(path.name + ".part")
        temp.write_bytes(content)
        os.replace(temp, path)
        return path.name, len(content), "downloaded"

    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.workers)) as pool:
        futures = {pool.submit(worker, job): job for job in jobs}
        for future in concurrent.futures.as_completed(futures):
            job = futures[future]
            try:
                name, size, status = future.result()
                print(f"{name}: {size} bytes ({status})", flush=True)
            except Exception as exc:  # noqa: BLE001 - 单个文件失败不阻塞其余任务
                failed += 1
                print(f"失败 {job[1].name}: {exc}", file=sys.stderr, flush=True)

    if failed:
        print(f"有 {failed} 个文件下载失败，可重新运行本脚本续传。", file=sys.stderr)
        return 1
    print(f"\n完成: {out_dir}")
    print("运行评估: python tests/eval/vocadito_bench.py --data-dir " + str(out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
