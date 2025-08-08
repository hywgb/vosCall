-- 将 ECB CSV 加载进 billing.fx_rates
-- 需先插入 currencies 表（ISO 4217 全量，建议通过官方列表导入）

CREATE TEMP TABLE tmp_ecb (
  date DATE,
  USD NUMERIC, JPY NUMERIC, BGN NUMERIC, CZK NUMERIC, DKK NUMERIC,
  GBP NUMERIC, HUF NUMERIC, PLN NUMERIC, RON NUMERIC, SEK NUMERIC,
  CHF NUMERIC, ISK NUMERIC, NOK NUMERIC, TRY NUMERIC, AUD NUMERIC,
  BRL NUMERIC, CAD NUMERIC, CNY NUMERIC, HKD NUMERIC, IDR NUMERIC,
  ILS NUMERIC, INR NUMERIC, KRW NUMERIC, MXN NUMERIC, MYR NUMERIC,
  NZD NUMERIC, PHP NUMERIC, SGD NUMERIC, THB NUMERIC, ZAR NUMERIC
);

\copy tmp_ecb FROM 'scripts/fx_ecb_latest.csv' WITH (FORMAT csv, HEADER true);

-- 基准 EUR→quote
INSERT INTO billing.fx_rates(as_of_date, base_currency, quote_currency, rate)
SELECT date, 'EUR', k AS quote, v::NUMERIC
FROM tmp_ecb t
CROSS JOIN LATERAL (
  VALUES
  ('USD', t.USD),('JPY', t.JPY),('BGN', t.BGN),('CZK', t.CZK),('DKK', t.DKK),
  ('GBP', t.GBP),('HUF', t.HUF),('PLN', t.PLN),('RON', t.RON),('SEK', t.SEK),
  ('CHF', t.CHF),('ISK', t.ISK),('NOK', t.NOK),('TRY', t.TRY),('AUD', t.AUD),
  ('BRL', t.BRL),('CAD', t.CAD),('CNY', t.CNY),('HKD', t.HKD),('IDR', t.IDR),
  ('ILS', t.ILS),('INR', t.INR),('KRW', t.KRW),('MXN', t.MXN),('MYR', t.MYR),
  ('NZD', t.NZD),('PHP', t.PHP),('SGD', t.SGD),('THB', t.THB),('ZAR', t.ZAR)
) AS kv(k,v)
WHERE v IS NOT NULL;