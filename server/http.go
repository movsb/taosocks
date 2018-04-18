package main

import (
	"bufio"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"strings"
)

var logf = log.Printf
var logn = log.Println

var serverVersion = "taosocks/20171218"

type HTTP struct {
}

func (h *HTTP) Handle(conn net.Conn) bool {
	if h.handle(conn) {
		conn.Close()
		return true
	}
	return false
}

func (h *HTTP) handle(conn net.Conn) bool {
	bio := bufio.NewReader(conn)

	req, err := http.ReadRequest(bio)
	if err != nil {
		log.Println(err)
		return true
	}

	if h.tryUpgrade(conn, req) {
		return false
	}

	h.doProxy(conn, req)

	return true
}

func (h *HTTP) tryUpgrade(conn net.Conn, req *http.Request) bool {
	if req.Method == "GET" && req.URL.Path == "/" && strings.ToLower(req.Header.Get("Connection")) == "upgrade" {
		ver := req.Header.Get("Upgrade")
		user := req.Header.Get("Username")
		pass := req.Header.Get("Password")

		if ver == serverVersion {
			if auth.Test(user, pass) {
				conn.Write([]byte("HTTP/1.1 101 Switching Protocol\r\n"))
				conn.Write([]byte("\r\n"))
				return true
			} else {
				logf("bad user: %s:%s\n", user, pass)
			}
		} else {
			logf("bad version: %s\n", ver)
		}
	}

	return false
}

func (h *HTTP) doProxy(conn net.Conn, req *http.Request) {
	if !strings.HasPrefix(strings.ToLower(config.Server), "https://") {
		config.Server = "https://" + config.Server
	}
	u, err := req.URL.Parse(config.Server)
	u.Path = req.RequestURI
	req.URL = u
	req.Host = u.Host
	if req.URL.Scheme == "" {
		req.URL.Scheme = "https"
	}
	resp, err := http.DefaultTransport.RoundTrip(req)
	if err != nil {
		log.Printf("%v\n", req)
		log.Println(err)
		return
	}

	defer resp.Body.Close()

	conn.Write([]byte(fmt.Sprintf("%s %s\r\n", resp.Proto, resp.Status)))
	resp.Header.Write(conn)
	conn.Write([]byte("\r\n"))
	io.Copy(conn, resp.Body)
}
