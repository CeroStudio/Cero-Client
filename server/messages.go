package main

import (
	"database/sql"
	"net/http"
	"strconv"
	"strings"
	"time"
)

type message struct {
	ID          int64            `json:"id"`
	From        string           `json:"from_uuid"`
	To          string           `json:"to_uuid"`
	Content     string           `json:"content"`
	SentAt      int64            `json:"sent_at"`
	ReadAt      *int64           `json:"read_at"`
	EditedAt    *int64           `json:"edited_at"`
	Attachments []attachmentJSON `json:"attachments,omitempty"`
}

func areFriends(a, b string) bool {
	x, y := pair(a, b)
	var status string
	err := db.QueryRow(`SELECT status FROM friendships WHERE user_a = ? AND user_b = ?`, x, y).Scan(&status)
	return err == nil && status == "accepted"
}

func lastPathSegment(path, prefix string) string {
	return strings.TrimPrefix(strings.TrimPrefix(path, prefix), "/")
}

func handleMessagesGet(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r).UUID
	other := lastPathSegment(r.URL.Path, "/api/messages")
	if other == "" {
		writeJSON(w, http.StatusNotFound, M{"error": "not_found"})
		return
	}
	if !areFriends(me, other) {
		writeJSON(w, http.StatusForbidden, M{"error": "not_friends"})
		return
	}

	limit := 50
	if v, err := strconv.Atoi(r.URL.Query().Get("limit")); err == nil {
		limit = v
	}
	if limit > 100 {
		limit = 100
	}
	before := time.Now().UnixMilli() + 1
	if v, err := strconv.ParseInt(r.URL.Query().Get("before"), 10, 64); err == nil {
		before = v
	}

	rows, err := db.Query(`
                SELECT id, from_uuid, to_uuid, content, sent_at, read_at, edited_at
                FROM messages
                WHERE ((from_uuid = ? AND to_uuid = ?) OR (from_uuid = ? AND to_uuid = ?))
                  AND sent_at < ?
                ORDER BY sent_at DESC
                LIMIT ?
        `, me, other, other, me, before, limit)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	defer rows.Close()

	var msgs []message
	for rows.Next() {
		var m message
		var readAt, editedAt sql.NullInt64
		if err := rows.Scan(&m.ID, &m.From, &m.To, &m.Content, &m.SentAt, &readAt, &editedAt); err != nil {
			continue
		}
		if readAt.Valid {
			m.ReadAt = &readAt.Int64
		}
		if editedAt.Valid {
			m.EditedAt = &editedAt.Int64
		}
		msgs = append(msgs, m)
	}

	for i, j := 0, len(msgs)-1; i < j; i, j = i+1, j-1 {
		msgs[i], msgs[j] = msgs[j], msgs[i]
	}
	if msgs == nil {
		msgs = []message{}
	}

	if len(msgs) > 0 {
		ids := make([]int64, 0, len(msgs))
		for _, m := range msgs {
			ids = append(ids, m.ID)
		}
		byMsg, err := fetchAttachmentsForMessages(ids)
		if err == nil {
			apiBase := apiBaseFromRequest(r)
			for i := range msgs {
				if atts, ok := byMsg[msgs[i].ID]; ok && len(atts) > 0 {
					msgs[i].Attachments = make([]attachmentJSON, 0, len(atts))
					for _, a := range atts {
						msgs[i].Attachments = append(msgs[i].Attachments, attachmentToJSON(a, apiBase))
					}
				}
			}
		}
	}

	_, _ = db.Exec(`UPDATE messages SET read_at = ? WHERE to_uuid = ? AND from_uuid = ? AND read_at IS NULL`,
		nowMillis(), me, other)

	writeJSON(w, http.StatusOK, M{"messages": msgs})
}

func handleMessagesSend(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	var body struct {
		To            string  `json:"to"`
		Content       string  `json:"content"`
		AttachmentIDs []int64 `json:"attachment_ids,omitempty"`
	}
	if err := readJSON(r, &body); err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_content"})
		return
	}

	clean := strings.TrimSpace(body.Content)


	hasAttachments := len(body.AttachmentIDs) > 0
	if clean == "" && !hasAttachments {
		writeJSON(w, http.StatusBadRequest, M{"error": "missing_fields"})
		return
	}
	if len(clean) > 1000 {
		writeJSON(w, http.StatusBadRequest, M{"error": "content_length", "max": 1000})
		return
	}
	if body.To == "" {
		writeJSON(w, http.StatusBadRequest, M{"error": "missing_fields"})
		return
	}
	if !areFriends(me.UUID, body.To) {
		writeJSON(w, http.StatusForbidden, M{"error": "not_friends"})
		return
	}

	now := nowMillis()
	res, err := db.Exec(`INSERT INTO messages (from_uuid, to_uuid, content, sent_at) VALUES (?, ?, ?, ?)`,
		me.UUID, body.To, clean, now)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	id, _ := res.LastInsertId()

	msg := message{ID: id, From: me.UUID, To: body.To, Content: clean, SentAt: now}

	if hasAttachments {
		linked, err := linkAttachmentsToMessage(id, me.UUID, body.AttachmentIDs)
		if err == nil && len(linked) > 0 {
			apiBase := apiBaseFromRequest(r)
			msg.Attachments = make([]attachmentJSON, 0, len(linked))
			for _, a := range linked {
				msg.Attachments = append(msg.Attachments, attachmentToJSON(a, apiBase))
			}
		}
	}

	hub.notify(body.To, M{"type": "message", "message": msg, "from": me})

	writeJSON(w, http.StatusOK, M{"ok": true, "message": msg})
}

func handleMessagesEdit(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	idStr := lastPathSegment(r.URL.Path, "/api/messages")
	id, err := strconv.ParseInt(idStr, 10, 64)
	if err != nil || id == 0 {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_id"})
		return
	}

	var body struct {
		Content string `json:"content"`
	}
	if err := readJSON(r, &body); err != nil {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_content"})
		return
	}
	clean := strings.TrimSpace(body.Content)
	if len(clean) == 0 || len(clean) > 1000 {
		writeJSON(w, http.StatusBadRequest, M{"error": "content_length", "max": 1000})
		return
	}

	var m message
	var readAt, editedAt sql.NullInt64
	err = db.QueryRow(`SELECT id, from_uuid, to_uuid, content, sent_at, read_at, edited_at FROM messages WHERE id = ?`, id).
		Scan(&m.ID, &m.From, &m.To, &m.Content, &m.SentAt, &readAt, &editedAt)
	if err == sql.ErrNoRows {
		writeJSON(w, http.StatusNotFound, M{"error": "not_found"})
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	if m.From != me.UUID {
		writeJSON(w, http.StatusForbidden, M{"error": "forbidden"})
		return
	}

	now := nowMillis()
	if _, err := db.Exec(`UPDATE messages SET content = ?, edited_at = ? WHERE id = ?`, clean, now, id); err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	m.Content = clean
	m.EditedAt = &now
	if readAt.Valid {
		m.ReadAt = &readAt.Int64
	}

	byMsg, err := fetchAttachmentsForMessages([]int64{m.ID})
	if err == nil && len(byMsg[m.ID]) > 0 {
		apiBase := apiBaseFromRequest(r)
		m.Attachments = make([]attachmentJSON, 0, len(byMsg[m.ID]))
		for _, a := range byMsg[m.ID] {
			m.Attachments = append(m.Attachments, attachmentToJSON(a, apiBase))
		}
	}

	hub.notify(m.To, M{"type": "message_edited", "message": m})
	writeJSON(w, http.StatusOK, M{"ok": true, "message": m})
}

func handleMessagesDelete(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r)
	idStr := lastPathSegment(r.URL.Path, "/api/messages")
	id, err := strconv.ParseInt(idStr, 10, 64)
	if err != nil || id == 0 {
		writeJSON(w, http.StatusBadRequest, M{"error": "invalid_id"})
		return
	}

	var fromUUID, toUUID string
	err = db.QueryRow(`SELECT from_uuid, to_uuid FROM messages WHERE id = ?`, id).Scan(&fromUUID, &toUUID)
	if err == sql.ErrNoRows {
		writeJSON(w, http.StatusNotFound, M{"error": "not_found"})
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	if fromUUID != me.UUID {
		writeJSON(w, http.StatusForbidden, M{"error": "forbidden"})
		return
	}

	if _, err := db.Exec(`DELETE FROM messages WHERE id = ?`, id); err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}

	hub.notify(toUUID, M{"type": "message_deleted", "id": id})
	writeJSON(w, http.StatusOK, M{"ok": true, "id": id})
}

func handleUnreadCount(w http.ResponseWriter, r *http.Request) {
	me := userFromCtx(r).UUID
	rows, err := db.Query(`
                SELECT from_uuid, COUNT(*) as count
                FROM messages
                WHERE to_uuid = ? AND read_at IS NULL
                GROUP BY from_uuid
        `, me)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
		return
	}
	defer rows.Close()

	type unreadRow struct {
		From  string `json:"from_uuid"`
		Count int    `json:"count"`
	}
	unread := []unreadRow{}
	for rows.Next() {
		var u unreadRow
		if rows.Scan(&u.From, &u.Count) == nil {
			unread = append(unread, u)
		}
	}

	writeJSON(w, http.StatusOK, M{"unread": unread})
}
