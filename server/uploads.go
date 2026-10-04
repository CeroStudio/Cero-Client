package main

import (
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

const (
	uploadQuotaBytes int64 = 50 * 1024 * 1024

	uploadMaxFileBytes int64 = 25 * 1024 * 1024

	uploadMultipartMemory int64 = 1 * 1024 * 1024
)

var allowedMime = map[string]string{
	// images
	"image/png":  "image",
	"image/jpeg": "image",
	"image/gif":  "image",
	"image/webp": "image",
	"image/bmp":  "image",
	// audio
	"audio/mpeg":  "audio",
	"audio/mp3":   "audio",
	"audio/ogg":   "audio",
	"audio/wav":   "audio",
	"audio/x-wav": "audio",
	"audio/webm":  "audio",
	"audio/mp4":   "audio",
	"audio/aac":   "audio",
	"audio/x-m4a": "audio",
	// video
	"video/mp4":        "video",
	"video/webm":       "video",
	"video/ogg":        "video",
	"video/quicktime":  "video",
	"video/x-matroska": "video",
}

func uploadDir() string {
	dir := "./data/uploads"
	if v := os.Getenv("DATA_DIR"); v != "" {
		dir = filepath.Join(v, "uploads")
	}
	return dir
}

func userUploadDir(ownerUUID string, when time.Time) string {
	day := when.UTC().Format("2006-01-02")
	return filepath.Join(uploadDir(), ownerUUID, day)
}

func randomToken() string {
	b := make([]byte, 16)
	if _, err := rand.Read(b); err != nil {
		// Should never happen in practice; fall back to time-based.
		return strings.ReplaceAll(time.Now().UTC().Format("20060102T150405.000000000"), ".", "")
	}
	return hex.EncodeToString(b)
}

func dailyUploadUsage(ownerUUID string, now time.Time) (int64, error) {
	cutoff := now.Add(-24 * time.Hour).UnixMilli()
	var sum sql.NullInt64
	err := db.QueryRow(`
                SELECT COALESCE(SUM(size), 0)
                FROM attachments
                WHERE owner_uuid = ? AND created_at >= ?
        `, ownerUUID, cutoff).Scan(&sum)
	if err != nil {
		return 0, err
	}
	if !sum.Valid {
		return 0, nil
	}
	return sum.Int64, nil
}

func quotaRemaining(ownerUUID string, now time.Time) (int64, int64, error) {
	used, err := dailyUploadUsage(ownerUUID, now)
	if err != nil {
		return 0, 0, err
	}
	remaining := uploadQuotaBytes - used
	if remaining < 0 {
		remaining = 0
	}
	return used, remaining, nil
}

type attachmentJSON struct {
	ID        int64  `json:"id"`
	MessageID *int64 `json:"message_id,omitempty"`
	Name      string `json:"name"`
	Mime      string `json:"mime"`
	Kind      string `json:"kind"`
	Size      int64  `json:"size"`
	URL       string `json:"url"`
	CreatedAt int64  `json:"created_at"`
}

func attachmentToJSON(a attachmentRow, baseURL string) attachmentJSON {
	return attachmentJSON{
		ID:        a.ID,
		MessageID: a.MessageID,
		Name:      a.OrigName,
		Mime:      a.MimeType,
		Kind:      a.Kind,
		Size:      a.Size,
		URL:       baseURL + "/api/uploads/" + a.StoredName,
		CreatedAt: a.CreatedAt,
	}
}

type attachmentRow struct {
	ID         int64
	MessageID  *int64
	OwnerUUID  string
	StoredName string
	OrigName   string
	MimeType   string
	Kind       string
	Size       int64
	CreatedAt  int64
}

func fetchAttachmentsForMessages(messageIDs []int64) (map[int64][]attachmentRow, error) {
	out := make(map[int64][]attachmentRow)
	if len(messageIDs) == 0 {
		return out, nil
	}
	placeholders := make([]any, 0, len(messageIDs))
	qmarks := make([]string, 0, len(messageIDs))
	for _, id := range messageIDs {
		placeholders = append(placeholders, id)
		qmarks = append(qmarks, "?")
	}
	q := `SELECT id, message_id, owner_uuid, stored_name, orig_name, mime_type, kind, size, created_at
              FROM attachments
              WHERE message_id IN (` + strings.Join(qmarks, ",") + `)`
	rows, err := db.Query(q, placeholders...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	for rows.Next() {
		var a attachmentRow
		var msgID sql.NullInt64
		if err := rows.Scan(&a.ID, &msgID, &a.OwnerUUID, &a.StoredName, &a.OrigName,
			&a.MimeType, &a.Kind, &a.Size, &a.CreatedAt); err != nil {
			continue
		}
		if msgID.Valid {
			a.MessageID = &msgID.Int64
			out[msgID.Int64] = append(out[msgID.Int64], a)
		}
	}
	return out, nil
}

func linkAttachmentsToMessage(messageID int64, ownerUUID string, attachmentIDs []int64) ([]attachmentRow, error) {
	if len(attachmentIDs) == 0 {
		return nil, nil
	}
	placeholders := make([]any, 0, len(attachmentIDs)+1)
	qmarks := make([]string, 0, len(attachmentIDs))
	placeholders = append(placeholders, ownerUUID)
	for _, id := range attachmentIDs {
		placeholders = append(placeholders, id)
		qmarks = append(qmarks, "?")
	}
	q := `SELECT id, message_id, owner_uuid, stored_name, orig_name, mime_type, kind, size, created_at
              FROM attachments
              WHERE owner_uuid = ? AND id IN (` + strings.Join(qmarks, ",") + `) AND message_id IS NULL`
	rows, err := db.Query(q, placeholders...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var linked []attachmentRow
	for rows.Next() {
		var a attachmentRow
		var msgID sql.NullInt64
		if err := rows.Scan(&a.ID, &msgID, &a.OwnerUUID, &a.StoredName, &a.OrigName,
			&a.MimeType, &a.Kind, &a.Size, &a.CreatedAt); err != nil {
			continue
		}
		_, err := db.Exec(`UPDATE attachments SET message_id = ? WHERE id = ?`, messageID, a.ID)
		if err != nil {
			log.Printf("[uploads] link att %d → msg %d: %v", a.ID, messageID, err)
			continue
		}
		a.MessageID = &messageID
		linked = append(linked, a)
	}
	return linked, nil
}

func handleUploadCreate(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	if me == nil {
		writeJSON(w, http.StatusUnauthorized, M{"error": "unauthorized"})
		return
	}

	r.Body = http.MaxBytesReader(w, r.Body, uploadMaxFileBytes+64*1024)

	if err := r.ParseMultipartForm(uploadMultipartMemory); err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_form", "detail": err.Error()})
		return
	}

	file, header, err := r.FormFile("file")
	if err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "missing_file"})
		return
	}
	defer file.Close()

	if header.Size <= 0 {
		writeJSON(w, http.StatusBadRequest, M{"error": "empty_file"})
		return
	}
	if header.Size > uploadMaxFileBytes {
		writeJSON(w, http.StatusRequestEntityTooLarge, M{
			"error": "file_too_large",
			"max":   uploadMaxFileBytes,
			"size":  header.Size,
		})
		return
	}

	mime := header.Header.Get("Content-Type")
	mime = strings.ToLower(strings.TrimSpace(strings.Split(mime, ";")[0]))
	if mime == "" {
		mime = sniffMimeFromExt(header.Filename)
	}
	kind, ok := allowedMime[mime]
	if !ok {
		writeJSON(w, http.StatusUnsupportedMediaType, M{
			"error":   "unsupported_type",
			"mime":    mime,
			"allowed": allowedMimeKeys(),
		})
		return
	}

	now := time.Now()
	used, remaining, err := quotaRemaining(me.UUID, now)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	if remaining < header.Size {
		writeJSON(w, http.StatusRequestEntityTooLarge, M{
			"error":        "quota_exceeded",
			"quota_bytes":  uploadQuotaBytes,
			"used_bytes":   used,
			"remaining":    remaining,
			"needed":       header.Size,
			"reset_in_sec": int((24 * time.Hour).Seconds()),
		})
		return
	}

	dir := userUploadDir(me.UUID, now)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		log.Printf("[uploads] mkdir %s: %v", dir, err)
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	ext := strings.ToLower(filepath.Ext(header.Filename))
	storedName := now.UTC().Format("20060102T150405") + "_" + randomToken() + ext
	dstPath := filepath.Join(dir, storedName)

	dst, err := os.OpenFile(dstPath, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o644)
	if err != nil {
		log.Printf("[uploads] create %s: %v", dstPath, err)
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	written, err := io.Copy(dst, file)
	dst.Close()
	if err != nil {
		_ = os.Remove(dstPath)
		log.Printf("[uploads] write %s: %v", dstPath, err)
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	if written != header.Size {
		_ = os.Remove(dstPath)
		writeJSON(w, http.StatusInternalServerError, M{"error": "size_mismatch"})
		return
	}

	origName := strings.TrimSpace(header.Filename)
	if origName == "" {
		origName = "file" + ext
	}
	if len(origName) > 255 {
		origName = origName[:255]
	}

	nowMilli := nowMillis()
	res, err := db.Exec(`
                INSERT INTO attachments
                        (owner_uuid, stored_name, orig_name, mime_type, kind, size, created_at, message_id)
                VALUES (?, ?, ?, ?, ?, ?, ?, NULL)
        `, me.UUID, storedName, origName, mime, kind, written, nowMilli)
	if err != nil {
		_ = os.Remove(dstPath)
		log.Printf("[uploads] insert row: %v", err)
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	attID, _ := res.LastInsertId()

	usedAfter := used + written
	remainingAfter := uploadQuotaBytes - usedAfter
	if remainingAfter < 0 {
		remainingAfter = 0
	}

	apiBase := r.Header.Get("X-Forwarded-Prefix")
	if apiBase == "" {
		apiBase = ""
	}

	writeJSON(w, http.StatusCreated, M{
		"ok": true,
		"attachment": attachmentJSON{
			ID:        attID,
			Name:      origName,
			Mime:      mime,
			Kind:      kind,
			Size:      written,
			URL:       apiBase + "/api/uploads/" + storedName,
			CreatedAt: nowMilli,
		},
		"quota": M{
			"used":      usedAfter,
			"remaining": remainingAfter,
			"limit":     uploadQuotaBytes,
		},
	})
}

func handleUploadGet(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	if me == nil {
		writeJSON(w, http.StatusUnauthorized, M{"error": "unauthorized"})
		return
	}
	storedName := lastPathSegment(r.URL.Path, "/api/uploads")
	if storedName == "" || strings.Contains(storedName, "/") || strings.Contains(storedName, "..") {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_name"})
		return
	}

	var (
		ownerUUID string
		mimeType  string
		origName  string
		size      int64
	)
	err := db.QueryRow(`
                SELECT owner_uuid, mime_type, orig_name, size
                FROM attachments
                WHERE stored_name = ?
        `, storedName).Scan(&ownerUUID, &mimeType, &origName, &size)
	if err == sql.ErrNoRows {
		writeJSON(w, http.StatusNotFound, M{"error": "not_found"})
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	if ownerUUID != me.UUID && !areFriends(me.UUID, ownerUUID) {
		writeJSON(w, http.StatusForbidden, M{"error": "forbidden"})
		return
	}

	matches, _ := filepath.Glob(filepath.Join(uploadDir(), "*", "*", storedName))
	if len(matches) == 0 {
		writeJSON(w, http.StatusNotFound, M{"error": "file_missing"})
		return
	}

	w.Header().Set("Content-Type", mimeType)
	w.Header().Set("Content-Length", strconv.FormatInt(size, 10))
	w.Header().Set("Content-Disposition",
		`inline; filename="`+sanitizeFilename(origName)+`"`)
	w.Header().Set("Cache-Control", "private, max-age=3600")
	w.Header().Set("X-Content-Type-Options", "nosniff")

	http.ServeFile(w, r, matches[0])
}

func handleUploadQuota(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	if me == nil {
		writeJSON(w, http.StatusUnauthorized, M{"error": "unauthorized"})
		return
	}
	used, remaining, err := quotaRemaining(me.UUID, time.Now())
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	writeJSON(w, http.StatusOK, M{
		"used":         used,
		"remaining":    remaining,
		"limit":        uploadQuotaBytes,
		"reset_in_sec": int((24 * time.Hour).Seconds()),
	})
}

func sniffMimeFromExt(name string) string {
	ext := strings.ToLower(filepath.Ext(name))
	switch ext {
	case ".png":
		return "image/png"
	case ".jpg", ".jpeg":
		return "image/jpeg"
	case ".gif":
		return "image/gif"
	case ".webp":
		return "image/webp"
	case ".bmp":
		return "image/bmp"
	case ".mp3":
		return "audio/mpeg"
	case ".ogg":
		return "audio/ogg"
	case ".oga":
		return "audio/ogg"
	case ".wav":
		return "audio/wav"
	case ".m4a":
		return "audio/mp4"
	case ".aac":
		return "audio/aac"
	case ".mp4":
		return "video/mp4"
	case ".webm":
		return "video/webm"
	case ".mov":
		return "video/quicktime"
	case ".mkv":
		return "video/x-matroska"
	}
	return ""
}

func allowedMimeKeys() []string {
	out := make([]string, 0, len(allowedMime))
	for k := range allowedMime {
		out = append(out, k)
	}
	return out
}

func sanitizeFilename(s string) string {
	var b strings.Builder
	for _, r := range s {
		if r < 0x20 || r == '"' || r == '\\' {
			continue
		}
		b.WriteRune(r)
	}
	if b.Len() == 0 {
		return "file"
	}
	return b.String()
}
