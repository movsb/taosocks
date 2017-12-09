package main

import (
    "io"
    "net"
    "fmt"
    "bufio"
    "os"
    "log"
    "net/http"
)

var logf = log.Printf
var logn = log.Println

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
        return true
    }

    if req.URL.Path == "/" && req.Header.Get("Connection") == "upgrade" && req.Header.Get("Upgrade") == "taosocks/20171209" && req.Header.Get("Token") == "taosocks" {
        conn.Write([]byte("HTTP/1.1 101 Switching Protocol\r\n"))
        conn.Write([]byte("\r\n\r\n"))
        return false
    }

    var path = req.URL.Path
    if path == "/" {
        path = "/index.html"
    }

    path = "../www" + path

    file, err := os.Open(path)
    if err != nil {
        conn.Write([]byte("HTTP/1.1 404 Not Found\r\n"))
        conn.Write([]byte("\r\n"))
        return true
    }

    defer file.Close()

    stat, err := file.Stat()
    if err != nil {
        conn.Write([]byte("HTTP/1.1 403 Forbidden\r\n"))
        conn.Write([]byte("\r\n"))
        return true
    }

    conn.Write([]byte("HTTP/1.1 200 OK\r\n"))
    conn.Write([]byte(fmt.Sprintf("Content-Length: %d\r\n", stat.Size())))
    conn.Write([]byte("Connection: close\r\n"))
    conn.Write([]byte("\r\n"))

    io.Copy(conn, file)

    return true
}

