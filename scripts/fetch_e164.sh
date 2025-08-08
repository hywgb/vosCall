#!/usr/bin/env bash
set -euo pipefail

# 获取真实 E.164 国家/地区拨号代码与区域前缀数据源
# 数据源提示：可使用 ITU/国家监管机构或 libphonenumber 的公开资源

OUT_XML=./PhoneNumberMetadata.xml
OUT_CSV=./e164_prefixes.csv

curl -fsSL "https://raw.githubusercontent.com/google/libphonenumber/master/resources/PhoneNumberMetadata.xml" -o "$OUT_XML"

# 将 XML 转换为 CSV（国家码,区域名,前缀）— 需要 xmllint / xq 可选增强；此处给出基本提取示例
python3 - "$OUT_XML" "$OUT_CSV" <<'PY'
import sys, re
from xml.etree import ElementTree as ET
xml = ET.parse(sys.argv[1])
root = xml.getroot()
ns = { 'ph': '' }

with open(sys.argv[2], 'w') as f:
    f.write('country_code,country_name,iso_numeric,calling_code,area_name,prefix\n')
    for territory in root.iter('territory'):
        cc = territory.get('id')
        country_code = territory.get('countryCode')
        # Use available fields; iso_numeric unknown here, leave blank
        if not country_code:
            continue
        area_name = ''
        # extract generalDesc/nationalNumberPattern or fixedLine/mobile for prefixes (heuristic)
        for ndp in territory.iter('nationalNumberPattern'):
            pattern = ndp.text or ''
            pattern = pattern.strip().replace(' ', '')
            if not pattern or len(pattern) > 128:
                continue
            # naive extraction of leading prefix digits
            m = re.match(r"(\\d{2,6})", pattern)
            pref = m.group(1) if m else ''
            if pref:
                f.write(f"{cc},{cc},,{country_code},{area_name},{pref}\n")
                break
PY

echo "Saved: $OUT_CSV"