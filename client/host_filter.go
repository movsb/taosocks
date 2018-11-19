package main

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"os"
	"regexp"
	"strings"
	"sync"
)

// ProxyType is
type ProxyType byte

const (
	proxyTypeNone       ProxyType = iota
	proxyTypeDirect               // direct, from rules.txt
	proxyTypeProxy                // proxy, from rules.txt
	proxyTypeReject               // reject, from rules.txt
	proxyTypeAutoDirect           // direct, from checker, auto-generated
	proxyTypeAutoProxy            // proxy, from checker, auto-generated
)

func (t ProxyType) String() string {
	switch t {
	case proxyTypeDirect:
		return "direct"
	case proxyTypeProxy:
		return "proxy"
	case proxyTypeReject:
		return "reject"
	case proxyTypeAutoDirect:
		return "auto-direct"
	case proxyTypeAutoProxy:
		return "auto-proxy"
	default:
		return "unknown"
	}
}

// ProxyTypeFromString is
func ProxyTypeFromString(name string) ProxyType {
	switch name {
	case "direct":
		return proxyTypeDirect
	case "proxy":
		return proxyTypeProxy
	case "reject":
		return proxyTypeReject
	case "auto-direct":
		return proxyTypeAutoDirect
	case "auto-proxy":
		return proxyTypeAutoProxy
	default:
		return 0
	}
}

// AddrType is
type AddrType uint

// Address Types
const (
	_ AddrType = iota
	IPv4
	Domain
)

var reIsComment = regexp.MustCompile(`^[ \t]*#`)

func isComment(line string) bool {
	return reIsComment.MatchString(line)
}

// HostFilter returns the proxy type on specified host.
type HostFilter struct {
	mu    sync.RWMutex
	hosts map[string]ProxyType
	cidrs map[*net.IPNet]ProxyType
}

// SaveAuto saves auto-generated rules.
func (f *HostFilter) SaveAuto(path string) {
	f.mu.Lock()
	defer f.mu.Unlock()

	file, err := os.Create(path)
	if err != nil {
		return
	}
	defer file.Close()

	w := bufio.NewWriter(file)

	for k, t := range f.hosts {
		switch t {
		case proxyTypeAutoDirect, proxyTypeAutoProxy:
			fmt.Fprintf(w, "%s,%s\n", k, t)
		}
	}

	w.Flush()
	file.Close()
}

// LoadAuto loads auto-generated rules.
func (f *HostFilter) LoadAuto(path string) {
	file, err := os.Open(path)
	if err != nil {
		return
	}

	defer file.Close()

	f.scanFile(file)
}

// Init loads user-defined rules.
func (f *HostFilter) Init(path string) {
	f.hosts = make(map[string]ProxyType)
	f.cidrs = make(map[*net.IPNet]ProxyType)

	if file, err := os.Open(path); err != nil {
		tslog.Red("rule file not found: %s\n", path)
	} else {
		f.scanFile(file)
		file.Close()
	}
}

func (f *HostFilter) scanFile(reader io.Reader) {
	scanner := bufio.NewScanner(reader)

	for scanner.Scan() {
		rule := strings.Trim(scanner.Text(), " \t")
		if isComment(rule) || rule == "" {
			continue
		}
		toks := strings.Split(rule, ",")
		if len(toks) == 2 {
			ptype := ProxyTypeFromString(toks[1])
			if ptype == 0 {
				tslog.Red("invalid proxy type: %s\n", toks[1])
				continue
			}

			if strings.IndexByte(toks[0], '/') == -1 {
				f.hosts[toks[0]] = ptype
			} else {
				_, ipnet, err := net.ParseCIDR(toks[0])
				if err == nil {
					f.cidrs[ipnet] = ptype
				} else {
					tslog.Red("bad cidr: %s\n", toks[0])
				}
			}
		} else {
			tslog.Red("invalid rule: %s\n", rule)
		}
	}
}

// AddHost adds a rule. (thread-safe)
func (f *HostFilter) AddHost(host string, ptype ProxyType) {
	f.mu.Lock()
	defer f.mu.Unlock()
	ty, ok := f.hosts[host]
	f.hosts[host] = ptype
	if !ok {
		tslog.Green("+ 添加规则：[%s] %s", ptype, host)
	} else {
		if ty != ptype {
			tslog.Green("* 改变规则：[%s -> %s] %s", ty, ptype, host)
		}
	}
}

// DeleteHost deletes a rule. (thread-safe)
func (f *HostFilter) DeleteHost(host string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	delete(f.hosts, host)
	tslog.Red("- 删除规则：%s", host)
}

// Test returns proxy type for host host.
func (f *HostFilter) Test(host string, port string) (proxyType ProxyType) {
	defer func() {
		if proxyType == proxyTypeNone {
			pty := proxyTypeAutoDirect
			if !tcpChecker.Check(host, port) {
				pty = proxyTypeAutoProxy
			}
			f.AddHost(host, pty)
			proxyType = pty
		}
	}()

	f.mu.RLock()
	defer f.mu.RUnlock()

	host = strings.ToLower(host)

	// if host is TopLevel, like localhost.
	if !strings.Contains(host, ".") {
		return proxyTypeDirect
	}

	aty := Domain
	if net.ParseIP(host).To4() != nil {
		aty = IPv4
	}

	if aty == IPv4 {
		if ty, ok := f.hosts[host]; ok {
			return ty
		}
		ip := net.ParseIP(host)
		for ipnet, ty := range f.cidrs {
			if ipnet.Contains(ip) {
				return ty
			}
		}
	} else if aty == Domain {
		// test suffixes (sub strings)
		// eg. host is play.golang.org, then these suffixes will be tested:
		//		play.golang.org
		//		golang.org
		//		org
		part := host
		for {
			if ty, ok := f.hosts[part]; ok {
				return ty
			}
			index := strings.IndexByte(part, '.')
			if index == -1 {
				break
			}
			part = part[index+1:]
		}
	}

	return proxyTypeNone
}
