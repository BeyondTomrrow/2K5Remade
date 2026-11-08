/* Real Edge paint-ownership checks for the shipped CBS corner adapter.
 * These are browser fixtures, not a native SKY CAM replacement capture. */
'use strict';
const fs=require('fs'),path=require('path'),http=require('http'),assert=require('assert');
let playwright;try{playwright=require('playwright');}catch(_){playwright=require(path.resolve(path.dirname(process.execPath),'../node_modules/playwright'));}
const root=path.resolve(__dirname,'../../mods/presentations');let checks=0;const errors=[];
const state={valid:true,matchId:'camera-A',gameStatus:'in_progress',quarter:1,gameClock:245,playClock:27,phase:4,possession:'away',down:2,distance:7,away:{abbreviation:'DAL',score:7,timeouts:3},home:{abbreviation:'NE',score:3,timeouts:2},context:{scorebugVisible:false,replayActive:true,paused:false,playSelection:false}};
function yes(a,label){assert.ok(a,label);checks++;}function equal(a,b,label){assert.deepStrictEqual(a,b,label);checks++;}
function event(token='17',extra={}){return Object.assign({name:'CAMERA_MARK_SHOW',source:'native',nativeVisible:true,nativeCameraToken:token},extra);}
function installedBrowser(){if(process.env.PLAYWRIGHT_BROWSER_PATH)return process.env.PLAYWRIGHT_BROWSER_PATH;const bundled=playwright.chromium.executablePath();if(fs.existsSync(bundled))return bundled;const core=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft/EdgeCore');if(fs.existsSync(core))for(const v of fs.readdirSync(core).filter(n=>/^\d+(?:\.\d+)+$/.test(n)).sort((a,b)=>b.localeCompare(a,undefined,{numeric:true}))){const exe=path.join(core,v,'msedge.exe');if(fs.existsSync(exe))return exe;}throw new Error('No installed Edge/Chromium.');}
const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.png':'image/png','.woff2':'font/woff2','.woff':'font/woff','.ttf':'font/ttf','.otf':'font/otf'};
const server=http.createServer((req,res)=>{let f;try{f=path.resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://127.0.0.1').pathname));}catch(_){res.writeHead(400);res.end();return;}if(!f.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}fs.readFile(f,(e,d)=>{res.writeHead(e?404:200,{'Content-Type':mime[path.extname(f)]||'application/octet-stream'});res.end(e?undefined:d);});});
async function main(){
  await new Promise(r=>server.listen(0,'127.0.0.1',r));const browser=await playwright.chromium.launch({headless:true,executablePath:installedBrowser()});
  const delay=ms=>new Promise(r=>setTimeout(r,ms));
  async function fixture(host='game',broken=false){
    const p=await browser.newPage({viewport:{width:1920,height:1080},reducedMotion:'reduce'});p.on('pageerror',e=>errors.push(e.message));
    if(broken)await p.route('**/assets/branding/cbs-sports.svg',r=>r.abort());
    await p.addInitScript(host=>{window.NFL2K5_HOST=host;window.__cameraMessages=[];const real=requestAnimationFrame.bind(window);let n=0;const frames=new Map();window.requestAnimationFrame=fn=>{frames.set(++n,fn);return n;};window.cancelAnimationFrame=id=>frames.delete(id);window.__cameraFrame=()=>new Promise(done=>real(t=>{const batch=Array.from(frames);frames.clear();batch.forEach(([,fn])=>fn(t));done();}));},host);
    await p.goto('http://127.0.0.1:'+server.address().port+'/CBS_2026/scorebug.html');
    await p.waitForFunction(()=>window.NFL2K5&&window.CBSScorebug);
    if(!await p.evaluate(()=>!!window.CBSCameraMark))await p.addScriptTag({path:path.join(root,'CBS_2026/camera-mark.js')});
    await p.evaluate(async s=>{NFL2K5.log=t=>__cameraMessages.push(t);NFL2K5.dispatch({type:'state',state:s});CBSCameraMark.updateState(s);document.getElementById('network').style.transition='none';await document.fonts.ready;},state);
    if(!broken)await p.waitForFunction(()=>{const n=document.querySelector('#network img');return n.complete&&n.naturalWidth>0;});return p;
  }
  async function snap(p){return p.evaluate(()=>({owner:CBSCameraMark.snapshot(),messages:__cameraMessages.slice(),count:document.querySelectorAll('#network').length,src:document.querySelector('#network img').getAttribute('src'),html:document.getElementById('network').outerHTML}));}
  async function send(p,e){return p.evaluate(e=>CBSCameraMark.handleEvent(e),e);}
  async function frame(p){await p.evaluate(()=>__cameraFrame());}async function paint(p){await frame(p);await frame(p);}
  async function update(p,change){await p.evaluate(change=>{const s=JSON.parse(JSON.stringify(NFL2K5.state));Object.assign(s,change);NFL2K5.dispatch({type:'state',state:s});CBSCameraMark.updateState(s);},change);}
  try{
    let p=await fixture();const before=await snap(p);equal(before.owner.active,false,'No invented popup before actual native event');yes(await send(p,event()),'Consumes typed camera event');let s=await snap(p);
    yes(s.owner.active&&!s.owner.acknowledged,'Exact instance waits for visible paint');await frame(p);equal((await snap(p)).owner.acknowledged,false,'First visible frame cannot acknowledge');await frame(p);s=await snap(p);
    yes(s.owner.painted&&s.owner.acknowledged,'Second visible frame acknowledges real corner');equal(s.messages,['NFL2K5_CAMERA_MARK_SHOW:17'],'One SHOW for painted token');equal(s.count,1,'Adapter adds no second logo');equal(s.html,before.html,'Adapter never changes original network artwork or style');
    await send(p,event());await paint(p);equal((await snap(p)).messages.length,1,'Duplicate SHOW does not replay ACK');
    await send(p,event('18',{name:'CAMERA_MARK_HIDE',nativeVisible:false}));equal((await snap(p)).owner.token,'17','Stale HIDE cannot release current owner');
    await send(p,event('17',{name:'CAMERA_MARK_HIDE',nativeVisible:true}));yes((await snap(p)).owner.active,'Malformed visible HIDE is rejected');
    await p.evaluate(()=>document.getElementById('network').style.opacity='.5');await frame(p);s=await snap(p);yes(s.owner.active&&!s.owner.acknowledged,'Fading corner immediately revokes native replacement');equal(s.messages.filter(x=>x==='NFL2K5_CAMERA_MARK_HIDE:17').length,1,'Lost paint sends one HIDE');
    await paint(p);equal((await snap(p)).messages.filter(x=>x==='NFL2K5_CAMERA_MARK_HIDE:17').length,1,'Hidden monitor does not spam HIDE');await p.evaluate(()=>document.getElementById('network').style.opacity='1');await frame(p);equal((await snap(p)).owner.acknowledged,false,'Reappearance requires two fresh painted frames');await frame(p);equal((await snap(p)).messages.filter(x=>x==='NFL2K5_CAMERA_MARK_SHOW:17').length,2,'Same native instance can reacquire visibly painted corner');
    await send(p,event('4294967295'));equal((await snap(p)).owner.token,'4294967295','Maximum uint32 token accepted');await paint(p);yes((await snap(p)).owner.acknowledged,'Replacement token paints');await send(p,event('17',{name:'CAMERA_MARK_HIDE',nativeVisible:false}));yes((await snap(p)).owner.active,'Delayed old HIDE cannot clear replacement');
    await send(p,event('4294967295',{name:'CAMERA_MARK_HIDE',nativeVisible:false}));equal((await snap(p)).owner.active,false,'Exact native HIDE releases');await paint(p);equal((await snap(p)).owner.acknowledged,false,'Cancelled RAF cannot acknowledge late');await p.close();
    for(const token of ['0','01','+1','-1','4294967296','1.0',17,null,'1e2',' 17','17 ']){p=await fixture();await send(p,event(token));await paint(p);equal((await snap(p)).owner.active,false,'Reject noncanonical token '+JSON.stringify(token));await p.close();}
    for(const extra of [{source:'fixture'},{nativeVisible:false}]){p=await fixture();await send(p,event('17',extra));await paint(p);equal((await snap(p)).owner.active,false,'Reject malformed native source/visibility');await p.close();}
    for(const [label,change] of [['pause',{context:{paused:true,replayActive:true}}],['play selection',{context:{playSelection:true,replayActive:true}}],['replay ended',{context:{replayActive:false}}],['inactive',{gameStatus:'inactive'}],['invalid',{valid:false}],['match change',{matchId:'camera-B'}],['team change',{home:{abbreviation:'BUF'}}]]){p=await fixture();await send(p,event());await paint(p);await update(p,change);const s=await snap(p);equal(s.owner.active,false,label+' releases exact instance');yes(s.messages.includes('NFL2K5_CAMERA_MARK_HIDE:17'),label+' revokes native suppression');await p.close();}
    for(const [label,code] of [['hidden ancestor',()=>document.getElementById('canvas').style.visibility='hidden'],['unloaded logo',()=>document.querySelector('#network img').src='missing-camera-mark-test.svg'],['outside viewport',()=>document.getElementById('network').style.right='-1000px'],['zero geometry',()=>document.getElementById('network').style.width='0px']]){p=await fixture();await p.evaluate(code);await send(p,event());await paint(p);equal((await snap(p)).owner.acknowledged,false,label+' retains native fallback');await p.close();}
    p=await fixture('game',true);await send(p,event());await paint(p);equal((await snap(p)).owner.acknowledged,false,'Failed logo cannot acknowledge');await delay(1050);await frame(p);s=await snap(p);yes(s.messages.includes('CBS_CAMERA_MARK_FALLBACK:not-painted'),'Unpainted timeout diagnoses fallback');yes(s.owner.active&&!s.owner.acknowledged,'Timeout never fakes painted ownership');await p.close();
    p=await fixture('browser');await send(p,event());await paint(p);s=await snap(p);yes(s.owner.painted&&!s.owner.acknowledged,'Browser fixture can inspect paint without native ownership');equal(s.messages.length,0,'Standalone browser sends no native SHOW');await p.evaluate(()=>CBSCameraMark.cancel());equal((await snap(p)).messages.length,0,'Standalone browser sends no native HIDE');await p.close();
    equal(errors,[],'No browser script errors');console.log('PASS '+checks+' CBS camera-mark assertions in real Chromium/Edge.');
  }finally{await browser.close();server.close();}
}
main().catch(e=>{server.close();console.error(e.stack||e);process.exitCode=1;});
