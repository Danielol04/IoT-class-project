DROP TABLE IF EXISTS alarm_log CASCADE;
DROP TABLE IF EXISTS access_log CASCADE;
DROP TABLE IF EXISTS authorized_cards CASCADE;
DROP TABLE IF EXISTS nodes CASCADE;
DROP TABLE IF EXISTS sites CASCADE;

CREATE TABLE sites (
    id SERIAL PRIMARY KEY,
    site_id VARCHAR(50) UNIQUE NOT NULL,
    description VARCHAR(200),
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE nodes (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(20) UNIQUE NOT NULL,
    node_type VARCHAR(50) NOT NULL,
    site_id VARCHAR(50) NOT NULL,
    description VARCHAR(200),
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE authorized_cards (
    id SERIAL PRIMARY KEY,
    card_id VARCHAR(20) UNIQUE NOT NULL,
    owner_name VARCHAR(100) NOT NULL,
    site_id VARCHAR(50),
    is_active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE access_log (
    id SERIAL PRIMARY KEY,
    card_id VARCHAR(20),
    device_id VARCHAR(20),
    site_id VARCHAR(50),
    result VARCHAR(20),
    event_ts BIGINT,
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE alarm_log (
    id SERIAL PRIMARY KEY,
    site_id VARCHAR(50),
    device_id VARCHAR(20),
    alarm_type VARCHAR(50),
    event_ts BIGINT,
    details TEXT,
    created_at TIMESTAMP DEFAULT NOW()
);

INSERT INTO sites (site_id, description)
VALUES ('lavavajillas', 'Sistema IoT local')
ON CONFLICT DO NOTHING;

INSERT INTO nodes (device_id, node_type, site_id, description) VALUES
('C0:49:EF:CA:65:A0', 'access_control', 'lavavajillas', 'Nodo RFID'),
('FC:B4:67:56:51:6C', 'door_monitor', 'lavavajillas', 'Nodo puerta')
ON CONFLICT DO NOTHING;

INSERT INTO authorized_cards (card_id, owner_name, site_id, is_active) VALUES
('f378cd97', 'Alice García', 'lavavajillas', TRUE),
('E5F6A7B8', 'Bob Martínez', 'lavavajillas', TRUE),
('C9D0E1F2', 'Carlos López', 'lavavajillas', TRUE)
ON CONFLICT DO NOTHING;

