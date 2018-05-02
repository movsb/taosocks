package internal

import (
	"fmt"
	"log"
)

type TSLog struct {
}

func (o *TSLog) log(c string, f string, v ...interface{}) {
	s := ""

	if c != "" {
		s += fmt.Sprintf("\033[%sm", c)
	}

	s += fmt.Sprintf(f, v...)

	if c != "" {
		s += fmt.Sprint("\033[0m")
	}

	log.Print(s)
}

func (o *TSLog) Log(f string, v ...interface{}) {
	o.log("0", f, v...)
}

func (o *TSLog) Green(f string, v ...interface{}) {
	o.log("0;32", f, v...)
}

func (o *TSLog) Red(f string, v ...interface{}) {
	o.log("0;31", f, v...)
}

func (o *TSLog) Gray(f string, v ...interface{}) {
	o.log("1;30", f, v...)
}
