CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE IF NOT EXISTS ecg_telemetry (
    id              BIGSERIAL PRIMARY KEY,
    device_id       TEXT        NOT NULL DEFAULT 'sim-001',
    seq_id          BIGINT      NOT NULL,
    ts              TIMESTAMPTZ NOT NULL,
    ts_us           BIGINT      NOT NULL,
    iv              BYTEA       NOT NULL,
    ciphertext      BYTEA       NOT NULL,
    mac             BYTEA       NOT NULL,
    voltage_mv      DOUBLE PRECISION,
    bpm             DOUBLE PRECISION,
    rr_interval_ms  DOUBLE PRECISION,
    classification  TEXT,
    verified        BOOLEAN     NOT NULL DEFAULT FALSE,
    ingested_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    CONSTRAINT uq_device_seq UNIQUE (device_id, seq_id),
    CONSTRAINT chk_iv_len  CHECK (octet_length(iv)  = 12),
    CONSTRAINT chk_mac_len CHECK (octet_length(mac) = 32)
);
CREATE INDEX IF NOT EXISTS idx_ecg_ts_brin ON ecg_telemetry USING BRIN (ts);
CREATE INDEX IF NOT EXISTS idx_ecg_class   ON ecg_telemetry (classification)
    WHERE classification <> 'NORMAL';

CREATE TABLE IF NOT EXISTS device_commands (
    id          BIGSERIAL PRIMARY KEY,
    device_id   TEXT        NOT NULL DEFAULT 'sim-001',
    seq_id      BIGINT      NOT NULL,
    command     TEXT        NOT NULL,
    params      JSONB       NOT NULL DEFAULT '{}',
    status      TEXT        NOT NULL DEFAULT 'PENDING'
                CHECK (status IN ('PENDING','ACCEPTED','REJECTED')),
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (device_id, seq_id)
);

CREATE TABLE IF NOT EXISTS audit_logs (
    id          BIGSERIAL PRIMARY KEY,
    ts          TIMESTAMPTZ NOT NULL DEFAULT now(),
    severity    TEXT        NOT NULL CHECK (severity IN ('INFO','WARN','CRITICAL')),
    event_type  TEXT        NOT NULL,
    seq_id      BIGINT,
    details     JSONB       NOT NULL DEFAULT '{}'
);
CREATE INDEX IF NOT EXISTS idx_audit_ts ON audit_logs (ts DESC);

CREATE OR REPLACE FUNCTION audit_logs_immutable() RETURNS trigger AS $$
BEGIN
    RAISE EXCEPTION 'audit_logs is append-only';
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_audit_immutable ON audit_logs;
CREATE TRIGGER trg_audit_immutable
    BEFORE UPDATE OR DELETE ON audit_logs
    FOR EACH ROW EXECUTE FUNCTION audit_logs_immutable();