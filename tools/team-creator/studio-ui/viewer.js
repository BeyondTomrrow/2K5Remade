'use strict';
class UniformViewer {
  constructor(canvas) {
    this.canvas=canvas;this.gl=canvas.getContext('webgl2',{alpha:true,antialias:true});
    if(!this.gl)throw Error('This browser needs WebGL 2 for the interactive player preview.');
    this.yaw=.35;this.pitch=.08;this.distance=3.3;this.target=0;this.spin=false;this.meshes=[];this.textures={};
    const gl=this.gl;
    const shader=(type,source)=>{const s=gl.createShader(type);gl.shaderSource(s,source);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s;};
    this.program=gl.createProgram();
    gl.attachShader(this.program,shader(gl.VERTEX_SHADER,`#version 300 es
      in vec3 aPosition;in vec2 aUV;in vec3 aNormal;uniform mat4 uMVP;out vec2 vUV;out vec3 vNormal;
      void main(){gl_Position=uMVP*vec4(aPosition,1.);vUV=aUV;vNormal=aNormal;}`));
    gl.attachShader(this.program,shader(gl.FRAGMENT_SHADER,`#version 300 es
      precision highp float;in vec2 vUV;in vec3 vNormal;uniform sampler2D uTexture;out vec4 outColor;
      void main(){vec4 c=texture(uTexture,vUV);if(c.a<.12)discard;vec3 n=normalize(vNormal);float light=.65+.35*abs(dot(n,normalize(vec3(-.4,.8,1.))));outColor=vec4(c.rgb*light,1.);}`));
    gl.linkProgram(this.program);if(!gl.getProgramParameter(this.program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(this.program));
    this.locations={};for(const key of ['aPosition','aUV','aNormal'])this.locations[key]=gl.getAttribLocation(this.program,key);
    this.mvp=gl.getUniformLocation(this.program,'uMVP');
    this.solid('skin',[214,170,140,255]);this.solid('facemask',[180,180,180,255]);this.solid('mouth',[35,30,30,255]);
    this.solid('splayer',[210,210,210,255]);this.solid('helmet00',[220,220,220,255]);this.solid('number',[245,245,245,255]);
    let dragging=false,lastX=0,lastY=0;
    canvas.addEventListener('pointerdown',e=>{dragging=true;lastX=e.clientX;lastY=e.clientY;canvas.setPointerCapture(e.pointerId);});
    canvas.addEventListener('pointermove',e=>{if(!dragging)return;this.yaw-=(e.clientX-lastX)*.012;this.pitch=Math.max(-.65,Math.min(.85,this.pitch+(e.clientY-lastY)*.008));lastX=e.clientX;lastY=e.clientY;});
    canvas.addEventListener('pointerup',()=>dragging=false);
    canvas.addEventListener('wheel',e=>{e.preventDefault();this.distance=Math.max(.6,Math.min(8,this.distance+e.deltaY*.003));},{passive:false});
    this.resizeObserver=new ResizeObserver(()=>this.resize());this.resizeObserver.observe(canvas);this.resize();this.frame();
  }
  solid(key,color){const gl=this.gl;let t=this.textures[key];if(!t)t=this.textures[key]=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,t);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,1,1,0,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array(color));this.textureSettings();}
  textureSettings(){const gl=this.gl;gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);}
  async setTextures(values){await Promise.all(Object.entries(values).map(async([key,value])=>{if(key==='facemask_color'){this.solid('facemask',[...value,255]);return;}const img=new Image();img.src=value;await img.decode();const gl=this.gl;if(!this.textures[key])this.textures[key]=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,this.textures[key]);gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,false);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,img);this.textureSettings();}));}
  setModel(data){const gl=this.gl;let triangles=0;this.meshes=data.meshes.map(s=>{const p=new Float32Array(s.positions),normals=new Float32Array(p.length),idx=new Uint32Array(s.indices);
    for(let i=0;i<idx.length;i+=3){const a=idx[i]*3,b=idx[i+1]*3,c=idx[i+2]*3,ab=[p[b]-p[a],p[b+1]-p[a+1],p[b+2]-p[a+2]],ac=[p[c]-p[a],p[c+1]-p[a+1],p[c+2]-p[a+2]],n=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]];for(const v of [a,b,c])for(let k=0;k<3;k++)normals[v+k]+=n[k];}
    for(let i=0;i<normals.length;i+=3){const len=Math.hypot(normals[i],normals[i+1],normals[i+2])||1;for(let k=0;k<3;k++)normals[i+k]/=len;}
    const vao=gl.createVertexArray();gl.bindVertexArray(vao);
    for(const [name,array,size] of [['aPosition',p,3],['aUV',new Float32Array(s.uv),2],['aNormal',normals,3]]){const b=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,b);gl.bufferData(gl.ARRAY_BUFFER,array,gl.STATIC_DRAW);gl.enableVertexAttribArray(this.locations[name]);gl.vertexAttribPointer(this.locations[name],size,gl.FLOAT,false,0,0);}
    const ib=gl.createBuffer();gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,ib);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,idx,gl.STATIC_DRAW);triangles+=idx.length/3;return{vao,count:idx.length,texture:s.texture,name:s.name};});return{meshes:this.meshes.length,triangles};}
  resize(){const ratio=Math.min(devicePixelRatio||1,2);this.canvas.width=Math.max(1,Math.round(this.canvas.clientWidth*ratio));this.canvas.height=Math.max(1,Math.round(this.canvas.clientHeight*ratio));}
  view(name){this.target=name==='helmet'?.75:0;this.distance=name==='helmet'?1.05:3.3;this.pitch=.08;this.yaw={front:0,back:Math.PI,left:-Math.PI/2,helmet:.35}[name]||0;}
  matrix(){const f=1/Math.tan(Math.PI/8),aspect=this.canvas.width/this.canvas.height,n=.05,z=30;
    const proj=[f/aspect,0,0,0,0,f,0,0,0,0,(z+n)/(n-z),-1,0,0,2*z*n/(n-z),0];
    const eye=[Math.sin(this.yaw)*Math.cos(this.pitch)*this.distance,this.target+Math.sin(this.pitch)*this.distance,Math.cos(this.yaw)*Math.cos(this.pitch)*this.distance];
    const normalize=v=>{const l=Math.hypot(...v)||1;return v.map(x=>x/l);},cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]],dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0);
    const az=normalize([eye[0],eye[1]-this.target,eye[2]]),ax=normalize(cross([0,1,0],az)),ay=cross(az,ax),view=[ax[0],ay[0],az[0],0,ax[1],ay[1],az[1],0,ax[2],ay[2],az[2],0,-dot(ax,eye),-dot(ay,eye),-dot(az,eye),1],out=new Float32Array(16);
    for(let c=0;c<4;c++)for(let r=0;r<4;r++)for(let k=0;k<4;k++)out[c*4+r]+=proj[k*4+r]*view[c*4+k];return out;
  }
  frame(){const gl=this.gl;if(this.spin)this.yaw+=.005;gl.viewport(0,0,this.canvas.width,this.canvas.height);gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.enable(gl.DEPTH_TEST);gl.useProgram(this.program);gl.uniformMatrix4fv(this.mvp,false,this.matrix());
    for(const m of this.meshes){gl.bindVertexArray(m.vao);gl.bindTexture(gl.TEXTURE_2D,this.textures[m.texture]||this.textures.skin);gl.drawElements(gl.TRIANGLES,m.count,gl.UNSIGNED_INT,0);}requestAnimationFrame(()=>this.frame());}
}
window.UniformViewer=UniformViewer;
