package CAJParser

import (
	"bytes"
	"io"
)

const caj_TOC_NUMBER_OFFSET = 0x110

type CAJParser struct {
	pageNum int32
}

func New(file io.ReadSeeker) CAJParser {
	parser := CAJParser{
		pageNum: getPageNum(file),
	}
	return parser
}

func (parser CAJParser) Convert(file io.ReadSeeker) ([]byte, string) {
	toc := getToc(file)

	writer := new(bytes.Buffer)
	extractData(file, writer)

	extractedReader := bytes.NewReader(writer.Bytes())
	writer = new(bytes.Buffer)
	handlePages(extractedReader, writer, &parser)

	return writer.Bytes(), generateOutlineString(toc)
}
