/* Real Edge integration tests for the shipped token-bound CBS replay page.
 * This harness owns only a local HTTP server and browser pages. It does not
 * build, launch, inspect or mutate the native game process. Animation frames
 * and timers use a deterministic test clock; CSS is still the shipped CSS.
 * Run: node tools/cbs-scorebug/verify-native-replay.js
 */
'use strict';
const fs=require('fs'),path=require('path'),http=require('http'),assert=require('assert');
let playwright;
try{playwright=require('playwright');}catch(_){playwright=require(path.resolve(path.dirname(process.execPath),'../node_modules/playwright'));}
const root=path.resolve(__dirname,'../../mods/presentations');
const output=path.resolve(__dirname,'../../outputs/cbs-2026/native-replay-browser-20261019');
let checks=0;const results=[],errors=[];
function equal(a,b,label){assert.deepStrictEqual(a,b,label);checks++;}
function yes(v,label){assert.ok(v,label);checks++;}
const state={valid:true,matchId:'replay-test-A',gameStatus:'in_progress',quarter:2,gameClock:83,phase:4,possession:'away',
  away:{abbreviation:'DAL',score:7,timeouts:2},home:{abbreviation:'NE',score:3,timeouts:3},
  context:{scorebugVisible:false,periodEnding:false,paused:false,playSelection:false,replayActive:true}};
function event(name='PREPARE',token='17',direction='in',extra={}){return Object.assign({name:'NATIVE_REPLAY_'+name,replayToken:token,direction,source:'native_automatic_replay'},extra);}
function installedBrowser(){
  if(process.env.PLAYWRIGHT_BROWSER_PATH)return process.env.PLAYWRIGHT_BROWSER_PATH;
  const bundled=playwright.chromium.executablePath();if(fs.existsSync(bundled))return bundled;
  const core=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft/EdgeCore');
  if(fs.existsSync(core))for(const version of fs.readdirSync(core).filter(n=>/^\d+(?:\.\d+)+$/.test(n)).sort((a,b)=>b.localeCompare(a,undefined,{numeric:true}))){const exe=path.join(core,version,'msedge.exe');if(fs.existsSync(exe))return exe;}
  throw new Error('No installed Chromium/Edge found. Set PLAYWRIGHT_BROWSER_PATH.');
}
const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.png':'image/png','.woff2':'font/woff2'};
const server=http.createServer((req,res)=>{
  let file;try{file=path.resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://127.0.0.1').pathname));}catch(_){res.writeHead(400);res.end();return;}
  if(!file.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}
  fs.readFile(file,(error,data)=>{if(error){res.writeHead(404);res.end();return;}res.writeHead(200,{'Content-Type':mime[path.extname(file)]||'application/octet-stream'});res.end(data);});
});
async function main(){
  fs.mkdirSync(output,{recursive:true});
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const browser=await playwright.chromium.launch({headless:true,executablePath:installedBrowser()});
  // Playwright's in-page polling uses window timers. This fixture controls
  // those timers, so readiness waits must poll from Node's unaffected clock.
  async function until(page,predicate,arg,timeout=5000){
    const deadline=Date.now()+timeout;
    do{if(await page.evaluate(predicate,arg))return;await new Promise(resolve=>setTimeout(resolve,20));}while(Date.now()<deadline);
    throw new Error('Timed out waiting for replay browser condition: '+String(predicate));
  }
  async function fixture(options={}){
    const page=await browser.newPage({viewport:{width:1920,height:1080},reducedMotion:options.reduced?'reduce':'no-preference'});
    page.on('pageerror',e=>errors.push(e.message));
    if(options.brokenAsset)await page.route('**/assets/branding/nfl-shield.svg',route=>route.abort());
    await page.addInitScript(options=>{
      window.NFL2K5_HOST=options.host||'game';window.__replayMessages=[];
      const realFonts=document.fonts;window.__realFonts=realFonts;
      let resolve,reject;const pending=new Promise((a,b)=>{resolve=a;reject=b;});
      window.__resolveFonts=()=>resolve();window.__rejectFonts=()=>reject(new Error('test font failure'));
      Object.defineProperty(document,'fonts',{configurable:true,value:{ready:pending,get status(){return options.fontStatus||realFonts.status;},check:q=>!options.fontCheckFails&&realFonts.check(q)}});
      let now=0,nextId=1;const frames=new Map(),timers=new Map(),realFrame=window.requestAnimationFrame.bind(window);
      Object.defineProperty(performance,'now',{configurable:true,value:()=>now});
      window.requestAnimationFrame=fn=>{const id=nextId++;frames.set(id,fn);return id;};
      window.cancelAnimationFrame=id=>frames.delete(id);
      window.setTimeout=(fn,delay,...args)=>{const id=nextId++;timers.set(id,{at:now+Math.max(0,Number(delay)||0),fn:()=>fn(...args)});return id;};
      window.clearTimeout=id=>timers.delete(id);
      window.__frame=delta=>new Promise(done=>realFrame(()=>{now+=delta||0;const batch=Array.from(frames);frames.clear();batch.forEach(([,fn])=>fn(now));done();}));
      window.__time=delta=>{now+=delta||0;let limit=1000;while(limit--){const pending=Array.from(timers).filter(([,timer])=>timer.at<=now).sort((a,b)=>a[1].at-b[1].at);if(!pending.length)return;pending.forEach(([id,timer])=>{if(timers.delete(id))timer.fn();});}throw new Error('Test timer loop did not settle');};
    },options);
    await page.goto('http://127.0.0.1:'+server.address().port+'/CBS_2026/scorebug.html');
    await until(page,()=>!!(window.CBSNativeReplay&&window.CBSReplayWipe&&window.NFL2K5.listening));
    await page.evaluate(async s=>{NFL2K5.log=text=>__replayMessages.push(text);await __realFonts.ready;NFL2K5.dispatch({type:'state',state:s});},state);
    await page.evaluate(()=>__realFonts.ready);
    return page;
  }
  async function emit(p,e){await p.evaluate(e=>NFL2K5.dispatch({type:'event',event:e}),e);}
  async function update(p,change){await p.evaluate(change=>{const next=JSON.parse(JSON.stringify(NFL2K5.state));Object.assign(next,change);NFL2K5.dispatch({type:'state',state:next});},change);}
  async function snap(p){return p.evaluate(()=>({owner:CBSNativeReplay.snapshot(),messages:__replayMessages.slice(),hidden:document.getElementById('cbs-replay-wipe').hidden,time:document.getElementById('cbs-replay-wipe').getAttribute('data-controlled-time-ms'),eyebarHidden:document.getElementById('eyebar').classList.contains('hidden'),eyebarAria:document.getElementById('eyebar').getAttribute('aria-hidden'),networkVisible:document.getElementById('network').classList.contains('on'),networkAria:document.getElementById('network').getAttribute('aria-hidden'),networkZ:Number(getComputedStyle(document.getElementById('network')).zIndex),wipeZ:Number(getComputedStyle(document.getElementById('cbs-replay-wipe')).zIndex),animations:document.getElementById('cbs-replay-wipe').getAnimations({subtree:true}).map(a=>({time:a.currentTime,state:a.playState}))}));}
  async function frame(p,ms=0){await p.evaluate(ms=>__frame(ms),ms);}
  async function paint(p){await frame(p);await frame(p);}
  async function ready(p,token='17',direction='in'){
    await emit(p,event('PREPARE',token,direction));await p.evaluate(()=>__resolveFonts());
    try{await until(p,t=>__replayMessages.includes('NFL2K5_REPLAY_READY:'+t),token);}
    catch(error){console.error('Replay readiness snapshot: '+JSON.stringify(await p.evaluate(()=>({owner:CBSNativeReplay.snapshot(),messages:__replayMessages,state:NFL2K5.state,fontStatus:document.fonts.status,motion:document.documentElement.getAttribute('data-preview-motion')}))));throw error;}
  }
  async function start(p,token='17',direction='in'){await emit(p,event('START',token,direction));await paint(p);}
  async function coverage(p,name){
    // Keep only the transition layer and transparent ancestors for an alpha
    // measurement of actual browser rasterization, not a CSS-label assertion.
    const style=await p.addStyleTag({content:'html,body,#canvas{background:transparent!important}#canvas>*:not(#cbs-replay-wipe){visibility:hidden!important}'});
    const png=await p.screenshot({omitBackground:true});await style.evaluate(n=>n.remove());
    equal(await p.evaluate(()=>document.getElementById('network').classList.contains('on')),true,'Wipe-only raster isolation restores persistent network corner after '+name);
    fs.writeFileSync(path.join(output,name+'.png'),png);
    const alpha=await p.evaluate(data=>new Promise((resolve,reject)=>{const image=new Image();image.onload=()=>{const canvas=document.createElement('canvas');canvas.width=image.width;canvas.height=image.height;const ctx=canvas.getContext('2d',{willReadFrequently:true});ctx.drawImage(image,0,0);const pixels=ctx.getImageData(0,0,image.width,image.height).data;let opaque=0,nonzero=0,min=255;for(let i=3;i<pixels.length;i+=4){if(pixels[i]>=253)opaque++;if(pixels[i])nonzero++;min=Math.min(min,pixels[i]);}resolve({width:image.width,height:image.height,opaqueFraction:opaque/(pixels.length/4),nonzeroFraction:nonzero/(pixels.length/4),minimumAlpha:min});};image.onerror=reject;image.src=data;}),'data:image/png;base64,'+png.toString('base64'));
    results.push({name,...alpha});return alpha;
  }
  async function cornerAboveCover(p,name){
    await until(p,()=>Number(getComputedStyle(document.getElementById('network')).opacity)===1);
    const visible=await p.screenshot({omitBackground:true});
    fs.writeFileSync(path.join(output,name+'-with-network.png'),visible);
    const style=await p.addStyleTag({content:'#network{visibility:hidden!important}'});
    const concealed=await p.screenshot({omitBackground:true});await style.evaluate(n=>n.remove());
    const changed=await p.evaluate(data=>Promise.all(data.map(src=>new Promise((resolve,reject)=>{const image=new Image();image.onload=()=>{const canvas=document.createElement('canvas');canvas.width=image.width;canvas.height=image.height;const ctx=canvas.getContext('2d',{willReadFrequently:true});ctx.drawImage(image,0,0);resolve(ctx.getImageData(0,0,image.width,image.height));};image.onerror=reject;image.src=src;}))).then(images=>{const r=document.getElementById('network').getBoundingClientRect();let changed=0,total=0;for(let y=Math.floor(r.top);y<Math.ceil(r.bottom);y++)for(let x=Math.floor(r.left);x<Math.ceil(r.right);x++){const i=(y*images[0].width+x)*4;total++;if(Math.max(...[0,1,2,3].map(c=>Math.abs(images[0].data[i+c]-images[1].data[i+c])))>4)changed++;}return {changed,total,fraction:changed/total};}),[visible,concealed].map(png=>'data:image/png;base64,'+png.toString('base64')));
    yes(changed.fraction>.01,name+' actual corner pixels remain visible above fully opaque wipe');
    results.push({name:name+'-network-corner',...changed});
  }
  try{
    for(const direction of ['in','out']){
      let p=await fixture();await emit(p,event('PREPARE','17',direction));let s=await snap(p);
      equal(s.messages,[],'No READY before required fonts');equal(s.hidden,true,'PREPARE keeps native visual fallback');
      equal(s.eyebarHidden,true,'Replay hides lower '+direction+' Eyebar');equal(s.eyebarAria,'true','Replay Eyebar is accessibility-hidden');
      equal(s.networkVisible,true,'Replay preserves independent CBS corner');equal(s.networkAria,'false','Replay corner remains accessibility-visible');
      await p.evaluate(()=>__resolveFonts());await until(p,()=>__replayMessages.includes('NFL2K5_REPLAY_READY:17'));
      equal((await snap(p)).messages,['NFL2K5_REPLAY_READY:17'],'Valid fonts and actual images produce one READY');
      await emit(p,event('START','17',direction));equal((await snap(p)).owner.started,false,'START waits for two painted frames');
      await frame(p);equal((await snap(p)).owner.started,false,'First RAF cannot acknowledge paint');
      await frame(p);s=await snap(p);equal(s.owner.started,true,'Second RAF acknowledges START');equal(s.messages.filter(m=>m==='NFL2K5_REPLAY_START:17').length,1,'One painted START acknowledgment');
      const cover=direction==='in'?880:40;await frame(p,cover);equal((await snap(p)).owner.covered,true,'Arrives at '+direction+' cover marker');
      equal((await snap(p)).messages.includes('NFL2K5_REPLAY_COVER:17'),false,'Cover is not acknowledged before paint');
      await paint(p);equal((await snap(p)).messages.filter(m=>m==='NFL2K5_REPLAY_COVER:17').length,1,'Painted cover acknowledgment');
      const beforeEcho=await snap(p);
      await emit(p,{name:'REPLAY_STARTED'});await emit(p,{name:'REPLAY_ENDED'});
      const afterEcho=await snap(p);
      equal(afterEcho.owner,beforeEcho.owner,'Legacy replay echoes do not mutate active '+direction+' token ownership');
      equal(afterEcho.time,beforeEcho.time,'Legacy replay echoes do not reset controlled '+direction+' timeline');
      equal(afterEcho.hidden,false,'Legacy replay echoes cannot hide the controlled '+direction+' cover');
      equal(afterEcho.messages,beforeEcho.messages,'Legacy replay echoes cannot duplicate token acknowledgments');
      yes(afterEcho.animations.every(a=>a.state==='paused'&&a.time===cover),'Legacy replay echoes cannot replace paused '+direction+' animations with wall-clock animations');
      const a=await coverage(p,direction+'-cover');yes(a.opaqueFraction>=.99999,direction+' cover is opaque over the full viewport');
      s=await snap(p);yes(s.networkZ>s.wipeZ,direction+' CBS corner stacks above replay wipe');
      await cornerAboveCover(p,direction+'-cover');
      await p.evaluate(()=>__time(1500));await frame(p);s=await snap(p);
      equal(s.hidden,false,'Controlled '+direction+' hold survives legacy 1050 ms cutoff');equal(s.time,String(cover),'Hold does not advance beyond camera handoff');yes(s.animations.every(a=>a.state==='paused'&&a.time===cover),'All actual CSS animations remain paused at cover');
      const b=await coverage(p,direction+'-hold-1500ms');equal(b.opaqueFraction,a.opaqueFraction,'Held cover keeps identical opacity');
      await emit(p,event('HANDOFF','16',direction));equal((await snap(p)).owner.handed,false,'Old token cannot release held cover');
      await emit(p,event('HANDOFF','17',direction==='in'?'out':'in'));equal((await snap(p)).owner.handed,false,'Wrong direction cannot release held cover');
      await emit(p,event('HANDOFF','17',direction));equal((await snap(p)).owner.handed,true,'Exact native handoff releases cover');
      await frame(p,1050-cover);s=await snap(p);equal(s.owner.done,true,'Timeline reaches exact 1050 ms');equal(s.messages.includes('NFL2K5_REPLAY_DONE:17'),false,'DONE waits for completed frame paint');
      await paint(p);s=await snap(p);equal(s.messages.filter(m=>m==='NFL2K5_REPLAY_DONE:17').length,1,'Completed frame acknowledges DONE once');
      const end=await coverage(p,direction+'-done');equal(end.nonzeroFraction,0,'Completed transition is transparent');
      await emit(p,event('CANCEL','17',direction));s=await snap(p);equal(s.owner.token,'','Native cancellation releases token');equal(s.hidden,true,'Native cancellation hides transition');
      equal(s.messages.filter(m=>m==='NFL2K5_REPLAY_CANCEL:17').length,0,'Native cancellation does not echo cancellation');await p.close();
    }
    let p=await fixture();await p.evaluate(()=>__resolveFonts());
    for(const token of ['0','01','+1','-1','4294967296','1.0',17,null,'1e2',' 17','17 ']){await emit(p,event('PREPARE',token));equal((await snap(p)).owner.token,'','Reject malformed token '+JSON.stringify(token));}
    for(const extra of [{source:'fixture'},{direction:'sideways'}]){await emit(p,event('PREPARE','17','in',extra));equal((await snap(p)).owner.token,'','Reject malformed source or direction');}await p.close();
    for(const [label,change] of [['pause',{context:{replayActive:true,paused:true}}],['play selection',{context:{replayActive:true,playSelection:true}}],['animations disabled',{context:{replayActive:true,animationsEnabled:false}}],['replay ended',{context:{replayActive:false}}],['invalid game',{valid:false}],['inactive game',{gameStatus:'inactive'}],['match change',{matchId:'replay-test-B'}],['team change',{home:{abbreviation:'KC'}}]]){
      p=await fixture();await ready(p);await start(p);await update(p,change);await paint(p);const s=await snap(p);
      equal(s.owner.token,'',label+' invalidates owner');equal(s.hidden,true,label+' restores native fallback');equal(s.messages.filter(m=>m==='NFL2K5_REPLAY_CANCEL:17').length,1,label+' reports cancellation once');
      if(['pause','play selection','invalid game','inactive game','replay ended'].includes(label)){
        equal(s.eyebarHidden,true,label+' suppresses lower Eyebar');equal(s.networkVisible,false,label+' suppresses CBS corner');
        equal(s.networkAria,'true',label+' corner is accessibility-hidden');
      }
      await p.close();
    }
    p=await fixture();await ready(p);await emit(p,event('START'));await emit(p,event('CANCEL'));await paint(p);
    equal((await snap(p)).messages.filter(m=>m==='NFL2K5_REPLAY_START:17').length,0,'Cancelled pending paint never acknowledges');await p.close();
    p=await fixture();await emit(p,event('PREPARE'));await emit(p,event('START'));await paint(p);
    equal((await snap(p)).owner.started,false,'START cannot bypass unresolved PREPARE asset/font readiness');
    equal((await snap(p)).messages.includes('NFL2K5_REPLAY_START:17'),false,'Unready transition cannot acquire painted ownership');await p.close();
    p=await fixture();await ready(p);await emit(p,event('START'));await emit(p,event('START'));await paint(p);
    equal((await snap(p)).messages.filter(m=>m==='NFL2K5_REPLAY_START:17').length,1,'Repeated START before first painted acknowledgment is idempotent');
    await frame(p,880);await paint(p);await emit(p,event('HANDOFF'));await frame(p,170);await paint(p);
    equal((await snap(p)).messages.filter(m=>m==='NFL2K5_REPLAY_DONE:17').length,1,'Repeated START cannot fork the RAF loop or duplicate DONE');await p.close();
    p=await fixture();await ready(p,'4294967295','out');await emit(p,event('PREPARE','4294967295','out'));
    equal((await snap(p)).messages.filter(m=>m==='NFL2K5_REPLAY_READY:4294967295').length,1,'Duplicate PREPARE is idempotent at maximum uint32 token');
    await emit(p,event('HANDOFF','4294967295','out'));equal((await snap(p)).owner.handed,false,'Early HANDOFF cannot bypass painted cover');
    await emit(p,event('START','4294967295','in'));await paint(p);equal((await snap(p)).owner.started,false,'Wrong-direction START is ignored');
    await emit(p,event('START','4294967295','out',{source:'fixture'}));await paint(p);equal((await snap(p)).owner.started,false,'Wrong-source START is ignored');
    await start(p,'4294967295','out');await frame(p,40);await paint(p);await emit(p,event('HANDOFF','4294967295','out'));await frame(p,1010);await paint(p);
    equal((await snap(p)).owner.done,true,'Maximum uint32 token completes through exact handoff');await p.close();
    p=await fixture();await ready(p);await emit(p,event('PREPARE','18','out'));await until(p,()=>__replayMessages.includes('NFL2K5_REPLAY_READY:18'));
    equal((await snap(p)).messages.filter(m=>m==='NFL2K5_REPLAY_CANCEL:17').length,1,'New owner cancels replaced token');
    await emit(p,event('CANCEL','17'));equal((await snap(p)).owner.token,'18','Stale cancellation cannot remove replacement');await p.close();
    for(const options of [{reduced:true},{brokenAsset:true}]){
      p=await fixture(options);await emit(p,event('PREPARE'));await p.evaluate(()=>__resolveFonts());await until(p,()=>__replayMessages.includes('NFL2K5_REPLAY_CANCEL:17'));
      const messages=(await snap(p)).messages;
      equal(messages.includes('NFL2K5_REPLAY_READY:17'),false,'Reduced motion or failed asset keeps native visual fallback');
      yes(messages.some(m=>m.startsWith(options.reduced?'CBS_REPLAY_FALLBACK:reduced-motion':'CBS_REPLAY_FALLBACK:asset:assets/branding/nfl-shield.svg')),'Reduced motion or failed asset records its fallback reason');await p.close();
    }
    // Exercise the real shipped scorebug.onState router. No test code writes
    // data-preview-motion: the explicit game setting must perform that work.
    p=await fixture({reduced:true});
    equal(await p.evaluate(()=>document.documentElement.getAttribute('data-preview-motion')),null,'Absent game setting leaves fresh browser/system motion policy intact');
    await update(p,{context:{replayActive:true,animationsEnabled:true}});
    equal(await p.evaluate(()=>document.documentElement.getAttribute('data-preview-motion')),'full','Real scorebug state router applies explicit game Animations ON');
    await ready(p);equal((await snap(p)).owner.token,'17','Explicit ON permits PREPARE under reduced-motion OS');
    await start(p);equal((await snap(p)).owner.started,true,'Explicit ON paints actual native replay under reduced-motion OS');
    await frame(p,880);await paint(p);
    yes((await snap(p)).messages.includes('NFL2K5_REPLAY_COVER:17'),'Explicit ON reaches painted cover under reduced-motion OS');
    yes((await coverage(p,'os-reduced-game-on-cover')).opaqueFraction>=.99999,'Explicit ON overrides OS CSS suppression and fully covers viewport');
    await update(p,{context:{replayActive:true,animationsEnabled:false}});
    let settingSnap=await snap(p);
    equal(await p.evaluate(()=>document.documentElement.getAttribute('data-preview-motion')),'reduced','Real scorebug state router applies explicit game Animations OFF');
    equal(settingSnap.owner.token,'','Explicit OFF releases already active native replay');equal(settingSnap.hidden,true,'Explicit OFF hides transition');
    equal(settingSnap.messages.filter(m=>m==='NFL2K5_REPLAY_CANCEL:17').length,1,'Explicit OFF reports exactly one token cancellation');
    await emit(p,event('PREPARE','18','out'));await paint(p);settingSnap=await snap(p);
    equal(settingSnap.owner.token,'','Explicit OFF refuses subsequent native ownership');
    equal(settingSnap.messages.includes('NFL2K5_REPLAY_READY:18'),false,'Explicit OFF cannot send READY');
    await update(p,{context:{replayActive:true,animationsEnabled:true}});await ready(p,'19','out');await start(p,'19','out');
    equal((await snap(p)).owner.started,true,'Turning game Animations back ON permits fresh native ownership');await p.close();
    p=await fixture();await update(p,{context:{replayActive:true,animationsEnabled:false}});await emit(p,event('PREPARE'));await paint(p);
    equal((await snap(p)).owner.token,'','Game Animations OFF refuses ownership even without reduced-motion OS');
    equal((await snap(p)).messages.includes('NFL2K5_REPLAY_READY:17'),false,'Game Animations OFF sends no READY on normal OS');await p.close();
    p=await fixture();await emit(p,event('PREPARE'));await p.evaluate(()=>__rejectFonts());await until(p,()=>__replayMessages.includes('NFL2K5_REPLAY_CANCEL:17'));
    equal((await snap(p)).owner.token,'','Rejected font readiness cancels token');await p.close();
    p=await fixture();await emit(p,event('PREPARE'));await p.evaluate(()=>__time(4200));equal((await snap(p)).owner.token,'','Pending assets/fonts watchdog is bounded');await p.close();
    p=await fixture();await ready(p);await start(p);await frame(p,880);await paint(p);await p.evaluate(()=>__time(4200));
    equal((await snap(p)).owner.token,'','Held handoff watchdog is bounded');equal((await snap(p)).hidden,true,'Watchdog restores native visuals');await p.close();
    p=await fixture();await ready(p);await p.addStyleTag({content:'#canvas{opacity:0!important}'});await emit(p,event('START'));await paint(p);
    equal((await snap(p)).messages.includes('NFL2K5_REPLAY_START:17'),false,'Invisible ancestor prevents painted ownership');equal((await snap(p)).owner.token,'','Invisible transition cancels');await p.close();
    p=await fixture({host:'browser'});await emit(p,event('PREPARE'));await p.evaluate(()=>__resolveFonts());await p.evaluate(()=>__time(4200));
    equal((await snap(p)).messages,[],'Browser preview cannot send native acknowledgments');await p.close();
    equal(errors,[],'No browser script errors');
    fs.writeFileSync(path.join(output,'coverage.json'),JSON.stringify({checks,results},null,2)+'\n');
    console.log('PASS '+checks+' CBS native replay assertions in real Chromium/Edge.');
  }finally{await browser.close();server.close();}
}
main().catch(error=>{server.close();console.error(error.stack||error);process.exitCode=1;});
