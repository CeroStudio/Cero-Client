package main

import (
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"
)

func securityHeaders(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		h := w.Header()
		h.Set("X-Content-Type-Options", "nosniff")
		h.Set("X-Frame-Options", "DENY")
		h.Set("X-DNS-Prefetch-Control", "off")
		h.Set("X-Download-Options", "noopen")
		h.Set("X-Permitted-Cross-Domain-Policies", "none")
		h.Set("Referrer-Policy", "no-referrer")
		h.Set("Strict-Transport-Security", "max-age=15552000; includeSubDomains")
		h.Set("Cross-Origin-Opener-Policy", "same-origin")
		h.Set("Cross-Origin-Resource-Policy", "same-origin")
		next.ServeHTTP(w, r)
	})
}

func corsMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		origin := r.Header.Get("Origin")
		if origin != "" {
			w.Header().Set("Access-Control-Allow-Origin", origin)
			w.Header().Set("Vary", "Origin")
		}
		w.Header().Set("Access-Control-Allow-Methods", "GET,POST,PATCH,DELETE,OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")

		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}

type slidingLimiter struct {
	mu       sync.Mutex
	window   time.Duration
	max      int
	counters map[string]*windowCount
}

type windowCount struct {
	count     int
	windowEnd time.Time
}

func newLimiter(window time.Duration, max int) *slidingLimiter {
	l := &slidingLimiter{window: window, max: max, counters: make(map[string]*windowCount)}
	go l.cleanupLoop()
	return l
}

func (l *slidingLimiter) cleanupLoop() {
	ticker := time.NewTicker(time.Minute)
	for range ticker.C {
		now := time.Now()
		l.mu.Lock()
		for k, v := range l.counters {
			if now.After(v.windowEnd) {
				delete(l.counters, k)
			}
		}
		l.mu.Unlock()
	}
}

func (l *slidingLimiter) allow(key string) (bool, int) {
	l.mu.Lock()
	defer l.mu.Unlock()

	now := time.Now()
	wc, ok := l.counters[key]
	if !ok || now.After(wc.windowEnd) {
		wc = &windowCount{count: 0, windowEnd: now.Add(l.window)}
		l.counters[key] = wc
	}
	wc.count++
	remaining := l.max - wc.count
	if remaining < 0 {
		remaining = 0
	}
	return wc.count <= l.max, remaining
}

func clientKey(r *http.Request) string {
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		return strings.TrimSpace(strings.Split(xff, ",")[0])
	}
	return r.RemoteAddr
}

var (
	generalLimiter  = newLimiter(60*time.Second, 120)
	messagesLimiter = newLimiter(10*time.Second, 20)
)

func generalAPIRateLimit(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !strings.HasPrefix(r.URL.Path, "/api/") {
			next.ServeHTTP(w, r)
			return
		}
		ok, remaining := generalLimiter.allow(clientKey(r))
		w.Header().Set("RateLimit-Limit", strconv.Itoa(generalLimiter.max))
		w.Header().Set("RateLimit-Remaining", strconv.Itoa(remaining))
		if !ok {
			writeJSON(w, http.StatusTooManyRequests, M{"error": "too_many_requests"})
			return
		}
		next.ServeHTTP(w, r)
	})
}

func messagesRateLimit(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !strings.HasPrefix(r.URL.Path, "/api/messages") {
			next.ServeHTTP(w, r)
			return
		}
		if r.Method == http.MethodGet {
			next.ServeHTTP(w, r)
			return
		}
		ok, _ := messagesLimiter.allow(clientKey(r))
		if !ok {
			writeJSON(w, http.StatusTooManyRequests, M{"error": "rate_limited"})
			return
		}
		next.ServeHTTP(w, r)
	})
}
