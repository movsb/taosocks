package main

import (
	"bufio"
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
