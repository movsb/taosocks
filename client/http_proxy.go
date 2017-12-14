package main

import (
    "bytes"
    "strings"
    "net/http"
    "bufio"
    "net"
)

type HTTPProxy struct {
    conn    net.Conn
    bio     *bufio.ReadWriter
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

func(h *HTTPProxy) req2host(r *http.Request) string {
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

func (h *HTTPProxy) creqteRelayer(r *http.Request) Relayer {
    host := h.req2host(r)
    var proxyType = Direct
    var isIP = net.ParseIP(host).To4() != nil
    if isIP {
        proxyType = filter.Test(host, IPv4)
    } else {
        proxyType = filter.Test(host, Domain)
    }

    var rr Relayer
    
    switch proxyType {
    case Direct:
        rr = &LocalRelayer{}
    case Proxy:
        rr = &RemoteRelayer{
            Server: config.Server,
            Insecure: config.Insecure,
        }
    case Reject:
    }

    return rr
}

func (h *HTTPProxy) handleConnect(r *http.Request) {
    var rr = h.creqteRelayer(r)
    if rr != nil {
        if rr.Begin(h.host2addr(r), h.conn) {
            h.conn.Write([]byte("HTTP/1.1 200 OK\r\n\r\n"))
            rr.Relay()
            rr.End()
        }
    }
}

func (h *HTTPProxy) handlePlain(r *http.Request) {
    var rr = h.creqteRelayer(r)
    if rr != nil {
        if rr.Begin(h.host2addr(r), h.conn) {
            rr.ToRemote(h.req2bytes(r))
            rr.Relay()
            rr.End()
        }
    }
}
