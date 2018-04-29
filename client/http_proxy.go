package main

import (
	"bufio"
	"bytes"
	"net"
	"net/http"
	"strings"
)

type HTTPProxy struct {
	conn net.Conn
	bio  *bufio.ReadWriter
}

func (h *HTTPProxy) Handle(conn net.Conn, bio *bufio.ReadWriter) {
	h.bio = bio
	h.conn = conn

	req, err := http.ReadRequest(bio.Reader)
	if err != nil {
		logf("read request error: %s\n", err)
		return
	}

	if req.Method == http.MethodConnect {
		h.handleConnect(req)
	} else {
		h.handlePlain(req)
	}
}

func (h *HTTPProxy) host2addr(r *http.Request) string {
	if strings.Index(r.Host, ":") != -1 {
		return r.Host
	}
	return r.Host + ":80"
}

func (h *HTTPProxy) req2host(r *http.Request) string {
	index := strings.Index(r.Host, ":")
	if index != -1 {
		return r.Host[:index]
	}
	return r.Host
}

func (h *HTTPProxy) req2bytes(r *http.Request) []byte {
	var buf bytes.Buffer
	var bio = bufio.NewWriter(&buf)
	r.Write(bio)
	bio.Flush()
	return buf.Bytes()
}

func (h *HTTPProxy) handleConnect(r *http.Request) {
	sr := &SmartRelayer{}
	sr.Relay(h.host2addr(r), h.conn, func(r Relayer) error {
		return r.ToLocal([]byte("HTTP/1.1 200 OK\r\n\r\n"))
	})
}

func (h *HTTPProxy) handlePlain(r *http.Request) {
	sr := &SmartRelayer{}
	sr.Relay(h.host2addr(r), h.conn, func(rr Relayer) error {
		return rr.ToRemote(h.req2bytes(r))
	})
}
