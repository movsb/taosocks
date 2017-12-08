package main

import (
    "net"
    "crypto/tls"
    "log"
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
        log.Printf("error dial server: %s\n", err)
        return nil, err
    }

    _, err = conn.Write([]byte("GET /?token=taosocks HTTP/1.1\r\n\r\n"))
    if err != nil {
        conn.Close()
        return nil, err
    }

    return conn, nil
}


