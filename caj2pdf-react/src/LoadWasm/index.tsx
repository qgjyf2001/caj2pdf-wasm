import './wasm_exec.js';
import './wasmTypes.d.ts';
import './LoadWasm.css';

import React, { useEffect } from 'react';
import { Spin } from 'antd';

async function loadWasm(): Promise<void> {
  const goWasm = new window.Go();
  const result = await WebAssembly.instantiateStreaming(fetch('caj2pdf.wasm'), goWasm.importObject);
  goWasm.run(result.instance);
}

export const LoadWasm: React.FC<React.PropsWithChildren<{}>> = (props) => {
  const [isLoading, setIsLoading] = React.useState(true);
  const [isEmscriptenLoading,setEmscriptenLoading] = React.useState(true)

  useEffect(() => {

    window.Module.onRuntimeInitialized = function() {
      setEmscriptenLoading(false)
    }
    loadWasm().then(() => {
      setIsLoading(false);
    });
  }, []);

  if (isLoading || isEmscriptenLoading) {
    return (<>
      <Spin className='LoadWasm' tip="Loading Wasm...">
       <div className="content" />
      </Spin></>
    );
  } else {
    return <React.Fragment>{props.children}</React.Fragment>;
  }
};
