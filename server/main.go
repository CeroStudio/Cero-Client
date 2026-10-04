package main

import (
	"context"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"
)

var hub *Hub

func main() {
	initDB()
	defer db.Close()

	hub = newHub()

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGTERM, syscall.SIGINT)
	defer signal.Stop(stop)

	mux := http.NewServeMux()

	mux.HandleFunc("GET /health", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, M{"ok": true, "time": time.Now().UnixMilli()})
	})

	mux.HandleFunc("/ws", hub.handleWS)

	mux.HandleFunc("GET /api/friends", authMiddleware(handleFriendsList))
	mux.HandleFunc("GET /api/friends/requests", authMiddleware(handleFriendRequests))
	mux.HandleFunc("POST /api/friends/add", authMiddleware(handleFriendAdd))
	mux.HandleFunc("POST /api/friends/accept", authMiddleware(handleFriendAccept))
	mux.HandleFunc("POST /api/friends/block", authMiddleware(handleFriendBlock))
	mux.HandleFunc("DELETE /api/friends/{uuid}", authMiddleware(handleFriendDelete))

	mux.HandleFunc("GET /api/messages/unread/count", authMiddleware(handleUnreadCount))
	mux.HandleFunc("GET /api/messages/{uuid}", authMiddleware(handleMessagesGet))
	mux.HandleFunc("POST /api/messages", authMiddleware(handleMessagesSend))
	mux.HandleFunc("PATCH /api/messages/{id}", authMiddleware(handleMessagesEdit))
	mux.HandleFunc("DELETE /api/messages/{id}", authMiddleware(handleMessagesDelete))

	mux.HandleFunc("POST /api/uploads", authMiddleware(handleUploadCreate))
	mux.HandleFunc("GET /api/uploads/quota", authMiddleware(handleUploadQuota))
	mux.HandleFunc("GET /api/uploads/{name}", authMiddleware(handleUploadGet))

	mux.HandleFunc("GET /api/library/skins", authMiddleware(handleLibrarySkinsList))
	mux.HandleFunc("POST /api/library/skins", authMiddleware(handleLibrarySkinsUpload))
	mux.HandleFunc("POST /api/library/skins/{id}/apply", authMiddleware(handleLibrarySkinsApply))
	mux.HandleFunc("PATCH /api/library/skins/{id}", authMiddleware(handleLibrarySkinsUpdate))
	mux.HandleFunc("DELETE /api/library/skins/{id}", authMiddleware(handleLibrarySkinsDelete))
	mux.HandleFunc("GET /api/library/skins/quota", authMiddleware(handleLibrarySkinsQuota))
	mux.HandleFunc("GET /api/library/skins/{id}/file", authMiddleware(handleLibrarySkinsFile))

	var handler http.Handler = mux
	handler = messagesRateLimit(handler)
	handler = generalAPIRateLimit(handler)
	handler = corsMiddleware(handler)
	handler = securityHeaders(handler)
	handler = recoverMiddleware(handler)

	port := os.Getenv("PORT")
	if port == "" {
		port = "3134"
	}

	srv := &http.Server{
		Addr:         ":" + port,
		Handler:      handler,
		ReadTimeout:  60 * time.Second,
		WriteTimeout: 120 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	go func() {
		log.Printf("CeroClient server on http://localhost:%s", port)

		if err := srv.ListenAndServe(); err != nil &&
			err != http.ErrServerClosed {
			log.Fatalf("listen: %v", err)
		}
	}()

	ctx, cancel := signal.NotifyContext(
		context.Background(),
		syscall.SIGINT,
		syscall.SIGTERM,
	)
	defer cancel()

	go runShell(ctx, cancel)

	<-ctx.Done()

	log.Println("shutdown signal received, draining connections...")

	shutdownCtx, shutdownCancel := context.WithTimeout(
		context.Background(),
		25*time.Second,
	)
	defer shutdownCancel()

	if err := srv.Shutdown(shutdownCtx); err != nil {
		log.Printf("shutdown error: %v", err)
	}

	log.Println("shutdown complete")
}

func recoverMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer func() {
			if err := recover(); err != nil {
				log.Printf("panic: %v", err)
				writeJSON(w, http.StatusInternalServerError, M{"error": "internal"})
			}
		}()
		next.ServeHTTP(w, r)
	})
}
