#!/usr/bin/env bash
set -euo pipefail

OUT_XML=./iso4217_list_one.xml
OUT_CSV=./iso4217_currencies.csv

# 官方来源（由 SIX Group 托管）
curl -fsSL "https://www.six-group.com/dam/download/financial-information/data-center/iso-currrency/lists/list-one.xml" -o "$OUT_XML"

python3 - "$OUT_XML" "$OUT_CSV" <<'PY'
import sys
import xml.etree.ElementTree as ET
xml = ET.parse(sys.argv[1])
root = xml.getroot()
ns = {'ns': 'http://www.iso.org/iso-4217'}

with open(sys.argv[2], 'w') as f:
    f.write('code,name\n')
    for c in root.findall('.//ns:CcyNtry', ns):
        code = c.findtext('ns:Ccy', default='', namespaces=ns)
        name = c.findtext('ns:CcyNm', default='', namespaces=ns)
        if code:
            f.write(f"{code},{name}\n")
PY

echo "Saved: $OUT_CSV"