package CAJParser

import (
	"fmt"
)

func generateOutlineString(toc []toc) string {
	outline := ""
	for _, item := range toc {
		outline += fmt.Sprintf("%d %d %s\n", item.Level, item.Page, item.Title)
	}
	return outline
}
