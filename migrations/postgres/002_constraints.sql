BEGIN;

-- 索引
CREATE INDEX IF NOT EXISTS idx_trunks_account ON core.trunks(account_id);
CREATE INDEX IF NOT EXISTS idx_prefixes_dest ON routing.prefixes(dest_id);
CREATE INDEX IF NOT EXISTS idx_rate_items_table ON billing.rate_items(rate_table_id);
CREATE INDEX IF NOT EXISTS idx_plan_entries_plan ON routing.plan_entries(plan_id);
CREATE INDEX IF NOT EXISTS idx_plan_entries_prefix ON routing.plan_entries(prefix_id);
CREATE INDEX IF NOT EXISTS idx_plan_entries_vendor ON routing.plan_entries(vendor_id);
CREATE INDEX IF NOT EXISTS idx_blacklist_account ON security.blacklist_destinations(account_id);

-- 唯一性与时间区间冲突检查：费率版本不重叠（同账户同名）
CREATE UNIQUE INDEX IF NOT EXISTS uq_rate_table_name ON billing.rate_tables(account_id, name, effective_from);

CREATE OR REPLACE FUNCTION billing.check_rate_overlap() RETURNS TRIGGER AS $$
DECLARE
  cnt INT;
BEGIN
  SELECT count(*) INTO cnt FROM billing.rate_tables rt
   WHERE rt.account_id = NEW.account_id
     AND rt.name = NEW.name
     AND (NEW.effective_to IS NULL OR rt.effective_from <= NEW.effective_to)
     AND (rt.effective_to IS NULL OR rt.effective_to >= NEW.effective_from)
     AND rt.rate_table_id <> COALESCE(NEW.rate_table_id, -1);
  IF cnt > 0 THEN
    RAISE EXCEPTION 'Rate table overlap for account %, name %', NEW.account_id, NEW.name;
  END IF;
  RETURN NEW;
END; $$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_check_rate_overlap ON billing.rate_tables;
CREATE TRIGGER trg_check_rate_overlap
BEFORE INSERT OR UPDATE ON billing.rate_tables
FOR EACH ROW EXECUTE FUNCTION billing.check_rate_overlap();

-- 更新时间戳
CREATE OR REPLACE FUNCTION core.touch_updated_at() RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END; $$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_touch_accounts ON core.accounts;
CREATE TRIGGER trg_touch_accounts BEFORE UPDATE ON core.accounts FOR EACH ROW EXECUTE FUNCTION core.touch_updated_at();

DROP TRIGGER IF EXISTS trg_touch_trunks ON core.trunks;
CREATE TRIGGER trg_touch_trunks BEFORE UPDATE ON core.trunks FOR EACH ROW EXECUTE FUNCTION core.touch_updated_at();

COMMIT;