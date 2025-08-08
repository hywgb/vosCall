CREATE TABLE IF NOT EXISTS cdr (
  call_id String,
  attempt UInt8,
  
  start_ts DateTime,
  answer_ts Nullable(DateTime),
  end_ts DateTime,
  billsec UInt32,

  from_uri String,
  to_uri String,
  e164_from String,
  e164_to String,
  ingress_trunk String,
  egress_trunk String,

  sip_final_code UInt16,
  sip_final_reason String,
  route_plan String,
  vendor String,

  codec_in String,
  codec_out String,
  transcoded UInt8,

  pdd_ms UInt32,

  asr_bucket String,
  acd_seconds Float32,

  bytes_tx UInt64,
  bytes_rx UInt64,

  node String
)
ENGINE = MergeTree
PARTITION BY toYYYYMM(start_ts)
ORDER BY (call_id, attempt)
SETTINGS index_granularity = 8192;

-- 日聚合视图（示例）
CREATE MATERIALIZED VIEW IF NOT EXISTS cdr_daily_agg
ENGINE = SummingMergeTree
PARTITION BY toYYYYMM(start_ts)
ORDER BY (toDate(start_ts), vendor, egress_trunk)
AS
SELECT
  toDate(start_ts) AS day,
  vendor,
  egress_trunk,
  countIf(answer_ts IS NOT NULL) AS answered,
  count() AS total,
  sum(billsec) AS billsec,
  avgIf(pdd_ms, pdd_ms > 0) AS avg_pdd_ms
FROM cdr
GROUP BY day, vendor, egress_trunk;