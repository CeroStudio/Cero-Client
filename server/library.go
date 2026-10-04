package main

import (
	"bytes"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"image/png"
	"io"
	"log"
	"mime/multipart"
	"net/http"
	"net/textproto"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

const (
	skinQuotaBytes int64 = 10 * 1024 * 1024

	skinMaxFileBytes int64 = 1 * 1024 * 1024

	skinMultipartMemory int64 = 256 * 1024

)

const mojangSkinAPI = "https://api.minecraftservices.com/minecraft/profile/skins"

func libraryDir() string {
	dir := "./data/library"
	if v := os.Getenv("DATA_DIR"); v != "" {
		dir = filepath.Join(v, "library")
	}
	return dir
}

func userLibraryDir(ownerUUID, kind string, when time.Time) string {
	day := when.UTC().Format("2006-01-02")
	return filepath.Join(libraryDir(), kind, ownerUUID, day)
}

type libraryItem struct {
	ID         int64          `json:"id"`
	Kind       string         `json:"kind"`
	Name       string         `json:"name"`
	Mime       string         `json:"mime"`
	Size       int64          `json:"size"`
	Metadata   map[string]any `json:"metadata,omitempty"`
	IsFavorite bool           `json:"is_favorite"`
	IsActive   bool           `json:"is_active"`
	CreatedAt  int64          `json:"created_at"`
	URL        string         `json:"url"`
	PreviewURL string         `json:"preview_url"`
}

type skinMeta struct {
	Variant          string `json:"variant"`
	Width            int    `json:"width"`
	Height           int    `json:"height"`
	MojangTextureURL string `json:"mojang_texture_url,omitempty"`
	MojangState      string `json:"mojang_state,omitempty"`
	AppliedAt        *int64 `json:"applied_at,omitempty"`
}

func libraryDirFromItem(ownerUUID, kind, storedName string) string {
	pattern := filepath.Join(libraryDir(), kind, ownerUUID, "*", storedName)
	matches, _ := filepath.Glob(pattern)
	if len(matches) > 0 {
		return matches[0]
	}
	return ""
}

func parseSkinPNG(data []byte) (int, int, error) {
	cfg, err := png.DecodeConfig(bytes.NewReader(data))
	if err != nil {
		return 0, 0, fmt.Errorf("invalid_png: %w", err)
	}
	w, h := cfg.Width, cfg.Height
	if w != h && w != 2*h {
		return 0, 0, fmt.Errorf("invalid_dimensions_%dx%d", w, h)
	}
	if w < 64 || w > 1024 {
		return 0, 0, fmt.Errorf("unsupported_width_%d", w)
	}
	if h < 32 || h > 1024 {
		return 0, 0, fmt.Errorf("unsupported_height_%d", h)
	}
	return w, h, nil
}

func detectSkinVariantFromBytes(data []byte, w, h int) string {
	img, err := png.Decode(bytes.NewReader(data))
	if err != nil {
		return "classic"
	}
	bounds := img.Bounds()
	if bounds.Dx() != w || bounds.Dy() != h {
		return "classic"
	}

	if w == 64 && h == 64 {
		for y := 48; y < 52; y++ {
			_, _, _, a := img.At(51, y).RGBA()
			if a != 0 {
				return "classic"
			}
		}
		return "slim"
	}
	return "classic"
}

func dailyLibraryUsage(ownerUUID, kind string, now time.Time) (int64, error) {
	cutoff := now.Add(-24 * time.Hour).UnixMilli()
	var sum sql.NullInt64
	err := db.QueryRow(`
                SELECT COALESCE(SUM(size), 0)
                FROM library_items
                WHERE owner_uuid = ? AND kind = ? AND created_at >= ?
        `, ownerUUID, kind, cutoff).Scan(&sum)
	if err != nil {
		return 0, err
	}
	if !sum.Valid {
		return 0, nil
	}
	return sum.Int64, nil
}

func handleLibrarySkinsList(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	rows, err := db.Query(`
                SELECT id, kind, name, stored_name, mime_type, size, metadata,
                       is_favorite, is_active, created_at
                FROM library_items
                WHERE owner_uuid = ? AND kind = 'skin'
                ORDER BY is_favorite DESC, created_at DESC
        `, me.UUID)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	defer rows.Close()

	apiBase := apiBaseFromRequest(r)
	items := []libraryItem{}
	for rows.Next() {
		var (
			it         libraryItem
			storedName string
			metaJSON   sql.NullString
			isFav      int
			isAct      int
		)
		if err := rows.Scan(&it.ID, &it.Kind, &it.Name, &storedName, &it.Mime,
			&it.Size, &metaJSON, &isFav, &isAct, &it.CreatedAt); err != nil {
			continue
		}
		it.IsFavorite = isFav != 0
		it.IsActive = isAct != 0
		if metaJSON.Valid && metaJSON.String != "" {
			var meta map[string]any
			if json.Unmarshal([]byte(metaJSON.String), &meta) == nil {
				it.Metadata = meta
			}
		}
		it.URL = apiBase + "/api/library/skins/" + strconv.FormatInt(it.ID, 10) + "/file"
		it.PreviewURL = it.URL
		items = append(items, it)
	}

	mojangSkin, _ := fetchMojangSkinState(r)
	writeJSON(w, http.StatusOK, M{
		"items":       items,
		"current":     mojangSkin,
		"quota_limit": skinQuotaBytes,
	})
}

func handleLibrarySkinsUpload(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	if me == nil {
		writeJSON(w, http.StatusUnauthorized, M{"error": "unauthorized"})
		return
	}

	r.Body = http.MaxBytesReader(w, r.Body, skinMaxFileBytes+64*1024)
	if err := r.ParseMultipartForm(skinMultipartMemory); err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_form", "detail": err.Error()})
		return
	}

	file, header, err := r.FormFile("file")
	if err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "missing_file"})
		return
	}
	defer file.Close()
	if header.Size <= 0 || header.Size > skinMaxFileBytes {
		writeJSON(w, http.StatusRequestEntityTooLarge, M{
			"error": "file_too_large",
			"max":   skinMaxFileBytes,
			"size":  header.Size,
		})
		return
	}

	mime := header.Header.Get("Content-Type")
	mime = strings.ToLower(strings.TrimSpace(strings.Split(mime, ";")[0]))
	if mime != "image/png" {
		if strings.EqualFold(filepath.Ext(header.Filename), ".png") {
			mime = "image/png"
		} else {
			writeJSON(w, http.StatusUnsupportedMediaType, M{
				"error":   "unsupported_type",
				"mime":    mime,
				"allowed": []string{"image/png"},
			})
			return
		}
	}

	data, err := io.ReadAll(file)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	if int64(len(data)) > skinMaxFileBytes {
		writeJSON(w, http.StatusRequestEntityTooLarge, M{"error": "file_too_large"})
		return
	}

	w2, h, err := parseSkinPNG(data)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_skin", "detail": err.Error()})
		return
	}

	now := time.Now()
	used, err := dailyLibraryUsage(me.UUID, "skin", now)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	remaining := skinQuotaBytes - used
	if remaining < int64(len(data)) {
		writeJSON(w, http.StatusRequestEntityTooLarge, M{
			"error":       "quota_exceeded",
			"quota_bytes": skinQuotaBytes,
			"used_bytes":  used,
			"remaining":   remaining,
			"needed":      int64(len(data)),
		})
		return
	}

	dir := userLibraryDir(me.UUID, "skin", now)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		log.Printf("[library] mkdir %s: %v", dir, err)
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	storedName := now.UTC().Format("20060102T150405") + "_" + randomToken() + ".png"
	dstPath := filepath.Join(dir, storedName)
	if err := os.WriteFile(dstPath, data, 0o644); err != nil {
		log.Printf("[library] write %s: %v", dstPath, err)
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	name := strings.TrimSpace(r.FormValue("name"))
	if name == "" {
		base := filepath.Base(header.Filename)
		name = strings.TrimSuffix(base, filepath.Ext(base))
	}
	if name == "" {
		name = "Skin"
	}
	if len(name) > 64 {
		name = name[:64]
	}

	variant := strings.ToLower(strings.TrimSpace(r.FormValue("variant")))
	if variant != "classic" && variant != "slim" {
		variant = detectSkinVariantFromBytes(data, w2, h)
	}

	meta := skinMeta{
		Variant: variant,
		Width:   w2,
		Height:  h,
	}
	metaJSON, _ := json.Marshal(meta)

	nowMilli := nowMillis()
	res, err := db.Exec(`
                INSERT INTO library_items
                        (owner_uuid, kind, name, stored_name, mime_type, size, metadata, is_favorite, is_active, created_at)
                VALUES (?, 'skin', ?, ?, ?, ?, ?, 0, 0, ?)
        `, me.UUID, name, storedName, mime, int64(len(data)), string(metaJSON), nowMilli)
	if err != nil {
		_ = os.Remove(dstPath)
		log.Printf("[library] insert row: %v", err)
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	id, _ := res.LastInsertId()

	apiBase := apiBaseFromRequest(r)
	writeJSON(w, http.StatusCreated, M{
		"ok": true,
		"item": libraryItem{
			ID:   id,
			Kind: "skin",
			Name: name,
			Mime: mime,
			Size: int64(len(data)),
			Metadata: map[string]any{
				"variant": variant,
				"width":   w2,
				"height":  h,
			},
			IsFavorite: false,
			IsActive:   false,
			CreatedAt:  nowMilli,
			URL:        apiBase + "/api/library/skins/" + strconv.FormatInt(id, 10) + "/file",
			PreviewURL: apiBase + "/api/library/skins/" + strconv.FormatInt(id, 10) + "/file",
		},
		"quota": M{
			"used":      used + int64(len(data)),
			"remaining": remaining - int64(len(data)),
			"limit":     skinQuotaBytes,
		},
	})
}

func handleLibrarySkinsDelete(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	id, err := parseIDFromPath(r.URL.Path, "/api/library/skins")
	if err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_id"})
		return
	}

	var storedName string
	err = db.QueryRow(`SELECT stored_name FROM library_items WHERE id = ? AND owner_uuid = ?`,
		id, me.UUID).Scan(&storedName)
	if err == sql.ErrNoRows {
		writeJSON(w, http.StatusNotFound, M{"error": "not_found"})
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	if _, err := db.Exec(`DELETE FROM library_items WHERE id = ? AND owner_uuid = ?`, id, me.UUID); err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	path := libraryDirFromItem(me.UUID, "skin", storedName)
	if path != "" {
		_ = os.Remove(path)
	}
	writeJSON(w, http.StatusOK, M{"ok": true, "id": id})
}

func handleLibrarySkinsUpdate(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	id, err := parseIDFromPath(r.URL.Path, "/api/library/skins")
	if err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_id"})
		return
	}
	var body struct {
		Name       *string `json:"name,omitempty"`
		IsFavorite *bool   `json:"is_favorite,omitempty"`
	}
	if err := readJSON(r, &body); err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_body"})
		return
	}

	var exists int
	err = db.QueryRow(`SELECT 1 FROM library_items WHERE id = ? AND owner_uuid = ?`,
		id, me.UUID).Scan(&exists)
	if err == sql.ErrNoRows {
		writeJSON(w, http.StatusNotFound, M{"error": "not_found"})
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	if body.Name != nil {
		clean := strings.TrimSpace(*body.Name)
		if clean == "" || len(clean) > 64 {
			writeJSON(w, http.StatusBadRequest, M{"error": "invalid_name"})
			return
		}
		if _, err := db.Exec(`UPDATE library_items SET name = ? WHERE id = ?`, clean, id); err != nil {
			writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
			return
		}
	}
	if body.IsFavorite != nil {
		val := 0
		if *body.IsFavorite {
			val = 1
		}
		if _, err := db.Exec(`UPDATE library_items SET is_favorite = ? WHERE id = ?`, val, id); err != nil {
			writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
			return
		}
	}
	writeJSON(w, http.StatusOK, M{"ok": true, "id": id})
}

func handleLibrarySkinsApply(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	idStr := strings.TrimPrefix(r.URL.Path, "/api/library/skins/")
	idStr = strings.TrimSuffix(idStr, "/apply")
	id, err := strconv.ParseInt(idStr, 10, 64)
	if err != nil || id == 0 {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_id"})
		return
	}

	var (
		storedName string
		metaJSON   sql.NullString
	)
	err = db.QueryRow(`SELECT stored_name, metadata FROM library_items WHERE id = ? AND owner_uuid = ?`,
		id, me.UUID).Scan(&storedName, &metaJSON)
	if err == sql.ErrNoRows {
		writeJSON(w, http.StatusNotFound, M{"error": "not_found"})
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	var meta skinMeta
	if metaJSON.Valid {
		_ = json.Unmarshal([]byte(metaJSON.String), &meta)
	}
	if meta.Variant == "" {
		meta.Variant = "classic"
	}

	path := libraryDirFromItem(me.UUID, "skin", storedName)
	if path == "" {
		writeJSON(w, http.StatusNotFound, M{"error": "file_missing"})
		return
	}
	data, err := os.ReadFile(path)
	if err != nil {
		log.Printf("[library] read %s: %v", path, err)
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	authHeader := r.Header.Get("Authorization")
	token := strings.TrimSpace(strings.TrimPrefix(authHeader, "Bearer "))
	if token == "" {
		writeJSON(w, http.StatusUnauthorized, M{"error": "missing_token"})
		return
	}

	var buf bytes.Buffer
	writer := multipart.NewWriter(&buf)
	if err := writer.WriteField("variant", meta.Variant); err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	hdr := textproto.MIMEHeader{}
	hdr.Set("Content-Disposition",
		`form-data; name="file"; filename="skin.png"`)
	hdr.Set("Content-Type", "image/png")
	part, err := writer.CreatePart(hdr)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	if _, err := part.Write(data); err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	if err := writer.Close(); err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	req, err := http.NewRequestWithContext(r.Context(),
		http.MethodPost, mojangSkinAPI, &buf)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("Content-Type", writer.FormDataContentType())

	resp, err := httpClient.Do(req)
	if err != nil {
		log.Printf("[library] mojang call: %v", err)
		writeJSON(w, http.StatusBadGateway, M{"error": "mojang_unreachable", "detail": err.Error()})
		return
	}
	defer resp.Body.Close()
	mojangBody, _ := io.ReadAll(resp.Body)

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		log.Printf("[library] mojang apply %s -> %d: %s", me.UUID, resp.StatusCode, string(mojangBody))
		writeJSON(w, http.StatusBadGateway, M{
			"error":       "mojang_error",
			"status":      resp.StatusCode,
			"mojang_body": json.RawMessage(mojangBody),
		})
		return
	}

	var mojangResp struct {
		ID    string `json:"id"`
		State string `json:"state"`
		URL   string `json:"url"`
		Var   string `json:"variant"`
	}
	if err := json.Unmarshal(mojangBody, &mojangResp); err == nil {
		meta.MojangTextureURL = mojangResp.URL
		meta.MojangState = mojangResp.State
		nowMilli := nowMillis()
		meta.AppliedAt = &nowMilli
	}

	_, _ = db.Exec(`UPDATE library_items SET is_active = 0 WHERE owner_uuid = ? AND kind = 'skin'`, me.UUID)
	metaJSONOut, _ := json.Marshal(meta)
	_, _ = db.Exec(`UPDATE library_items SET is_active = 1, metadata = ? WHERE id = ?`,
		string(metaJSONOut), id)

	writeJSON(w, http.StatusOK, M{
		"ok":           true,
		"id":           id,
		"mojang_state": meta.MojangState,
		"mojang_url":   meta.MojangTextureURL,
		"variant":      meta.Variant,
		"applied_at":   meta.AppliedAt,
	})
}

func handleLibrarySkinsFile(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	id, err := parseIDFromPath(r.URL.Path, "/api/library/skins")
	if err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_id"})
		return
	}

	var (
		storedName string
		ownerUUID  string
		mime       string
		size       int64
	)
	err = db.QueryRow(`
                SELECT owner_uuid, stored_name, mime_type, size
                FROM library_items
                WHERE id = ? AND kind = 'skin'
        `, id).Scan(&ownerUUID, &storedName, &mime, &size)
	if err == sql.ErrNoRows {
		writeJSON(w, http.StatusNotFound, M{"error": "not_found"})
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	if ownerUUID != me.UUID {
		writeJSON(w, http.StatusForbidden, M{"error": "forbidden"})
		return
	}

	path := libraryDirFromItem(ownerUUID, "skin", storedName)
	if path == "" {
		writeJSON(w, http.StatusNotFound, M{"error": "file_missing"})
		return
	}

	w.Header().Set("Content-Type", mime)
	w.Header().Set("Content-Length", strconv.FormatInt(size, 10))
	w.Header().Set("Cache-Control", "private, max-age=300")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	http.ServeFile(w, r, path)
}

func handleLibrarySkinsQuota(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	used, err := dailyLibraryUsage(me.UUID, "skin", time.Now())
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	remaining := skinQuotaBytes - used
	if remaining < 0 {
		remaining = 0
	}
	writeJSON(w, http.StatusOK, M{
		"used":         used,
		"remaining":    remaining,
		"limit":        skinQuotaBytes,
		"reset_in_sec": int((24 * time.Hour).Seconds()),
	})
}

type mojangSkinState struct {
	ID      string `json:"id"`
	State   string `json:"state"`
	URL     string `json:"url"`
	Variant string `json:"variant"`
	Source  string `json:"source"`
}

func fetchMojangSkinState(r *http.Request) (*mojangSkinState, error) {
	authHeader := r.Header.Get("Authorization")
	token := strings.TrimSpace(strings.TrimPrefix(authHeader, "Bearer "))
	if token == "" {
		return nil, errors.New("missing_token")
	}

	req, err := http.NewRequestWithContext(r.Context(),
		http.MethodGet, mojangSkinAPI, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Authorization", "Bearer "+token)

	resp, err := httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, fmt.Errorf("mojang_status_%d", resp.StatusCode)
	}

	var apiResp struct {
		Skins []struct {
			ID      string `json:"id"`
			State   string `json:"state"`
			URL     string `json:"url"`
			Variant string `json:"variant"`
			Source  string `json:"source"`
		} `json:"skins"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&apiResp); err != nil {
		return nil, err
	}
	for _, s := range apiResp.Skins {
		if s.State == "ACTIVE" {
			return &mojangSkinState{
				ID:      s.ID,
				State:   s.State,
				URL:     s.URL,
				Variant: s.Variant,
				Source:  s.Source,
			}, nil
		}
	}
	return &mojangSkinState{State: "NONE"}, nil
}

func parseIDFromPath(path, prefix string) (int64, error) {
	s := strings.TrimPrefix(path, prefix+"/")
	if idx := strings.IndexByte(s, '/'); idx >= 0 {
		s = s[:idx]
	}
	if s == "" {
		return 0, errors.New("empty")
	}
	return strconv.ParseInt(s, 10, 64)
}
