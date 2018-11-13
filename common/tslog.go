package common

import (
	"fmt"
	"time"
)

// TSLog is a simple logger with colored outputs.
type TSLog struct {
}

func (z *TSLog) now() string {
	return time.Now().Format("2006-01-02 15:04:05")
}

func (z *TSLog) log(c string, f string, v ...interface{}) {
	out := fmt.Sprintf(f, v...)

	if c != "" {
		out = fmt.Sprintf("\033[%sm%s %s\033[0m", c, z.now(), out)
	} else {
		out = fmt.Sprintf("%s %s", z.now(), out)
	}

	fmt.Println(out)
}

// Log logs
func (z *TSLog) Log(f string, v ...interface{}) {
	z.log("0", f, v...)
}

// Green greens output.
func (z *TSLog) Green(f string, v ...interface{}) {
	z.log("0;32", f, v...)
}

// Red reds output.
func (z *TSLog) Red(f string, v ...interface{}) {
	z.log("0;31", f, v...)
}

// Gray grays output.
func (z *TSLog) Gray(f string, v ...interface{}) {
	z.log("1;30", f, v...)
}
