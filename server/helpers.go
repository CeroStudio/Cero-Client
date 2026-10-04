package main

import (
	"encoding/json"
	"net/http"
	"strings"
)

type M map[string]any

func writeJSON(w http.ResponseWriter, status int, body any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(body)
}

func readJSON(r *http.Request, dst any) error {
	defer r.Body.Close()
	return json.NewDecoder(r.Body).Decode(dst)
}

func apiBaseFromRequest(r *http.Request) string {
	scheme := "http"
	if r.TLS != nil {
		scheme = "https"
	}
	if x := r.Header.Get("X-Forwarded-Proto"); x != "" {
		scheme = x
	}
	host := r.Host
	if x := r.Header.Get("X-Forwarded-Host"); x != "" {
		host = x
	}
	prefix := r.Header.Get("X-Forwarded-Prefix")
	prefix = strings.TrimRight(prefix, "/")
	return scheme + "://" + host + prefix
}
