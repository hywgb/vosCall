#!/usr/bin/env bash
set -euo pipefail

# 拉取 ECB 外汇基准（欧元基准），保存为 CSV 以便装载
# 参考：ECB SDW，许可遵循其条款

OUT=./fx_ecb_latest.csv
curl -fsSL "https://www.ecb.europa.eu/stats/eurofxref/eurofxref-hist.csv" -o "$OUT"
echo "Saved: $OUT"