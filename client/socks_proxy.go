package main

import (
	"bufio"
	"fmt"
	"io"
	"net"
)

const (
	v4  byte = 0x04
	v4a byte = 0x04
	v5  byte = 0x05
)

const (
	tcpStream byte = 0x01
)

const (
	addrTypeIPv4   byte = 0x01
	addrTypeDomain byte = 0x03
	addrTypeIPv6   byte = 0x04
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

	return bio.Flush()
}

// SocksProxy processes SOCKS protocols.
type SocksProxy struct {
	conn net.Conn
	bio  *bufio.ReadWriter
}

// Handle handles incoming SOCKS protocol connections.
func (s *SocksProxy) Handle(conn net.Conn, bio *bufio.ReadWriter) {
	s.conn = conn
	s.bio = bio

	s.handleV5()
}

func (s *SocksProxy) handleV5() {
	var bio = s.bio
	var err error

	if ver, err := bio.ReadByte(); err != nil || ver != v5 {
		tslog.Red("socks version error: %v,%d", err, ver)
		return
	}

	var authCount byte
	if authCount, err = bio.ReadByte(); err != nil || authCount == 0 {
		tslog.Red("socks auth count error: %v,%d", err, authCount)
		return
	}

	var authMethods []byte
	if authMethods, err = readn(bio.Reader, uint(authCount)); err != nil {
		tslog.Red("auth methods error: %v", err)
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
		tslog.Red("No acceptable auth method provided.")
		return
	}

	authReply := []byte{5, 0}
	if err := writen(bio.Writer, authReply); err != nil {
		tslog.Red("write auth reply error: %v", err)
		return
	}

	if ver, err := bio.ReadByte(); err != nil || ver != v5 {
		tslog.Red("socks version error: %v,%v", err, ver)
		return
	}

	var command byte
	if command, err = bio.ReadByte(); err != nil {
		tslog.Red("read command error: %v", err)
		return
	}

	if command != tcpStream {
		tslog.Red("command not supported: %d", command)
		return
	}

	if rsv, err := bio.ReadByte(); err != nil || rsv != 0 {
		tslog.Red("reserved byte error: %v", err)
		return
	}

	var addrType byte
	if addrType, err = bio.ReadByte(); err != nil {
		tslog.Red("read address type error: %v", err)
		return
	}

	strAddr := ""

	switch addrType {
	case addrTypeIPv4:
		var ipv4 []byte
		if ipv4, err = readn(bio.Reader, 4); err != nil {
			tslog.Red("read ipv4 error: %v", err)
			return
		}

		strAddr = net.IP(ipv4).String()

	case addrTypeDomain:
		var nameLen byte
		if nameLen, err = bio.ReadByte(); err != nil {
			tslog.Red("read domain len error: %v", err)
			return
		}

		var nameBytes []byte
		if nameBytes, err = readn(bio.Reader, uint(nameLen)); err != nil {
			tslog.Red("read domain error: %v", err)
			return
		}

		strAddr = string(nameBytes)
	default:
		tslog.Red("unknown address type: %d", addrType)
		return
	}

	var port uint16
	var portArray []byte

	if portArray, err = readn(bio.Reader, 2); err != nil {
		tslog.Red("read port error: %v", err)
		return
	}

	port = uint16(portArray[0])<<8 + uint16(portArray[1])

	hostport := fmt.Sprintf("%s:%d", strAddr, port)

	s.relay(hostport, s.conn)
}

func (s *SocksProxy) relay(host string, conn net.Conn) {
	sr := &SmartRelayer{}
	err := sr.Relay(host, conn, func(r Relayer) error {
		return r.ToLocal([]byte{5, 0, 0, 1, 0, 0, 0, 0, 0, 0})
	})

	if err != nil {
		conn.Write([]byte{5, 1, 0, 1, 0, 0, 0, 0, 0, 0})
		tslog.Red("relay error: %v", err)
	}
}
