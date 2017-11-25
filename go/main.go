package main

import (
    "fmt"
    "net"
    "io"
)

type Server struct {

}

func (s *Server) Run(network, addr string) error {
    l, err := net.Listen(network, addr)

    if err != nil {
        panic(err)
    }

    for {
        conn, err := l.Accept()

        // fmt.Println("accepted...")

        if err != nil {
            panic(err)
        }

        go s.handle(conn)
    }

    return nil
}

func (s *Server) handle(conn net.Conn) error {
    // defer conn.Close()

    // Reads the version byte
    version := []byte{0}
    if _, err := conn.Read(version); err != nil {
        panic(err)
    }

    if version[0] != 0x05 {
        panic("bad version")
    }

    authCount := []byte{0}
    if _, err := conn.Read(authCount); err != nil {
        panic("bad authCount")
    }

    authMethods := make([]byte, authCount[0])
    if _, err := conn.Read(authMethods); err != nil {
        panic(err)
    }

    var haveNoAuth bool = false
    for authMethod := range authMethods {
        if authMethod == 0x00 {
            haveNoAuth = true
            break
        }
    }

    if !haveNoAuth {
        panic("No acceptable auth method provided.")
    }

    authReply := []byte{5,0}
    if _, err := conn.Write(authReply); err != nil {
        panic("Failed to write auth method")
    }

    if _, err := conn.Read(version); err != nil {
        panic("error")
    }

    if version[0] != 5 {
        panic("bad version")
    }

    command := []byte{0}
    if _, err := conn.Read(command); err != nil {
        panic(err)
    }

    if command[0] != 1 {
        panic("bad command")
    }

    if _, err := conn.Read(command); err != nil {
        panic(err)
    }

    if command[0] != 0 {
        panic("bad reserved")
    }

    addrType := []byte{0}
    if _, err := conn.Read(addrType); err != nil {
        panic(err)
    }

    var strAddr string

    if addrType[0] == 1 {
        ipv4 := []byte{0,0,0,0}
        if _, err := io.ReadAtLeast(conn, ipv4, 4); err != nil {
            panic(err)
        }

        strAddr = string(net.IP(ipv4))
    } else if addrType[0] == 3 {
        nameLen := []byte{0}
        if _, err := conn.Read(nameLen); err != nil {
            panic("bad length")
        }

        name := make([]byte, nameLen[0])
        if _, err := io.ReadAtLeast(conn, name, int(nameLen[0])); err != nil {
            panic(err)
        }

        strAddr = string(name)
    } else {
        panic("bad addr")
    }

    portNumberArray := []byte{0,0}
    if _, err := conn.Read(portNumberArray); err != nil {
        panic("bad port")
    }

    portNumber := int(portNumberArray[0]) * 256 + int(portNumberArray[1])

    fmt.Printf("addr:%s,port:%d\n", strAddr, portNumber)

    addr := fmt.Sprintf("%s:%d", strAddr, portNumber)

    conn2, err := net.Dial("tcp", addr)
    if err != nil {
        conn.Close()
        if conn2 != nil {
            conn2.Close()
        }
        fmt.Printf("failed to dial: %s\n", addr)
        return nil
    }

    reply := []byte{5,0,0,1,0,0,0,0,0,0}
    conn.Write(reply)

    go relay(conn, conn2, addr)
    go relay(conn2, conn, "")

    return nil
}

func relay(conn1, conn2 net.Conn, addr string) {
    io.Copy(conn1, conn2)
    conn1.Close()
    if addr != "" {
        fmt.Printf("Close connection to %s\n", addr)
    }
}

func main() {
    s :=  Server{}
    s.Run("tcp", "127.0.0.1:1080")
}

