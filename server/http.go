package main

import (
    "io"
    "net"
    "fmt"
    "bufio"
    "os"
    "log"
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
    } else {
        return false
    }
}

func (h *HTTP) handle(conn net.Conn) bool {
    bio := bufio.NewReader(conn)

    req, err := http.ReadRequest(bio)
    if err != nil {
        return true
    }

    if req.Method != "GET" {
        conn.Write([]byte("HTTP/1.1 405 Method Not Allowed\r\n"))
        conn.Write([]byte("Allow: GET\r\n"))
        conn.Write([]byte("Connection: close\r\n"))
        conn.Write([]byte("\r\n"))
        log.Printf("method not allowed: %s\n", req.Method)
        log.Println(req)
        return true
    }

    // How to prevent relative path?
    if req.URL.Path == "" || req.URL.Path[0] != []byte("/")[0] || strings.Contains(req.URL.Path, "/../") {
        conn.Write([]byte("HTTP/1.1 400 Bad Request\r\n\r\n"))
        log.Println("Bad request: ", req)
        return true
    }

    if req.URL.Path == "/" && strings.ToLower(req.Header.Get("Connection")) == "upgrade" {
        ver  := req.Header.Get("Upgrade")
        user := req.Header.Get("Username")
        pass := req.Header.Get("Password")

        var ok = false

        if ver == serverVersion {
            if auth.Test(user, pass) {
                conn.Write([]byte("HTTP/1.1 101 Switching Protocol\r\n"))
                conn.Write([]byte("\r\n"))
                ok = true
            } else {
                logf("bad user: %s:%s\n", user, pass)
            }
        } else {
            logf("bad version: %s\n", ver)
        }

        if ok {
            return false
        }
    }

    defer func() {
        log.Println(req)
    }()

    var path = req.URL.Path
    if path == "/" {
        path = "/index.html"
    }

    path = "www" + path

    file, err := os.Open(path)
    if err != nil {
        conn.Write([]byte("HTTP/1.1 404 Not Found\r\n"))
        conn.Write([]byte("\r\n"))
        log.Printf("File open error: %s\n", err)
        return true
    }

    defer file.Close()

    stat, err := file.Stat()
    if err != nil {
        conn.Write([]byte("HTTP/1.1 403 Forbidden\r\n"))
        conn.Write([]byte("\r\n"))
        log.Printf("File not accessible: %s\n", path)
        return true
    }

    conn.Write([]byte("HTTP/1.1 200 OK\r\n"))
    conn.Write([]byte(fmt.Sprintf("Content-Length: %d\r\n", stat.Size())))
    conn.Write([]byte("Connection: close\r\n"))
    conn.Write([]byte("\r\n"))

    io.Copy(conn, file)

    return true
}

