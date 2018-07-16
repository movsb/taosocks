package main

import (
	"bufio"
	"io"
	"net"
	"os"
	"regexp"
	"strings"
)

// ProxyType is
type ProxyType byte

const (
	_ ProxyType = iota
	proxyTypeDefault
	proxyTypeDirect
	proxyTypeProxy
	proxyTypeReject
	proxyTypeAuto
)

// AddrType is
type AddrType uint

// Address Types
const (
	_ AddrType = iota
	IPv4
	Domain
)

var reIsComment = regexp.MustCompile(`^[ \t]*#`)
var reSplitHost = regexp.MustCompile(`\.([^.]+\.[^.]+)$`)

func isComment(line string) bool {
	return reIsComment.MatchString(line)
}

// HostFilter returns the proxy type on specified host.
type HostFilter struct {
	ch    chan string
	hosts map[string]ProxyType
	cidrs map[*net.IPNet]ProxyType
}

// SaveAuto saves auto-generated rules.
func (f *HostFilter) SaveAuto(path string) {
	file, err := os.Create(path)
	if err != nil {
		return
	}
	defer file.Close()

	w := bufio.NewWriter(file)

	for k, t := range f.hosts {
		if t == proxyTypeAuto {
			w.WriteString(k)
			w.WriteByte('\n')
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

	scn := bufio.NewScanner(file)

	n := 0

	for scn.Scan() {
		host := scn.Text()
		f.hosts[host] = proxyTypeAuto
		n++
	}

	tslog.Green("  加载了 %d 条自动规则", n)
}

// Init loads user-difined rules.
func (f *HostFilter) Init(path string) {
	f.ch = make(chan string)
	f.hosts = make(map[string]ProxyType)
	f.cidrs = make(map[*net.IPNet]ProxyType)

	go f.opRoutine()

	if file, err := os.Open(path); err != nil {
		logf("rule file not found: %s\n", path)
	} else {
		f.scanFile(file, false)
		file.Close()
	}
}

func (f *HostFilter) scanFile(reader io.Reader, isTmp bool) {
	scanner := bufio.NewScanner(reader)

	for scanner.Scan() {
		rule := scanner.Text()
		if isComment(rule) {
			continue
		}
		toks := strings.Split(rule, ",")
		if len(toks) == 3 {
			var ty ProxyType
			switch toks[2] {
			case "direct":
				ty = proxyTypeDirect
			case "proxy":
				ty = proxyTypeProxy
			case "reject":
				ty = proxyTypeReject
			default:
				continue
			}

			if toks[0] == "host" {
				f.hosts[toks[1]] = ty
			} else if toks[0] == "cidr" {
				_, ipnet, err := net.ParseCIDR(toks[1])
				if err == nil {
					f.cidrs[ipnet] = ty
				} else {
					logf("bad cidr: %s\n", toks[1])
				}
			}
		}
	}
}

// Add adds a rule. (thread-safe)
func (f *HostFilter) Add(host string) {
	f.ch <- "+" + host
}

// Del deletes a rule. (thread-safe)
func (f *HostFilter) Del(host string) {
	f.ch <- "-" + host
}

func (f *HostFilter) opRoutine() {
	for s := ""; ; {
		s = <-f.ch
		if s == "" {
			break
		}

		op := s[0]
		host := s[1:]

		switch op {
		case '+':
			f.hosts[host] = proxyTypeAuto
			tslog.Green("+ 自动添加代理规则：%s", host)
		case '-':
			delete(f.hosts, host)
			tslog.Red("- 自动删除代理规则：%s", host)
		}
	}
}

// Test returns proxy type for host host.
func (f *HostFilter) Test(host string) ProxyType {
	if colon := strings.IndexByte(host, ':'); colon != -1 {
		host = host[:colon]
	}

	host = strings.ToLower(host)

	// if is TopLevel
	if !strings.Contains(host, ".") {
		return proxyTypeDirect
	}

	aty := Domain
	if net.ParseIP(host).To4() != nil {
		aty = IPv4
	}

	if aty == IPv4 {
		ip := net.ParseIP(host)
		for ipnet, ty := range f.cidrs {
			if ipnet.Contains(ip) {
				return ty
			}
		}
	} else if aty == Domain {
		if ty, ok := f.hosts[host]; ok {
			return ty
		}

		matches := reSplitHost.FindStringSubmatch(host)
		if len(matches) == 2 {
			if ty, ok := f.hosts[matches[1]]; ok {
				return ty
			}
		}
	}

	return proxyTypeDefault
}
