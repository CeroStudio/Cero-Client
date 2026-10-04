package main

import (
	"database/sql"
	"log"
	"os"
	"path/filepath"

	_ "github.com/mattn/go-sqlite3"
)

var db *sql.DB

func initDB() *sql.DB {
	dataDir := "./data"
	if v := os.Getenv("DATA_DIR"); v != "" {
		dataDir = v
	}
	if err := os.MkdirAll(dataDir, 0o755); err != nil {
		log.Fatalf("mkdir data dir: %v", err)
	}

	dsn := filepath.Join(dataDir, "cero.db") + "?_journal_mode=WAL&_foreign_keys=on"
	conn, err := sql.Open("sqlite3", dsn)
	if err != nil {
		log.Fatalf("open db: %v", err)
	}

	conn.SetMaxOpenConns(1)

	schema := `
        CREATE TABLE IF NOT EXISTS users (
                uuid        TEXT PRIMARY KEY,
                username    TEXT NOT NULL,
                last_seen   INTEGER NOT NULL DEFAULT 0,
                status      TEXT NOT NULL DEFAULT 'offline',
                created_at  INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS friendships (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                user_a      TEXT NOT NULL,
                user_b      TEXT NOT NULL,
                status      TEXT NOT NULL DEFAULT 'pending',
                requested_by TEXT NOT NULL,
                created_at  INTEGER NOT NULL,
                UNIQUE(user_a, user_b),
                FOREIGN KEY(user_a) REFERENCES users(uuid) ON DELETE CASCADE,
                FOREIGN KEY(user_b) REFERENCES users(uuid) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS messages (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                from_uuid   TEXT NOT NULL,
                to_uuid     TEXT NOT NULL,
                content     TEXT NOT NULL,
                sent_at     INTEGER NOT NULL,
                read_at     INTEGER,
                edited_at   INTEGER,
                FOREIGN KEY(from_uuid) REFERENCES users(uuid) ON DELETE CASCADE,
                FOREIGN KEY(to_uuid)   REFERENCES users(uuid) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS attachments (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                message_id  INTEGER,
                owner_uuid  TEXT NOT NULL,
                stored_name TEXT NOT NULL,
                orig_name   TEXT NOT NULL,
                mime_type   TEXT NOT NULL,
                kind        TEXT NOT NULL,
                size        INTEGER NOT NULL,
                created_at  INTEGER NOT NULL,
                FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE SET NULL,
                FOREIGN KEY(owner_uuid) REFERENCES users(uuid) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS library_items (
                id           INTEGER PRIMARY KEY AUTOINCREMENT,
                owner_uuid   TEXT NOT NULL,
                kind         TEXT NOT NULL,
                name         TEXT NOT NULL,
                stored_name  TEXT NOT NULL,
                mime_type    TEXT NOT NULL DEFAULT 'image/png',
                size         INTEGER NOT NULL DEFAULT 0,
                metadata     TEXT,
                is_favorite  INTEGER NOT NULL DEFAULT 0,
                is_active    INTEGER NOT NULL DEFAULT 0,
                created_at   INTEGER NOT NULL,
                FOREIGN KEY(owner_uuid) REFERENCES users(uuid) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_msg_conv ON messages(from_uuid, to_uuid, sent_at);
        CREATE INDEX IF NOT EXISTS idx_att_msg   ON attachments(message_id);
        CREATE INDEX IF NOT EXISTS idx_att_owner ON attachments(owner_uuid, created_at);
        CREATE INDEX IF NOT EXISTS idx_lib_owner ON library_items(owner_uuid, kind, created_at);
        CREATE INDEX IF NOT EXISTS idx_friend_a ON friendships(user_a);
        CREATE INDEX IF NOT EXISTS idx_friend_b ON friendships(user_b);
        `
	if _, err := conn.Exec(schema); err != nil {
		log.Fatalf("migrate schema: %v", err)
	}

	db = conn
	return conn
}

func pair(a, b string) (string, string) {
	if a < b {
		return a, b
	}
	return b, a
}
