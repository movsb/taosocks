package main

import (
    "fmt"
    "io"
    "bufio"
    "net"
)

const (
    v4  byte = 0x04
    v4a byte = 0x04
    v5  byte = 0x05 
)

const (
    tcpStream   byte = 0x01
)

const (
    addrTypeIPv4    byte = 0x01
    addrTypeDomain  byte = 0x03
    addrTypeIPv6    byte = 0x04
)

func readn(bio *bufio.Reader, n uint) ([]byte, error) {
    s := make([]byte, n)
    _, err := io.ReadAtLeast(bio, s, int(n))
    return s, err
}

func writen(bio *bufio.Writer, b []byte) error {
    if _, err := bio.Write(b); err != nil {
        return err
    }

    if err := bio.Flush(); err != nil {
        return err
    }

    return nil
}

type SocksProxy struct {
    conn    net.Conn
    bio     *bufio.ReadWriter
}

func (s *SocksProxy) Handle(conn net.Conn, bio *bufio.ReadWriter) {
    s.conn = conn
    s.bio = bio

    s.handleV5()
}

func (s *SocksProxy) handleV5() {
    var bio = s.bio
    var err error

    if ver, err := bio.ReadByte(); err != nil || ver != v5 {
        logf("socks version error: %d\n", ver)
        return
    }

    var authCount byte
    if authCount, err = bio.ReadByte(); err != nil || authCount == 0 {
        logf("socks auth count error: %s\n", err)
        return
    }

    var authMethods []byte
    if authMethods, err = readn(bio.Reader, uint(authCount)); err != nil {
        logf("auth methods error: %s\n", err)
        return
    }

    haveNoAuth := false
    for authMethod := range authMethods {
        if authMethod == 0x00 {
            haveNoAuth = true
            break
        }
    }

    if !haveNoAuth {
        logn("No acceptable auth method provided.")
        return
    }

    authReply := []byte{5,0}
    if err := writen(bio.Writer, authReply); err != nil {
        logf("write auth reply error: %s\n", err)
        return
    }
    
    if ver, err := bio.ReadByte(); err != nil || ver != v5 {
        logn("socks version error: %s\n", err)
        return
    }

    var command byte
    if command, err = bio.ReadByte(); err != nil {
        logf("read command error: %s\n", err)
        return
    }

    if command != tcpStream {
        logf("command not supported: %d\n", command)
        return
    }

    if rsv, err := bio.ReadByte(); err != nil || rsv != 0 {
        logf("reserved byte error: %s\n", err)
        return
    }

    var addrType byte
    if addrType, err = bio.ReadByte(); err != nil {
        logf("read address type error: %s\n", err)
        return
    }

    strAddr := ""
    
    switch addrType {
    case addrTypeIPv4:
        var ipv4 []byte
        if ipv4, err = readn(bio.Reader, 4); err != nil {
            logf("read ipv4 error: %s\n", err)
            return
        }

        strAddr = net.IP(ipv4).String()

    case addrTypeDomain:
        var nameLen byte
        if nameLen, err = bio.ReadByte(); err != nil {
            logf("read domain len error: %s\n", err)
            return
        }

        var nameBytes []byte
        if nameBytes, err = readn(bio.Reader, uint(nameLen)); err != nil {
            logf("read domain error: %s\n", err)
            return
        }

        strAddr = string(nameBytes)

        // Chrome passes IP as domain
        if net.ParseIP(strAddr).To4() != nil {
            addrType = addrTypeIPv4
        }
    default:
        logf("unknown address type: %d\n", addrType)
        return
    }

    var port uint16

    if portArray, err := readn(bio.Reader, 2); err != nil {
        logf("read port error: %s\n", err)
        return
    } else {
        port = uint16(portArray[0]) << 8 + uint16(portArray[1])
    }

    strAddr += fmt.Sprintf(":%d", port)

    var proxyType ProxyType = Direct
    switch addrType {
    case addrTypeIPv4:
        proxyType = filter.Test(strAddr, IPv4)
    case addrTypeDomain:
        proxyType = filter.Test(strAddr, Domain)
    }

    var rr Relayer
    
    switch proxyType {
    case Direct:
        rr = &LocalRelayer{}
    case Proxy:
        rr = &RemoteRelayer{
            Server: config.Server,
            Insecure: config.Insecure,
        }
    case Reject:
    }

    if rr != nil {
        if rr.Begin(strAddr, s.conn) {
            s.conn.Write([]byte{5,0,0,1,0,0,0,0,0,0})
            rr.Relay()
        } else {
            s.conn.Write([]byte{5,1,0,1,0,0,0,0,0,0})
            s.conn.Close()
        }
    }
}
