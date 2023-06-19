import { Upload, Spin ,Row,Typography} from 'antd';
import 'antd/dist/reset.css';
import './App.css';
import { Base64 } from './utils/base64';

import { InboxOutlined } from '@ant-design/icons';
import { useState } from 'react';
const { Dragger } = Upload;
const { Title } = Typography;
function download(filename: any, content: any) {
  var blob = new Blob([content], { type: 'application/arraybuffer' });
  var url = window.URL.createObjectURL(blob);
  var a = document.createElement('a');

  a.setAttribute("style", "display: none")
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();

  setTimeout(function () {
    document.body.removeChild(a);
    window.URL.revokeObjectURL(url);
  }, 5);
}

const App = function () {
  const [loading,setLoading] = useState(false)
  const props = {
    name: 'file',
    multiple: false,
    beforeUpload: function (file: any) {
      setLoading(true)
      var reader = new FileReader();
      reader.readAsBinaryString(file);    // 解析成base64格式
      reader.onload = function () {
        var output_encoded = window.caj2pdf(Base64.encode(this.result?.toString()))
        var output = Base64.decode(output_encoded)

        const pdf_length = window.Module.ccall('mupdf_clean_length', 'number', ['array', 'number'], [output, output.length]);
        const pdf_ptr = window.Module.ccall('mupdf_clean', 'number', ['array', 'number'], [output, output.length]);
        const arr = [];
        const memoryView = new Uint8Array(window.Module.asm.memory.buffer);
        for (var i = 0; i < pdf_length; i++) {
          arr.push(memoryView[i + pdf_ptr]);
        }
        const ret = new Uint8Array(arr)
        setLoading(false)
        download("output.pdf",ret)
      }
      return false
    }
  };
  return (<div>
  <Row align='middle' style={{minHeight: '30vh'}} justify="center"> <Title>在线caj转pdf工具</Title> </Row>
  <Row align='middle' style={{minHeight: '20vh'}} justify="center">
    <Dragger {...props} style={{background: 'linear-gradient(135deg,#56c8ff,#6f99fc)'}}>
      <p className="ant-upload-drag-icon">
        <InboxOutlined />
      </p>
      <p className="ant-upload-text">上传caj并生成pdf</p>
      <p className="ant-upload-hint">
        点击此处或拖动文件到此处
      </p>
    </Dragger></Row>
    <Row align='middle' justify="center">
    {loading && <Spin></Spin>}</Row>
  </div>)
};

export default App;
