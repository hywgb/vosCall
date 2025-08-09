BEGIN;

CREATE OR REPLACE FUNCTION ops.write_audit(
  p_actor_id BIGINT,
  p_actor_name TEXT,
  p_action TEXT,
  p_entity_type TEXT,
  p_entity_id TEXT,
  p_diff JSONB
) RETURNS VOID AS $$
BEGIN
  INSERT INTO ops.audit_logs(actor_id, actor_name, action, entity_type, entity_id, diff)
  VALUES(p_actor_id, p_actor_name, p_action, p_entity_type, p_entity_id, p_diff);
END;
$$ LANGUAGE plpgsql;

COMMIT;