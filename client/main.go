package main

import (
    "fmt"
    "net"
    "io"
    "sync"
    "encoding/gob"
    "flag"
    "crypto/tls"
    "taosocks/internal"
)

type Config struct {
    Listen  string
    Server  string
    Secure  bool
}

var config Config
var filter Filter

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

    version := []byte{0}
    if _, err := conn.Read(version); err != nil {
        return err
    }

    if version[0] != 0x05 {
        return fmt.Errorf("bad version: %d", version[0])
    }

    authCount := []byte{0}
    if _, err := conn.Read(authCount); err != nil {
        return fmt.Errorf("bad authCount")
    }

    authMethods := make([]byte, authCount[0])
    if _, err := conn.Read(authMethods); err != nil {
        return fmt.Errorf("authMethods: %s", err)
    }

    var haveNoAuth bool = false
    for authMethod := range authMethods {
        if authMethod == 0x00 {
            haveNoAuth = true
            break
        }
    }

    if !haveNoAuth {
        return fmt.Errorf("No acceptable auth method provided.")
    }

    authReply := []byte{5,0}
    if _, err := conn.Write(authReply); err != nil {
        return fmt.Errorf("Failed to write auth method")
    }

    if _, err := conn.Read(version); err != nil {
        return fmt.Errorf("error")
    }

    if version[0] != 5 {
        return fmt.Errorf("bad version")
    }

    command := []byte{0}
    if _, err := conn.Read(command); err != nil {
        return fmt.Errorf("err: %s", err)
    }

    if command[0] != 1 {
        return fmt.Errorf("bad command")
    }

    if _, err := conn.Read(command); err != nil {
        return fmt.Errorf("err:%s", err)
    }

    if command[0] != 0 {
        return fmt.Errorf("bad reserved")
    }

    addrType := []byte{0}
    if _, err := conn.Read(addrType); err != nil {
        return fmt.Errorf("err:%s", err)
    }

    var strAddr string

    if addrType[0] == 1 {
        ipv4 := []byte{0,0,0,0}
        if _, err := io.ReadAtLeast(conn, ipv4, 4); err != nil {
            return fmt.Errorf("err:%s", err)
        }

        strAddr = net.IP(ipv4).String()
    } else if addrType[0] == 3 {
        nameLen := []byte{0}
        if _, err := conn.Read(nameLen); err != nil {
            return fmt.Errorf("bad length")
        }

        name := make([]byte, nameLen[0])
        if _, err := io.ReadAtLeast(conn, name, int(nameLen[0])); err != nil {
            return fmt.Errorf("err:%s", err)
        }

        strAddr = string(name)
    } else {
        return fmt.Errorf("bad addr")
    }

    portNumberArray := []byte{0,0}
    if _, err := conn.Read(portNumberArray); err != nil {
        return fmt.Errorf("bad port")
    }

    portNumber := int(portNumberArray[0]) * 256 + int(portNumberArray[1])

    addr := fmt.Sprintf("%s:%d", strAddr, portNumber)

    var proxyType ProxyType = Direct
    if addrType[0] == 1 {
        proxyType = filter.Test(strAddr, IPv4)
    } else if addrType[0] == 3 {
        proxyType = filter.Test(strAddr, Domain)
    }

    switch proxyType {
    case Direct:
        s.localRelay(addr, conn)
    case Proxy:
        s.remoteRelay(addr, conn)
    case Reject:
    }

    return nil
}

func (s *Server) localRelay(addr string, conn net.Conn) {
    conn2, err := net.Dial("tcp", addr)
    if err != nil {
        conn.Close()
        if conn2 != nil {
            conn2.Close()
        }
        fmt.Printf("Dial host:%s -  %s\n", addr, err)
        return
    }

    defer conn.Close()
    defer conn2.Close()

    reply := []byte{5,0,0,1,0,0,0,0,0,0}
    conn.Write(reply)

    fmt.Printf("> [Direct] %s\n", addr)

    wg := &sync.WaitGroup{}
    wg.Add(2)

    go func() {
        io.Copy(conn2, conn)
        wg.Done()
    }()

    go func() {
        io.Copy(conn, conn2)
        wg.Done()
    }()

    wg.Wait()

    fmt.Printf("< %s\n", addr)
}

func (s *Server) remoteRelay(addr string, conn net.Conn) {
    tlsconf := &tls.Config {
        InsecureSkipVerify: !config.Secure,
    }

    conn2, err := tls.Dial("tcp", config.Server, tlsconf)
    if err != nil {
        conn.Close()
        if conn2 != nil {
            conn2.Close()
        }
        fmt.Printf("Dial server: %s\n", err)
        return
    }

    defer conn2.Close()

    _, err = conn2.Write([]byte("GET /?token=taosocks HTTP/1.1\r\n\r\n"))
    if err != nil {
        return
    }

    enc := gob.NewEncoder(conn2)
    dec := gob.NewDecoder(conn2)

    err = enc.Encode(internal.OpenPacket{addr})
    if err != nil {
        return// fmt.Errorf("error enc")
    }

    var oapkt internal.OpenAckPacket
    err = dec.Decode(&oapkt)
    if err != nil {
        return// fmt.Errorf("error dec")
    }

    fmt.Printf("> [Proxy]  %s\n", addr)

    reply := []byte{5,0,0,1,0,0,0,0,0,0}

    if !oapkt.Status {
        reply[1] = 1
    }

    conn.Write(reply)

    if !oapkt.Status {
        return// nil
    }

    wg := &sync.WaitGroup{}

    wg.Add(2)

    go func() {
        relay1(enc, conn, conn2)
        wg.Done()
    }()

    go func() {
        relay2(conn, dec)
        wg.Done()
    }()

    wg.Wait()

    fmt.Printf("< %s\n", addr)
}

func relay1(enc *gob.Encoder, conn net.Conn, conn2 net.Conn) {
    defer conn.Close()
    defer conn2.Close()

    buf := make([]byte, 1024)

    for {
        var pkt internal.RelayPacket
        n, err := conn.Read(buf)
        if err != nil {
            return
        }

        pkt.Data = buf[:n]

        err = enc.Encode(pkt)
        if err != nil {
            return
        }
    }
}

func relay2(conn net.Conn, dec *gob.Decoder) {
    for {
        var pkt internal.RelayPacket
        err := dec.Decode(&pkt)
        if err != nil {
            return
        }

        _, err = conn.Write(pkt.Data)
        if err != nil {
            return
        }
    }
}

func parseConfig() {
    flag.StringVar(&config.Listen, "listen", "127.0.0.1:1080", "listen address(host:port)")
    flag.StringVar(&config.Server, "server", "127.0.0.1:1081", "server address(host:port)")
    flag.BoolVar(&config.Secure, "secure", true, "verify server certificate")
    flag.Parse()
}

func main() {
    parseConfig()
    filter.Init("../config/rules.txt")

    s :=  Server{}
    s.Run("tcp", config.Listen)
}

