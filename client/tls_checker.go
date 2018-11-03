package main

import (
	"container/list"
	"crypto/tls"
	"net"
	"sync"
	"time"
)

type _TlsCheckContext struct {
	wg *sync.WaitGroup
	ok bool
}

// TLSChecker is a synchronous TLS connectivity checker.
type TLSChecker struct {
	lock sync.Mutex
	maps map[string]*list.List
}

// NewTLSChecker news a TLS checker.
func NewTLSChecker() *TLSChecker {
	tc := &TLSChecker{}
	tc.maps = make(map[string]*list.List)
	return tc
}

// Check returns true if a TLS connection can be correctly made.
func (t *TLSChecker) Check(hostport string) bool {
	t.lock.Lock()
	var lst *list.List
	if l, ok := t.maps[hostport]; ok {
		lst = l
	} else {
		lst = list.New()
		t.maps[hostport] = lst
		go t.check(hostport)
	}
	wg := &sync.WaitGroup{}
	wg.Add(1)
	ctx := &_TlsCheckContext{wg: wg}
	lst.PushBack(ctx)
	t.lock.Unlock()
	wg.Wait()
	return ctx.ok
}

func (t *TLSChecker) check(hostport string) (ok bool) {
	defer func() {
		t.finish(hostport, ok)
	}()
	conn, err := net.DialTimeout("tcp4", hostport, time.Second*10)
	if err != nil {
		tslog.Red("? net.DialTimeout error: %s: %s", hostport, err)
		return false
	}
	host, _, _ := net.SplitHostPort(hostport)
	tlsClient := tls.Client(conn, &tls.Config{ServerName: host})
	err = tlsClient.Handshake()
	tlsClient.Close()
	if err != nil {
		tslog.Red("? tls handshake error: %s: %s", hostport, err)
		return false
	}
	return true
}

func (t *TLSChecker) finish(hostport string, ok bool) {
	t.lock.Lock()
	defer t.lock.Unlock()
	lst := t.maps[hostport]
	for lst.Len() > 0 {
		elem := lst.Front()
		ctx := elem.Value.(*_TlsCheckContext)
		ctx.ok = ok
		ctx.wg.Done()
		lst.Remove(elem)
	}
	delete(t.maps, hostport)
}
