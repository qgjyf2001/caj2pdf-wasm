package main

import (
	"bytes"
	"encoding/base64"
	"syscall/js"

	"github.com/rwv/caj2pdf-go/parser/CAJParser"
)

func caj2pdf_(buffer []byte) []byte {
	parser := CAJParser.New(bytes.NewReader(buffer))
	ret := parser.Convert(bytes.NewReader(buffer))
	return ret
}
func caj2pdf(this js.Value, inputs []js.Value) interface{} {
	input_base64 := inputs[0].String()
	input, _ := base64.StdEncoding.DecodeString(input_base64)
	output := caj2pdf_(input)
	return base64.StdEncoding.EncodeToString(output)
}

func main() {
	c := make(chan struct{}, 0)

	// 注册add函数
	js.Global().Set("caj2pdf", js.FuncOf(caj2pdf))

	<-c
}
