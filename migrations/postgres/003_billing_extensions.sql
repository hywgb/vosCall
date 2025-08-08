BEGIN;

CREATE TABLE IF NOT EXISTS billing.authorizations (
  auth_id      BIGSERIAL PRIMARY KEY,
  call_id      TEXT NOT NULL,
  account_id   BIGINT NOT NULL REFERENCES core.accounts(account_id) ON DELETE CASCADE,
  token        TEXT NOT NULL UNIQUE,
  amount       NUMERIC(18,6) NOT NULL DEFAULT 0,
  currency     CHAR(3) NOT NULL,
  status       TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','settled','void')),
  created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_auth_account ON billing.authorizations(account_id);

COMMIT;