BEGIN;

CREATE TYPE ops.job_status AS ENUM ('pending','running','done','failed');

CREATE TABLE IF NOT EXISTS ops.jobs (
  job_id       BIGSERIAL PRIMARY KEY,
  job_type     TEXT NOT NULL,                -- e.g., rate_import
  status       ops.job_status NOT NULL DEFAULT 'pending',
  payload      JSONB,                        -- job parameters
  payload_text TEXT,                         -- optional large content (e.g., CSV)
  error        TEXT,
  created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  started_at   TIMESTAMPTZ,
  finished_at  TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_jobs_type_status ON ops.jobs(job_type, status);

CREATE OR REPLACE FUNCTION ops.touch_jobs_updated_at() RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END; $$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_touch_jobs ON ops.jobs;
CREATE TRIGGER trg_touch_jobs BEFORE UPDATE ON ops.jobs FOR EACH ROW EXECUTE FUNCTION ops.touch_jobs_updated_at();

COMMIT;