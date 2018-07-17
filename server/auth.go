package main

import (
    "strings"
    "os"
    "bufio"
)

type Auth struct {
    users   map[string]string
}

func (a *Auth) Init(path string) {
    a.users = make(map[string]string)

    fp, err := os.Open(path)
    if err != nil {
        tslog.Red("user file not found: %s\n", path)
        return
    }

    scn := bufio.NewScanner(fp)

    for scn.Scan() {
        line := scn.Text()
        toks := strings.Split(line, ":")

        if len(toks) != 2 {
            tslog.Red("bad user: %s\n", line)
            continue
        }

        user := toks[0]
        pass := toks[1]
        a.users[user] = pass
    }

    fp.Close()
}

func (a *Auth) Test(user, pass string) bool {
    if p, ok := a.users[user]; ok {
        if p == pass {
            return true
        }
    }

    return false
}

