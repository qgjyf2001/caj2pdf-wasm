package main

import (
	"bytes"
	"encoding/base64"
	"syscall/js"

	"github.com/rwv/caj2pdf-go/parser/CAJParser"
)

func caj2pdf_(buffer []byte) ([]byte, string) {
	parser := CAJParser.New(bytes.NewReader(buffer))
	ret, outline := parser.Convert(bytes.NewReader(buffer))
	return ret, outline
}
func caj2pdf(this js.Value, inputs []js.Value) interface{} {
	input_base64 := inputs[0].String()
	input, _ := base64.StdEncoding.DecodeString(input_base64)
	output, outline := caj2pdf_(input)
	return map[string]interface{}{
		"file":    base64.StdEncoding.EncodeToString(output),
		"outline": outline,
	}
}

func main() {
	c := make(chan struct{}, 0)

	// 注册add函数
	js.Global().Set("caj2pdf", js.FuncOf(caj2pdf))

	<-c
}
