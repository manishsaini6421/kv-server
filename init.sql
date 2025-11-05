-- This script initializes the PostgreSQL database for my KV (Key-Value) Store project.

-- Create the main table to store key-value pairs.
-- 'key' is a unique identifier (primary key), and 'value' holds the associated data.
-- 'created_at' and 'updated_at' keep track of when the record was inserted or modified.
CREATE TABLE IF NOT EXISTS kv_store (
    key VARCHAR(255) PRIMARY KEY,        -- Unique key for each entry (string up to 255 chars)
    value TEXT NOT NULL,                 -- Value corresponding to the key, cannot be NULL
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,  -- Auto-set creation timestamp
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP   -- Auto-set modification timestamp
);

-- Creating an index on 'key' to improve lookup performance for read and write queries.
-- Indexing helps in faster searches by avoiding full table scans.
CREATE INDEX IF NOT EXISTS idx_key ON kv_store(key);

-- Define a function that automatically updates the 'updated_at' timestamp 
-- whenever a record is modified. This ensures I don’t have to manually handle it in the app.
CREATE OR REPLACE FUNCTION update_modified_column()
RETURNS TRIGGER AS $$
BEGIN
    -- Update the 'updated_at' column to the current timestamp
    -- every time a row is updated.
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;  -- This function uses PostgreSQL's procedural language PL/pgSQL.

-- Create a trigger that executes the above function before every update operation on kv_store.
-- This trigger ensures that 'updated_at' always reflects the last modification time.
CREATE TRIGGER update_kv_store_modtime
    BEFORE UPDATE ON kv_store
    FOR EACH ROW
    EXECUTE FUNCTION update_modified_column();
