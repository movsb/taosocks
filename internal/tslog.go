package internal

import (
	"fmt"
	"log"
)

type TSLog struct {
}

func (o *TSLog) log(c string, f string, v ...interface{}) {
	fmt.Printf("\033[%sm", c)
	log.Printf(f, v...)
	fmt.Print("\033[0m") // TODO 为什么不用输出换行？
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
