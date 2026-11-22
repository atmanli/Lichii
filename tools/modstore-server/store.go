package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"time"

	_ "modernc.org/sqlite"
)

const schema = `
CREATE TABLE IF NOT EXISTS games (
    title_id TEXT PRIMARY KEY,
    name TEXT NOT NULL
) STRICT;
CREATE TABLE IF NOT EXISTS mods (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    summary TEXT NOT NULL,
    description TEXT NOT NULL,
    author TEXT NOT NULL,
    type TEXT NOT NULL CHECK (type IN ('graphics','fps','cheats','translation','gameplay')),
    demo INTEGER NOT NULL CHECK (demo IN (0,1)),
    preview_sha TEXT,
    preview_type TEXT,
    status TEXT NOT NULL CHECK (status IN ('pending','approved','rejected')),
    updated_at INTEGER NOT NULL
) STRICT;
CREATE TABLE IF NOT EXISTS versions (
    mod_id TEXT NOT NULL REFERENCES mods(id),
    id TEXT NOT NULL,
    title_id TEXT NOT NULL REFERENCES games(title_id),
    game_version TEXT NOT NULL,
    build_id TEXT NOT NULL,
    min_lichii_build TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    size_bytes INTEGER NOT NULL,
    unpacked_size_bytes INTEGER NOT NULL,
    status TEXT NOT NULL CHECK (status IN ('pending','approved','rejected')),
    created_at INTEGER NOT NULL,
    PRIMARY KEY (mod_id,id)
) STRICT;
CREATE INDEX IF NOT EXISTS versions_catalog_mod ON versions (mod_id, status, created_at DESC);
CREATE INDEX IF NOT EXISTS versions_catalog_title ON versions (title_id, status, mod_id);
CREATE TABLE IF NOT EXISTS gamebanana (
    mod_id TEXT PRIMARY KEY REFERENCES mods(id),
    title_id TEXT NOT NULL REFERENCES games(title_id),
    page_url TEXT NOT NULL,
    preview_url TEXT,
    metadata TEXT NOT NULL
) STRICT;
CREATE INDEX IF NOT EXISTS gamebanana_title ON gamebanana(title_id, mod_id);
CREATE VIEW IF NOT EXISTS catalog_targets AS
    SELECT mod_id, title_id, status='approved' AS available FROM versions
    UNION ALL
    SELECT mod_id, title_id, 1 AS available FROM gamebanana;
`

const publicMod = `m.status = 'approved' AND EXISTS (
    SELECT 1 FROM catalog_targets t WHERE t.mod_id=m.id AND t.available)`

type store struct {
	db     *sql.DB
	assets string
}

func openStore(dir string) (*store, error) {
	dir, err := filepath.Abs(dir)
	if err != nil {
		return nil, err
	}
	assets := filepath.Join(dir, "assets")
	if err := os.MkdirAll(assets, 0700); err != nil {
		return nil, err
	}
	uri := url.URL{Scheme: "file", Path: filepath.ToSlash(filepath.Join(dir, "catalog.sqlite3")),
		RawQuery: "_pragma=foreign_keys(1)&_pragma=busy_timeout(5000)&_pragma=journal_mode(WAL)"}
	db, err := sql.Open("sqlite", uri.String())
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1)
	if _, err := db.Exec(schema); err != nil {
		db.Close()
		return nil, err
	}
	return &store{db: db, assets: assets}, nil
}

type game struct {
	TitleID  string `json:"title_id"`
	Name     string `json:"name"`
	ModCount int    `json:"mod_count"`
}

type mod struct {
	ID         string   `json:"id"`
	TitleIDs   []string `json:"title_ids"`
	Name       string   `json:"name"`
	Summary    string   `json:"summary"`
	Author     string   `json:"author"`
	Type       string   `json:"type"`
	Demo       bool     `json:"demo"`
	Source     string   `json:"source"`
	SourceURL  string   `json:"source_url,omitempty"`
	PreviewURL *string  `json:"preview_url"`
	UpdatedAt  string   `json:"updated_at"`
	Status     string   `json:"status,omitempty"`
}

type modDetail struct {
	mod
	Description string              `json:"description"`
	Versions    []version           `json:"versions"`
	External    *gameBananaMetadata `json:"external,omitempty"`
}

type version struct {
	ID                string `json:"id"`
	TitleID           string `json:"title_id"`
	GameVersion       string `json:"game_version"`
	BuildID           string `json:"build_id"`
	MinLichiiBuild    string `json:"min_lichii_build"`
	DownloadURL       string `json:"download_url"`
	SHA256            string `json:"sha256"`
	SizeBytes         int64  `json:"size_bytes"`
	UnpackedSizeBytes int64  `json:"unpacked_size_bytes"`
	Status            string `json:"status,omitempty"`
}

func modColumns(admin bool) string {
	match := "t.mod_id=m.id"
	if !admin {
		match += " AND t.available"
	}
	return `m.id, (SELECT json_group_array(title_id) FROM
	    (SELECT DISTINCT t.title_id FROM catalog_targets t WHERE ` + match + ` ORDER BY t.title_id)),
	    m.name, m.summary, m.author, m.type, m.demo, m.preview_sha, m.updated_at, m.status,
	    gb.page_url, gb.preview_url`
}

const modsFrom = " FROM mods m LEFT JOIN gamebanana gb ON gb.mod_id=m.id"
const publicPrefix = "/api/v1"
const adminPrefix = "/admin/v1"

type scanner interface {
	Scan(...any) error
}

func scanMod(row scanner, admin bool, description *string) (mod, error) {
	var m mod
	var titleIDs string
	var preview, sourceURL, remotePreview sql.NullString
	var updated int64
	args := []any{&m.ID, &titleIDs, &m.Name, &m.Summary, &m.Author, &m.Type, &m.Demo, &preview, &updated, &m.Status,
		&sourceURL, &remotePreview}
	if description != nil {
		args = append(args, description)
	}
	if err := row.Scan(args...); err != nil {
		return m, err
	}
	if err := json.Unmarshal([]byte(titleIDs), &m.TitleIDs); err != nil {
		return m, err
	}
	prefix := publicPrefix
	if admin {
		prefix = adminPrefix
	} else {
		m.Status = ""
	}
	switch {
	case sourceURL.Valid:
		m.Source = "gamebanana"
		m.SourceURL = sourceURL.String
		if remotePreview.Valid {
			m.PreviewURL = &remotePreview.String
		}
	case m.Demo:
		m.Source = "demo"
	default:
		m.Source = "submission"
	}
	if preview.Valid && !sourceURL.Valid {
		previewURL := prefix + "/mods/" + m.ID + "/preview"
		m.PreviewURL = &previewURL
	}
	m.UpdatedAt = time.UnixMilli(updated).UTC().Format("2006-01-02T15:04:05.000Z")
	return m, nil
}

func (s *store) detail(ctx context.Context, id string, admin bool) (modDetail, error) {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return modDetail{}, err
	}
	defer tx.Rollback()
	var detail modDetail
	prefix := adminPrefix
	modWhere := "m.id=?"
	versionWhere := "mod_id=?"
	if !admin {
		prefix = publicPrefix
		modWhere += " AND " + publicMod
		versionWhere += " AND status='approved'"
	}
	detail.mod, err = scanMod(tx.QueryRowContext(ctx, "SELECT "+modColumns(admin)+", m.description"+modsFrom+" WHERE "+modWhere, id), admin, &detail.Description)
	if err != nil {
		return detail, err
	}
	detail.Versions = []version{}
	if detail.Source == "gamebanana" {
		var raw string
		if err := tx.QueryRowContext(ctx, "SELECT metadata FROM gamebanana WHERE mod_id=?", id).Scan(&raw); err != nil {
			return detail, err
		}
		detail.External = new(gameBananaMetadata)
		return detail, json.Unmarshal([]byte(raw), detail.External)
	}
	rows, err := tx.QueryContext(ctx, `SELECT id, title_id, game_version, build_id, min_lichii_build, sha256,
	    size_bytes, unpacked_size_bytes, status FROM versions WHERE `+versionWhere+" ORDER BY created_at DESC, id ASC", id)
	if err != nil {
		return detail, err
	}
	defer rows.Close()
	for rows.Next() {
		var v version
		if err := rows.Scan(&v.ID, &v.TitleID, &v.GameVersion, &v.BuildID, &v.MinLichiiBuild, &v.SHA256,
			&v.SizeBytes, &v.UnpackedSizeBytes, &v.Status); err != nil {
			return detail, err
		}
		if !admin {
			v.Status = ""
		}
		v.DownloadURL = prefix + "/mods/" + id + "/versions/" + v.ID + "/download"
		detail.Versions = append(detail.Versions, v)
	}
	return detail, rows.Err()
}

func (s *store) changeVersionStatus(ctx context.Context, id, ver, status string) error {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer tx.Rollback()
	result, err := tx.ExecContext(ctx, "UPDATE versions SET status=? WHERE mod_id=? AND id=?", status, id, ver)
	if err := expectChanged(result, err); err != nil {
		return err
	}
	if _, err := tx.ExecContext(ctx, "UPDATE mods SET updated_at=? WHERE id=?", time.Now().UnixMilli(), id); err != nil {
		return err
	}
	return tx.Commit()
}

func expectChanged(result sql.Result, err error) error {
	if err != nil {
		return err
	}
	n, err := result.RowsAffected()
	if err != nil {
		return err
	}
	if n == 0 {
		return sql.ErrNoRows
	}
	return nil
}

func insertVersion(ctx context.Context, db interface {
	ExecContext(context.Context, string, ...any) (sql.Result, error)
}, modID string, v version, created int64) error {
	_, err := db.ExecContext(ctx, `INSERT INTO versions (mod_id,id,title_id,game_version,build_id,min_lichii_build,sha256,
	    size_bytes,unpacked_size_bytes,status,created_at) VALUES (?,?,?,?,?,?,?,?,?,?,?)`, modID, v.ID, v.TitleID,
		v.GameVersion, v.BuildID, v.MinLichiiBuild, v.SHA256, v.SizeBytes, v.UnpackedSizeBytes, v.Status, created)
	return err
}

func (s *store) health(ctx context.Context) error {
	var name string
	err := s.db.QueryRowContext(ctx, "SELECT name FROM games LIMIT 1").Scan(&name)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return err
	}
	info, err := os.Stat(s.assets)
	if err != nil {
		return err
	}
	if !info.IsDir() {
		return fmt.Errorf("asset directory is unavailable")
	}
	return nil
}
