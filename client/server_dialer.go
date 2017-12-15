package main

import (
    "net"
    "net/http"
    "crypto/tls"
    "errors"
    "bufio"
)

const (
    kToken   string = "taosocks"
    kVersion string = "taosocks/20171209"
)

type ServerDialer struct {

}

func (s *ServerDialer) Dial(server string, insecure bool) (net.Conn, error) {
    tlscfg := &tls.Config {
        InsecureSkipVerify: insecure,
    }

    conn, err := tls.Dial("tcp", server, tlscfg)
    if err != nil {
        if conn != nil {
            conn.Close()
        }
        return nil, err
    }

    req := s.createRequest(server)
    err = req.Write(conn)
    if err != nil {
        conn.Close()
        return nil, err
    }

    err = s.readResponse(conn)
    if err != nil {
        conn.Close()
        return nil, err
    }

    return conn, nil
}

func (s *ServerDialer) createRequest(server string) *http.Request {
    req, _ := http.NewRequest("GET", "/", nil)

    req.Host = server

    req.Header.Add("Connection", "upgrade")
    req.Header.Add("Upgrade", kVersion)
    req.Header.Add("Token", kToken)

    return req
}

func (s *ServerDialer) readResponse(conn net.Conn) error {
    bio := bufio.NewReader(conn)

    res, err := http.ReadResponse(bio, nil)
    if err != nil {
        return err
    }

    res.Body.Close()

    if res.StatusCode != 101 {
        return errors.New("server upgrade protocol error")
    }

    return nil
}

