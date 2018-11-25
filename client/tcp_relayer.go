package main

import (
	"bufio"
	"crypto/tls"
	"encoding/gob"
	"errors"
	"io"
	"net"
	"net/http"
	"sync"
	"time"

	"github.com/movsb/taosocks/common"
)

// Relayer exposes the interfaces for
// relaying a connection
type Relayer interface {
	Begin(addr string, src net.Conn) error
	Relay() *RelayResult
	ToRemote(b []byte) error
	ToLocal(b []byte) error
}

// RelayResult contains
type RelayResult struct {
	errTx error
	errRx error
	nTx   int64
	nRx   int64
}

// LocalRelayer is a relayer that relays all
// traffics through local network.
type LocalRelayer struct {
	src  net.Conn
	dst  net.Conn
	addr string
}

// Begin implements Relayer.Begin.
func (r *LocalRelayer) Begin(addr string, src net.Conn) error {
	r.src = src
	r.addr = addr

	dst, err := net.DialTimeout("tcp", addr, 10*time.Second)
	if err != nil {
		tslog.Red("? Dial host:%s -  %s", addr, err)
		return err
	}

	r.dst = dst
	return nil
}

// ToLocal implements Relayer.ToLocal.
func (r *LocalRelayer) ToLocal(b []byte) error {
	r.src.Write(b)
	return nil
}

// ToRemote implements Relayer.ToRemote.
func (r *LocalRelayer) ToRemote(b []byte) error {
	r.dst.Write(b)
	return nil
}

// Relay implements Relayer.Relay.
func (r *LocalRelayer) Relay() *RelayResult {
	tslog.Log("> [Direct] %s", r.addr)

	wg := &sync.WaitGroup{}
	wg.Add(2)

	var tx int64
	var rx int64

	var errTx, errRx error

	go func() {
		tx, errTx = io.Copy(r.dst, r.src)
		wg.Done()
		r.src.Close()
		r.dst.Close()
	}()

	go func() {
		rx, errRx = io.Copy(r.src, r.dst)
		wg.Done()
		r.src.Close()
		r.dst.Close()
	}()

	wg.Wait()

	r.src.Close()
	r.dst.Close()

	tslog.Gray("< [Direct] %s [TX:%d, RX:%d]", r.addr, tx, rx)

	return &RelayResult{
		errTx: errTx,
		errRx: errRx,
		nTx:   tx,
		nRx:   rx,
	}
}

const gVersion string = "taosocks/20180728"

var (
	// ErrCannotDialRemoteServer is
	ErrCannotDialRemoteServer = errors.New("cannot dial remote server")

	// ErrRemoteServerCannotConnectHost is
	ErrRemoteServerCannotConnectHost = errors.New("remote proxy server cannot connect to the specified host")
)

// RemoteRelayer is a relayer that relays all
// traffics through remote servers by using
// taosocks protocol
type RemoteRelayer struct {
	src  net.Conn
	dst  net.Conn
	addr string
}

func (r *RemoteRelayer) dialServer() (net.Conn, error) {
	tlscfg := &tls.Config{
		InsecureSkipVerify: config.Insecure,
	}

	conn, err := tls.Dial("tcp4", config.Server, tlscfg)
	if err != nil {
		return nil, err
	}

	// the upgrade request
	req, _ := http.NewRequest("GET", "/", nil)
	req.Host = config.Server
	req.Header.Add("Connection", "upgrade")
	req.Header.Add("Upgrade", gVersion)
	req.Header.Add("Authorization", "taosocks "+config.Key)

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

// Begin implements Relayer.Begin.
func (r *RemoteRelayer) Begin(addr string, src net.Conn) error {
	r.src = src
	r.addr = addr

	dst, err := r.dialServer()
	if err != nil {
		tslog.Red("%s", err)
		// return err
		return ErrCannotDialRemoteServer
	}

	r.dst = dst

	enc := gob.NewEncoder(r.dst)
	dec := gob.NewDecoder(r.dst)

	err = enc.Encode(common.OpenPacket{Addr: r.addr})
	if err != nil {
		return err
	}

	var oapkt common.OpenAckPacket
	err = dec.Decode(&oapkt)
	if err != nil {
		return err
	}

	if !oapkt.Status {
		return ErrRemoteServerCannotConnectHost
	}

	return nil
}

// ToLocal implements Relayer.ToLocal.
func (r *RemoteRelayer) ToLocal(b []byte) error {
	r.src.Write(b)

	return nil
}

// ToRemote implements Relayer.ToRemote.
func (r *RemoteRelayer) ToRemote(b []byte) error {
	var pkt common.RelayPacket
	pkt.Data = b

	enc := gob.NewEncoder(r.dst)
	enc.Encode(pkt)

	return nil
}

// Relay implements Relayer.Relay.
func (r *RemoteRelayer) Relay() *RelayResult {
	tslog.Log("> [Proxy ] %s", r.addr)

	wg := &sync.WaitGroup{}
	wg.Add(2)

	var tx int64
	var rx int64

	var errTx, errRx error

	go func() {
		tx, errTx = r.src2dst()
		wg.Done()
		if errTx != nil {
			r.src.Close()
			r.dst.Close()
		}
	}()

	go func() {
		rx, errRx = r.dst2src()
		wg.Done()
		if errRx != nil {
			r.src.Close()
			r.dst.Close()
		}
	}()

	wg.Wait()

	r.src.Close()
	r.dst.Close()

	tslog.Gray("< [Proxy ] %s [TX:%d, RX:%d]", r.addr, tx, rx)

	return &RelayResult{
		errTx: errTx,
		errRx: errRx,
		nTx:   tx,
		nRx:   rx,
	}
}

func (r *RemoteRelayer) src2dst() (int64, error) {
	enc := gob.NewEncoder(r.dst)

	buf := make([]byte, common.ReadBufSize)

	var all int64
	var err error
	var n int

	for {
		n, err = r.src.Read(buf)
		if err != nil {
			break
		}

		var pkt common.RelayPacket
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
		var pkt common.RelayPacket
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

// SmartRelayer is
type SmartRelayer struct {
}

// Relay relays
func (o *SmartRelayer) Relay(host string, conn net.Conn, beforeRelay func(r Relayer) error) error {
	hostname, portstr, _ := net.SplitHostPort(host)
	proxyType := filter.Test(hostname, portstr)

	var r Relayer

	switch proxyType {
	case proxyTypeDirect, proxyTypeAutoDirect:
		r = &LocalRelayer{}
	case proxyTypeProxy, proxyTypeAutoProxy:
		r = &RemoteRelayer{}
	case proxyTypeReject:
		return errors.New("host is rejected")
	}

	var beginErr error
	useRemote := false

	if err := r.Begin(host, conn); err != nil {
		beginErr = err
		switch r.(type) {
		case *LocalRelayer:
			if proxyType != proxyTypeDirect {
				r = &RemoteRelayer{}
				if err := r.Begin(host, conn); err == nil {
					beginErr = nil
					useRemote = true
				}
			}
		}
	}

	if beginErr != nil {
		conn.Close()
		// If the host is not connected to a network, remote relayer
		// may report an error not correspond to the host itself.
		// At this time, we should not remote the rules we already have.
		if beginErr != ErrCannotDialRemoteServer {
			switch proxyType {
			case proxyTypeAutoDirect, proxyTypeAutoProxy:
				filter.DeleteHost(hostname)
			}
		}
		return errors.New("no relayer can relay such host")
	}

	if useRemote {
		filter.AddHost(hostname, proxyTypeAutoProxy)
	}

	if beforeRelay != nil {
		if beforeRelay(r) != nil {
			conn.Close()
			return errors.New("beforeRelay returns an error")
		}
	}

	rr := r.Relay()
	_ = rr

	return nil
}
