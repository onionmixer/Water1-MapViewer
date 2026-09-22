# Baseline generator benchmark

Measured on 2026-09-22 in the project workspace using the supported English DOS baseline profile,
Linux 6.8.0-138-generic x86_64, 32 logical CPUs, Qt5 offscreen platform. This is one isolated generation
pass; the output is created in a Python temporary directory and removed afterward.

| Measurement | Result |
|---|---:|
| Elapsed generation time | 15.296 s |
| Maximum child RSS | 1,202,224 KiB (1,174.0 MiB) |
| Published package size | 17,301,678 B (16.50 MiB) |
| Tile count | 774 |
| Image | 24,192 × 6,048 px |

The RSS figure includes the generator process and its Qt image/PNG working memory. It should be treated as
a baseline capacity requirement, not a portable minimum for every platform.

Reproduce the measurement after building:

```sh
python3 tests/benchmark_generation.py --binary ./build/Water1_MapViewer --source ../water1eng \
  --compare-package ./output/uw1-eng-dos-uw1-eng-dos-baseline
```

`--compare-package` verifies that the generated `world_map.png`, port marker, metadata, manifests are byte-for-byte
equal to the selected completed package. The measured run passed this deterministic-output comparison.
