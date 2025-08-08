-- 以 CSV 文件导入 E.164 目的地与前缀
CREATE TEMP TABLE tmp_e164 (
  country_code TEXT,
  country_name TEXT,
  iso_numeric TEXT,
  calling_code TEXT,
  area_name TEXT,
  prefix TEXT
);

\copy tmp_e164 FROM 'scripts/e164_prefixes.csv' WITH (FORMAT csv, HEADER true);

INSERT INTO routing.destinations(country_code, country_name, iso_numeric, calling_code, area_name)
SELECT DISTINCT country_code, country_name, COALESCE(NULLIF(iso_numeric,''),'000'), calling_code, NULLIF(area_name,'')
FROM tmp_e164;

INSERT INTO routing.prefixes(dest_id, prefix, description)
SELECT d.dest_id, t.prefix, COALESCE(t.area_name, d.country_name)
FROM tmp_e164 t
JOIN routing.destinations d
  ON d.country_code = t.country_code AND d.calling_code = t.calling_code
WHERE t.prefix <> ''
ON CONFLICT DO NOTHING;