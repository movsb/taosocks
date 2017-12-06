package main

import (
    "io"
    "net"
    "flag"
    "fmt"
    "bufio"
    "os"
    "regexp"
)

type Config struct {
    Listen  string
}

var config Config

type Server struct {

}

func (s *Server) Run(network, addr string) error {
    l, err := net.Listen(network, addr)

    if err != nil {
        panic(err)
    }

    for {
        conn, err := l.Accept()

        if err != nil {
            panic(err)
        }

        go s.handle(conn)
    }

    return nil
}

func (s *Server) handle(conn net.Conn) error {
    defer conn.Close()

    scanner := bufio.NewScanner(conn)

    var r bool

    r = scanner.Scan()
    if !r {
        return scanner.Err()
    }

    re := regexp.MustCompile(" +")
    req := re.Split(scanner.Text(), -1)
    if  len(req) != 3 {
        fmt.Println(req)
        return nil
    }

    fmt.Printf("VERB: [%s]\nPATH: [%s]\nVERS: [%s]\n\n", req[0], req[1], req[2])

    for scanner.Scan() {
        if scanner.Text() == "" {
            break
        }
    }

    if scanner.Err() != nil {
        return nil
    }

    path := req[1][1:]
    if path == "" {
        path = "index.html"
    }

    file, err := os.Open(path)
    if err != nil {
        conn.Write([]byte("HTTP/1.1 403 Forbidden\r\n"))
        conn.Write([]byte("\r\n"))
        return nil
    }

    defer file.Close()

    stat, err := file.Stat()
    if err != nil {
        conn.Write([]byte("HTTP/1.1 403 Forbidden\r\n"))
        conn.Write([]byte("\r\n"))
        return nil
    }

    conn.Write([]byte("HTTP/1.1 200 OK\r\n"))
    conn.Write([]byte(fmt.Sprintf("Content-Length: %d\r\n", stat.Size())))
    conn.Write([]byte("Connection: close\r\n"))
    conn.Write([]byte("\r\n"))

    io.Copy(conn, file)

    fmt.Println("closing")

    return nil
}


func parseConfig() {
    flag.StringVar(&config.Listen, "listen", "127.0.0.1:1081", "listen address(host:port)")
    flag.Parse()
}

func main() {
    parseConfig()

    s :=  Server{}
    s.Run("tcp", config.Listen)
}

