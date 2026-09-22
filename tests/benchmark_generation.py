#!/usr/bin/env python3
"""Run one isolated generator pass and report reproducible baseline cost measurements."""

import argparse
import json
import os
import resource
import subprocess
import tempfile
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compare-package", type=Path)
    args = parser.parse_args()
    if not args.binary.is_file() or not (args.source / "NEWGAME.DAT").is_file():
        raise SystemExit("binary or baseline source is unavailable")

    with tempfile.TemporaryDirectory(prefix="water1-mapviewer-benchmark-") as temporary:
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        start = time.perf_counter()
        completed = subprocess.run(
            [str(args.binary), "--generate", str(args.source), "--output", temporary],
            env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        elapsed_seconds = time.perf_counter() - start
        if completed.returncode != 0:
            raise RuntimeError(completed.stderr or completed.stdout)
        package = Path(temporary) / "uw1-eng-dos-uw1-eng-dos-baseline"
        metadata = package / "world_map.json"
        if not metadata.is_file():
            raise RuntimeError("generator returned success without publishing world_map.json")
        output_bytes = sum(path.stat().st_size for path in package.rglob("*") if path.is_file())
        result = {
            "elapsed_seconds": round(elapsed_seconds, 3),
            "max_child_rss_kib": resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
            "package_output_bytes": output_bytes,
            "tile_file_count": len(list((package / "tiles").rglob("*.png"))),
            "published_metadata": metadata.name,
        }
        if args.compare_package:
            stable_outputs = ("world_map.png", "port_marker.png", "world_map.json", "manifest.json", "tile_manifest.json")
            for name in stable_outputs:
                generated = package / name
                reference = args.compare_package / name
                if not reference.is_file() or generated.read_bytes() != reference.read_bytes():
                    raise RuntimeError(f"non-deterministic generated output: {name}")
            result["deterministic_outputs_match"] = True
        print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
