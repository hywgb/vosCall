BEGIN;

CREATE SCHEMA IF NOT EXISTS core;
CREATE SCHEMA IF NOT EXISTS billing;
CREATE SCHEMA IF NOT EXISTS routing;
CREATE SCHEMA IF NOT EXISTS security;
CREATE SCHEMA IF NOT EXISTS ops;

-- 核心账户与中继
CREATE TABLE core.accounts (
  account_id       BIGSERIAL PRIMARY KEY,
  account_code     TEXT NOT NULL UNIQUE,
  name             TEXT NOT NULL,
  type             TEXT NOT NULL CHECK (type IN ('customer','vendor','reseller')),
  currency         CHAR(3) NOT NULL,
  balance          NUMERIC(18,6) NOT NULL DEFAULT 0,
  credit_limit     NUMERIC(18,6) NOT NULL DEFAULT 0,
  prepaid          BOOLEAN NOT NULL DEFAULT TRUE,
  status           TEXT NOT NULL DEFAULT 'active',
  created_at       TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE core.trunks (
  trunk_id         BIGSERIAL PRIMARY KEY,
  account_id       BIGINT NOT NULL REFERENCES core.accounts(account_id) ON DELETE CASCADE,
  name             TEXT NOT NULL,
  direction        TEXT NOT NULL CHECK (direction IN ('ingress','egress')),
  auth_mode        TEXT NOT NULL CHECK (auth_mode IN ('ip','digest')),
  auth_data        JSONB NOT NULL DEFAULT '{}'::jsonb,
  max_concurrent   INTEGER NOT NULL DEFAULT 0,
  max_cps          INTEGER NOT NULL DEFAULT 0,
  codecs           TEXT[] NOT NULL DEFAULT ARRAY['PCMA','PCMU','G729','G722','OPUS'],
  sip_transport    TEXT NOT NULL DEFAULT 'udp',
  srtp_policy      TEXT NOT NULL DEFAULT 'optional',
  enabled          BOOLEAN NOT NULL DEFAULT TRUE,
  created_at       TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- 目的地与前缀（E.164 国家/地区与更细分的路由前缀）
CREATE TABLE routing.destinations (
  dest_id          BIGSERIAL PRIMARY KEY,
  country_code     TEXT NOT NULL,   -- ISO3166-1 alpha-2
  country_name     TEXT NOT NULL,
  iso_numeric      TEXT NOT NULL,
  calling_code     TEXT NOT NULL,   -- E.164 国家区号，如 1, 44, 86
  area_name        TEXT,            -- 可为空，国家级记录为空
  created_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE routing.prefixes (
  prefix_id        BIGSERIAL PRIMARY KEY,
  dest_id          BIGINT NOT NULL REFERENCES routing.destinations(dest_id) ON DELETE CASCADE,
  prefix           TEXT NOT NULL,  -- 如 441, 4420, 8613
  description      TEXT NOT NULL,
  UNIQUE(dest_id, prefix)
);

-- 汇率与币种
CREATE TABLE billing.currencies (
  code CHAR(3) PRIMARY KEY,
  name TEXT NOT NULL
);

CREATE TABLE billing.fx_rates (
  as_of_date       DATE NOT NULL,
  base_currency    CHAR(3) NOT NULL,
  quote_currency   CHAR(3) NOT NULL REFERENCES billing.currencies(code),
  rate             NUMERIC(18,8) NOT NULL,
  PRIMARY KEY (as_of_date, base_currency, quote_currency)
);

-- 费率版本与明细
CREATE TABLE billing.rate_tables (
  rate_table_id    BIGSERIAL PRIMARY KEY,
  account_id       BIGINT NOT NULL REFERENCES core.accounts(account_id) ON DELETE CASCADE,
  name             TEXT NOT NULL,
  currency         CHAR(3) NOT NULL REFERENCES billing.currencies(code),
  effective_from   TIMESTAMPTZ NOT NULL,
  effective_to     TIMESTAMPTZ,
  created_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE billing.rate_items (
  rate_item_id     BIGSERIAL PRIMARY KEY,
  rate_table_id    BIGINT NOT NULL REFERENCES billing.rate_tables(rate_table_id) ON DELETE CASCADE,
  prefix_id        BIGINT NOT NULL REFERENCES routing.prefixes(prefix_id) ON DELETE RESTRICT,
  price_per_min    NUMERIC(18,6) NOT NULL,
  billing_step_sec INTEGER NOT NULL DEFAULT 60,
  min_time_sec     INTEGER NOT NULL DEFAULT 0,
  connection_fee   NUMERIC(18,6) NOT NULL DEFAULT 0,
  rounding_mode    TEXT NOT NULL DEFAULT 'ceil' CHECK (rounding_mode IN ('ceil','floor','round','bankers')),
  UNIQUE(rate_table_id, prefix_id)
);

-- 路由计划
CREATE TABLE routing.route_plans (
  plan_id          BIGSERIAL PRIMARY KEY,
  account_id       BIGINT NOT NULL REFERENCES core.accounts(account_id) ON DELETE CASCADE,
  name             TEXT NOT NULL,
  created_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE routing.vendors (
  vendor_id        BIGSERIAL PRIMARY KEY,
  account_id       BIGINT NOT NULL REFERENCES core.accounts(account_id) ON DELETE CASCADE,
  trunk_id         BIGINT NOT NULL REFERENCES core.trunks(trunk_id) ON DELETE CASCADE,
  name             TEXT NOT NULL,
  created_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE routing.plan_entries (
  entry_id         BIGSERIAL PRIMARY KEY,
  plan_id          BIGINT NOT NULL REFERENCES routing.route_plans(plan_id) ON DELETE CASCADE,
  prefix_id        BIGINT NOT NULL REFERENCES routing.prefixes(prefix_id),
  vendor_id        BIGINT NOT NULL REFERENCES routing.vendors(vendor_id),
  priority         INTEGER NOT NULL DEFAULT 100,     -- 数字越小优先级越高
  weight           INTEGER NOT NULL DEFAULT 100,
  max_cps          INTEGER NOT NULL DEFAULT 0,
  max_concurrent   INTEGER NOT NULL DEFAULT 0
);

-- 配额与黑白名单
CREATE TABLE security.blacklist_destinations (
  id               BIGSERIAL PRIMARY KEY,
  account_id       BIGINT REFERENCES core.accounts(account_id) ON DELETE CASCADE,
  prefix_id        BIGINT REFERENCES routing.prefixes(prefix_id),
  reason           TEXT,
  expire_at        TIMESTAMPTZ
);

-- CDR 账务汇总（明细在 ClickHouse）
CREATE TABLE billing.invoices (
  invoice_id       BIGSERIAL PRIMARY KEY,
  account_id       BIGINT NOT NULL REFERENCES core.accounts(account_id) ON DELETE CASCADE,
  period_start     DATE NOT NULL,
  period_end       DATE NOT NULL,
  currency         CHAR(3) NOT NULL,
  amount_due       NUMERIC(18,6) NOT NULL DEFAULT 0,
  status           TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','sent','paid','void')),
  created_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- 审计与 RBAC
CREATE TABLE ops.audit_logs (
  id               BIGSERIAL PRIMARY KEY,
  actor_id         BIGINT,
  actor_name       TEXT,
  action           TEXT NOT NULL,
  entity_type      TEXT NOT NULL,
  entity_id        TEXT,
  diff             JSONB,
  created_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE security.users (
  user_id          BIGSERIAL PRIMARY KEY,
  username         TEXT NOT NULL UNIQUE,
  password_hash    TEXT NOT NULL,
  display_name     TEXT NOT NULL,
  created_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE security.roles (
  role_id          BIGSERIAL PRIMARY KEY,
  role_name        TEXT NOT NULL UNIQUE
);

CREATE TABLE security.user_roles (
  user_id          BIGINT NOT NULL REFERENCES security.users(user_id) ON DELETE CASCADE,
  role_id          BIGINT NOT NULL REFERENCES security.roles(role_id) ON DELETE CASCADE,
  PRIMARY KEY (user_id, role_id)
);

COMMIT;