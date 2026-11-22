package main

import (
	"net"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"
)

func TestHealthProbesConfiguredPublicAddress(t *testing.T) {
	var redirect atomic.Bool
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch {
		case r.URL.Path == "/healthz" && redirect.Load():
			http.Redirect(w, r, "/other", http.StatusFound)
		case r.URL.Path == "/healthz" || r.URL.Path == "/other":
			w.WriteHeader(http.StatusOK)
		default:
			w.WriteHeader(http.StatusNotFound)
		}
	}))
	defer server.Close()
	_, port, err := net.SplitHostPort(server.Listener.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	for _, host := range []string{"127.0.0.1", "0.0.0.0", ""} {
		t.Setenv("MODSTORE_PUBLIC_ADDR", net.JoinHostPort(host, port))
		if err := run([]string{"health"}); err != nil {
			t.Fatalf("health on %q: %v", host, err)
		}
	}
	redirect.Store(true)
	if err := run([]string{"health"}); err == nil {
		t.Fatal("a redirected health check succeeded")
	}
}
