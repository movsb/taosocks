package main

import (
    "fmt"
    "net"
    "flag"
    "io"
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

        // Chrome passes IP as domain
        if net.ParseIP(strAddr).To4() != nil {
            addrType[0] = 1
        }
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

    var rr Relayer

    switch proxyType {
    case Direct:
        rr = &LocalRelayer{}
    case Proxy:
        rr = &RemoteRelayer{
            Server: config.Server,
            Secure: config.Secure,
        }
    case Reject:
    }

    if rr != nil {
        if rr.Begin(addr, conn) {
            conn.Write([]byte{5,0,0,1,0,0,0,0,0,0})
            rr.Relay()
        } else {
            conn.Write([]byte{5,1,0,1,0,0,0,0,0,0})
            conn.Close()
        }
    }

    return nil
}

func parseConfig() {
    flag.StringVar(&config.Listen, "listen", "127.0.0.1:1080", "listen address(host:port)")
    flag.StringVar(&config.Server, "server", "127.0.0.1:1081", "server address(host:port)")
    flag.BoolVar(&config.Secure, "secure", true, "verify server certificate")
    flag.Parse()
}

func main() {
    parseConfig()
    filter.Init("config/whites.txt")

    s :=  Server{}
    s.Run("tcp", config.Listen)
}

