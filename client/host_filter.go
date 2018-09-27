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
		tslog.Red("rule file not found: %s\n", path)
	} else {
		f.scanFile(file, false)
		file.Close()
	}
}

func (f *HostFilter) scanFile(reader io.Reader, isTmp bool) {
	scanner := bufio.NewScanner(reader)

	for scanner.Scan() {
		rule := strings.Trim(scanner.Text(), " \t")
		if isComment(rule) || rule == "" {
			continue
		}
		toks := strings.Split(rule, ",")
		if len(toks) == 2 {
			var ty ProxyType
			switch toks[1] {
			case "direct":
				ty = proxyTypeDirect
			case "proxy":
				ty = proxyTypeProxy
			case "reject":
				ty = proxyTypeReject
			default:
				tslog.Red("invalid proxy type: %s\n", toks[1])
				continue
			}

			if strings.IndexByte(toks[0], '/') == -1 {
				f.hosts[toks[0]] = ty
			} else {
				_, ipnet, err := net.ParseCIDR(toks[0])
				if err == nil {
					f.cidrs[ipnet] = ty
				} else {
					tslog.Red("bad cidr: %s\n", toks[0])
				}
			}
		} else {
			tslog.Red("invalid rule: %s\n", rule)
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
	// Remove port if there is one.
	if colon := strings.IndexByte(host, ':'); colon != -1 {
		host = host[:colon]
	}

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
		for {
			// tslog.Red("test %s\n", host)
			if ty, ok := f.hosts[host]; ok {
				return ty
			}
			index := strings.IndexByte(host, '.')
			if index == -1 {
				break
			}
			host = host[index+1:]
		}
	}

	return proxyTypeDefault
}
