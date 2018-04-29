package main

import (
	"bufio"
	"crypto/tls"
	"encoding/gob"
	"errors"
	"io"
	"log"
	"net"
	"net/http"
	"sync"

	"../internal"
)

// Relayer exposes the interfaces for
// relaying a connection
type Relayer interface {
	Begin(addr string, src net.Conn) bool
	Relay()
	End()
	ToRemote(b []byte) error
	ToLocal(b []byte) error
}

// LocalRelayer is a relayer that relays all
// traffics through local network
type LocalRelayer struct {
	src  net.Conn
	dst  net.Conn
	addr string
}

func (r *LocalRelayer) Begin(addr string, src net.Conn) bool {
	r.src = src
	r.addr = addr

	dst, err := net.Dial("tcp", addr)
	if err != nil {
		log.Printf("Dial host:%s -  %s\n", addr, err)
		return false
	}

	r.dst = dst
	return true
}

func (r *LocalRelayer) ToLocal(b []byte) error {
	r.src.Write(b)
	return nil
}

func (r *LocalRelayer) ToRemote(b []byte) error {
	r.dst.Write(b)
	return nil
}

func (r *LocalRelayer) End() {
	if r.src != nil {
		r.src.Close()
	}
	if r.dst != nil {
		r.dst.Close()
	}
}

func (r *LocalRelayer) Relay() {
	log.Printf("> [Direct] %s\n", r.addr)

	wg := &sync.WaitGroup{}
	wg.Add(2)

	var tx int64
	var rx int64

	go func() {
		tx, _ = io.Copy(r.dst, r.src)
		wg.Done()
		r.End()
	}()

	go func() {
		rx, _ = io.Copy(r.src, r.dst)
		wg.Done()
		r.End()
	}()

	wg.Wait()

	log.Printf("< [Direct] %s [TX:%d, RX:%d]\n", r.addr, tx, rx)

}

const kVersion string = "taosocks/20171218"

// RemoteRelayer is a relayer that relays all
// traffics through remote servers by using
// taosocks protocol
type RemoteRelayer struct {
	Server   string
	Insecure bool

	src  net.Conn
	dst  net.Conn
	addr string
}

func (r *RemoteRelayer) dialServer() (net.Conn, error) {
	tlscfg := &tls.Config{
		InsecureSkipVerify: r.Insecure,
	}

	conn, err := tls.Dial("tcp4", r.Server, tlscfg)
	if err != nil {
		return nil, err
	}

	// the upgrade request
	req, _ := http.NewRequest("GET", "/", nil)
	req.Host = r.Server
	req.Header.Add("Connection", "upgrade")
	req.Header.Add("Upgrade", kVersion)
	req.Header.Add("Username", config.Username)
	req.Header.Add("Password", config.Password)

	err = req.Write(conn)
	if err != nil {
		conn.Close()
		return nil, err
	}

	bio := bufio.NewReader(conn)

	resp, err := http.ReadResponse(bio, nil)
	if err != nil {
		conn.Close()
		return nil, err
	}

	resp.Body.Close()

	if resp.StatusCode != 101 {
		conn.Close()
		return nil, errors.New("server upgrade protocol error")
	}

	return conn, nil
}

func (r *RemoteRelayer) Begin(addr string, src net.Conn) bool {
	r.src = src
	r.addr = addr

	dst, err := r.dialServer()
	if err != nil {
		return false
	}

	r.dst = dst

	enc := gob.NewEncoder(r.dst)
	dec := gob.NewDecoder(r.dst)

	err = enc.Encode(internal.OpenPacket{Addr: r.addr})
	if err != nil {
		return false
	}

	var oapkt internal.OpenAckPacket
	err = dec.Decode(&oapkt)
	if err != nil {
		return false
	}

	if !oapkt.Status {
		return false
	}

	return true
}

func (r *RemoteRelayer) ToLocal(b []byte) error {
	r.src.Write(b)

	return nil
}

func (r *RemoteRelayer) ToRemote(b []byte) error {
	var pkt internal.RelayPacket
	pkt.Data = b

	enc := gob.NewEncoder(r.dst)
	enc.Encode(pkt)

	return nil
}

func (r *RemoteRelayer) End() {
	if r.src != nil {
		r.src.Close()
	}
	if r.dst != nil {
		r.dst.Close()
	}
}

func (r *RemoteRelayer) Relay() {
	log.Printf("> [Proxy ] %s\n", r.addr)

	wg := &sync.WaitGroup{}
	wg.Add(2)

	var tx int64
	var rx int64

	go func() {
		tx, _ = r.src2dst()
		wg.Done()
		r.End()
	}()

	go func() {
		rx, _ = r.dst2src()
		wg.Done()
		r.End()
	}()

	wg.Wait()

	log.Printf("< [Proxy ] %s [TX:%d, RX:%d]\n", r.addr, tx, rx)
}

func (r *RemoteRelayer) src2dst() (int64, error) {
	enc := gob.NewEncoder(r.dst)

	buf := make([]byte, 4096)

	var all int64
	var err error

	for {
		n, err := r.src.Read(buf)
		if err != nil {
			break
		}

		var pkt internal.RelayPacket
		pkt.Data = buf[:n]

		err = enc.Encode(pkt)
		if err != nil {
			// log.Printf("server reset: %s\n", err)
			break
		}

		all += int64(n)
	}

	return all, err
}

func (r *RemoteRelayer) dst2src() (int64, error) {
	dec := gob.NewDecoder(r.dst)

	var all int64
	var err error

	for {
		var pkt internal.RelayPacket
		err = dec.Decode(&pkt)
		if err != nil {
			// log.Printf("server reset: %s\n", err)
			break
		}

		_, err = r.src.Write(pkt.Data)
		if err != nil {
			break
		}

		all += int64(len(pkt.Data))
	}

	return all, err
}
