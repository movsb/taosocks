package main

import (
	"bufio"
	"log"
	"net"
	"os"
	"regexp"
	"strings"
)

type ProxyType uint

const (
	_ ProxyType = iota
	proxyTypeDefault
	proxyTypeDirect
	proxyTypeProxy
	proxyTypeReject
)

type AddrType uint

const (
	_ AddrType = iota
	IPv4
	Domain
)

var reSplit = regexp.MustCompile(`[[:alnum:]-]+\.([[:alnum:]]+)$`)
var reIsComment = regexp.MustCompile(`^[ \t]*#`)

func isComment(line string) bool {
	return reIsComment.MatchString(line)
}

type HostFilter struct {
	tlds  map[string]ProxyType
	slds  map[string]ProxyType
	cidrs map[*net.IPNet]ProxyType
}

func (f *HostFilter) Init(path string) {
	f.tlds = make(map[string]ProxyType)
	f.slds = make(map[string]ProxyType)
	f.cidrs = make(map[*net.IPNet]ProxyType)

	file, err := os.Open(path)
	if err != nil {
		logf("rule file not found: %s\n", path)
		return
	}

	scanner := bufio.NewScanner(file)

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

			if toks[0] == "tld" {
				f.tlds[toks[1]] = ty
			} else if toks[0] == "sld" {
				f.slds[toks[1]] = ty
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

func (f *HostFilter) Add(host string, proxyType ProxyType) {
	host = strings.ToLower(host)
	matches := reSplit.FindStringSubmatch(host)
	f.slds[matches[0]] = proxyType
	log.Printf("添加规则：%s => %d\n", host, proxyType)
}

func (f *HostFilter) Test(host string, aty AddrType) ProxyType {
	host = strings.ToLower(host)

	// if is toplevel
	if !strings.Contains(host, ".") {
		return proxyTypeDirect
	}

	if aty == IPv4 {
		ip := net.ParseIP(host)
		for ipnet, ty := range f.cidrs {
			if ipnet.Contains(ip) {
				return ty
			}
		}
	} else if aty == Domain {
		matches := reSplit.FindStringSubmatch(host)
		sld := matches[0]
		tld := matches[1]

		if ty, ok := f.tlds[tld]; ok {
			return ty
		}

		if ty, ok := f.slds[sld]; ok {
			return ty
		}
	}

	return proxyTypeDefault
}
