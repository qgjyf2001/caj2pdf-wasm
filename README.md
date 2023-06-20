# caj2pdf-wasm
caj2pdf wasm版，[网址](https://qgjyf2001.github.io/caj2pdf)
## 编译方式
``` bash
cd mupdf
bash build.sh
mv build/wasm/release/mutool.wasm  ../caj2pdf-react/public/mutool.wasm
mv build/wasm/release/mutool  ../caj2pdf-react/public/mutool.js
cd ../caj2pdf-react && yarn && yarn build
```