package main

import (
    "io"
    "net"
    "fmt"
    "bufio"
    "os"
    "log"
    "regexp"
    "strings"
    neturl "net/url"
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
    var r bool

    scanner := bufio.NewScanner(conn)

    r = scanner.Scan()
    if !r {
        err := scanner.Err()
        if err != nil {
            logf("fatal: no data: %s\n", scanner.Err())
        }
        return true
    }

    re := regexp.MustCompile(" +")
    req := re.Split(scanner.Text(), -1)
    if  len(req) != 3 {
        logf("fatal: not http request")
        return true
    }

    verb := req[0]
    if strings.ToUpper(verb) != "GET" {
        conn.Write([]byte("HTTP/1.1 405 Method Not Allowed\r\n"))
        conn.Write([]byte("Allow: GET\r\n"))
        conn.Write([]byte("Connection: close\r\n"))
        conn.Write([]byte("\r\n"))
        return true
    }

    url, err := neturl.ParseRequestURI(req[1])
    if err != nil {
        logf("fatal: cannot parse uri: %s\n", req[1])
        return true
    }

    queries, err := neturl.ParseQuery(url.RawQuery)
    if err != nil {
        logf("fatal: bad query string: %s\n", url.RawQuery)
        return true
    }

    for scanner.Scan() {
        if scanner.Text() == "" {
            break
        }
    }

    if scanner.Err() != nil {
        logn("fatal: failed to read headers")
        return true
    }

    token := queries.Get("token")

    if url.Path == "/" && token == "taosocks" {
        return false
    }

    var path = url.Path[1:]
    if path == "" {
        path = "index.html"
    }

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

