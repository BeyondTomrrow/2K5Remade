/* Measure actual CBS wipe pixels in a real Chromium/Edge browser.
 * The shipped page and assets are read only. Timers and CSS time are
 * controlled only in this disposable analysis page; no native game runs.
 * node tools/cbs-scorebug/measure-replay-coverage.js [output-directory]
 */
'use strict';
const fs=require('fs'),path=require('path'),http=require('http'),crypto=require('crypto'),assert=require('assert');
function dependency(name){try{return require(name);}catch(_){return require(path.resolve(path.dirname(process.execPath),'../node_modules',name));}}
const playwright=dependency('playwright'),PNG=dependency('pngjs').PNG;
const root=path.resolve(__dirname,'../../mods/presentations');
const output=path.resolve(process.argv[2]||path.join(__dirname,'../../outputs/cbs-2026/replay-coverage-20261019'));
const sourceFiles=['CBS_2026/scorebug.html','CBS_2026/replay-wipe.js','CBS_2026/replay-wipe.css','CBS_2026/assets/branding/nfl-shield.svg','CBS_2026/assets/branding/cbs-sports.svg'];
const hashSources=()=>Object.fromEntries(sourceFiles.map(file=>[file,crypto.createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex')]));
const sourceHashes=hashSources();
const controlled=process.env.REPLAY_COVERAGE_CONTROLLED==='1';
const times=process.env.REPLAY_COVERAGE_RETURN==='1'
  ?{in:[880],out:[0,20,40,60,67,67.2,68,70,80,96.6,100,120]}
  :process.env.REPLAY_COVERAGE_FINE==='1'
  ?{in:[846,848,850,852,854,856,858,902,903,904],out:[30,35,40,45,50]}
  :{in:[800,840,860,880,900,920],out:[0,20,40,60,80,100,120]};
function installedBrowser(){
  if(process.env.PLAYWRIGHT_BROWSER_PATH)return process.env.PLAYWRIGHT_BROWSER_PATH;
  const bundled=playwright.chromium.executablePath();if(fs.existsSync(bundled))return bundled;
  const core=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft/EdgeCore');
  if(fs.existsSync(core))for(const v of fs.readdirSync(core).filter(v=>/^\d+(?:\.\d+)+$/.test(v)).sort((a,b)=>a.localeCompare(b,undefined,{numeric:true})).reverse()){
    const exe=path.join(core,v,'msedge.exe');if(fs.existsSync(exe))return exe;
  }
  throw new Error('Set PLAYWRIGHT_BROWSER_PATH to an installed Chromium/Edge executable.');
}
const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.png':'image/png','.woff2':'font/woff2'};
const server=http.createServer((req,res)=>{
  let file;try{file=path.resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://127.0.0.1').pathname));}catch(_){res.writeHead(400);res.end();return;}
  if(!file.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}
  fs.readFile(file,(e,data)=>{if(e){res.writeHead(404);res.end();return;}res.writeHead(200,{'Content-Type':mime[path.extname(file)]||'application/octet-stream'});res.end(data);});
});
function coverage(buffer){
  const image=PNG.sync.read(buffer),{width,height,data}=image;
  assert.strictEqual(width,1920);assert.strictEqual(height,1080);
  const histogram=Array(256).fill(0),tiles=Array.from({length:9},()=>Array.from({length:16},()=>({pixels:0,opaque:0,ink:0,alphaSum:0})));
  let x0=width,y0=height,x1=-1,y1=-1;
  for(let y=0;y<height;y++)for(let x=0;x<width;x++){
    const alpha=data[(y*width+x)*4+3],tile=tiles[Math.floor(y/120)][Math.floor(x/120)];histogram[alpha]++;tile.pixels++;tile.alphaSum+=alpha;
    if(alpha){tile.ink++;x0=Math.min(x0,x);x1=Math.max(x1,x);y0=Math.min(y0,y);y1=Math.max(y1,y);}
    if(alpha>=253)tile.opaque++;
  }
  const pixels=width*height,opaque=histogram.slice(253).reduce((a,b)=>a+b,0),ink=pixels-histogram[0];
  return {width,height,pixels,alpha_threshold:253,nontransparent_pixels:ink,nontransparent_fraction:ink/pixels,
    opaque_pixels:opaque,opaque_fraction:opaque/pixels,fully_opaque_fraction:histogram[255]/pixels,
    mean_alpha:histogram.reduce((sum,count,a)=>sum+count*a,0)/(pixels*255),transparent_pixels:histogram[0],
    bounds:ink?{left:x0,top:y0,right:x1+1,bottom:y1+1}:null,alpha_histogram:histogram,
    tiles:tiles.map(row=>row.map(t=>({opaque_fraction:t.opaque/t.pixels,nontransparent_fraction:t.ink/t.pixels,mean_alpha:t.alphaSum/(t.pixels*255)})))};
}
async function main(){
  fs.mkdirSync(output,{recursive:true});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const executablePath=installedBrowser(),browser=await playwright.chromium.launch({headless:true,executablePath});
  let page;const errors=[];
  try{
    page=await browser.newPage({viewport:{width:1920,height:1080},deviceScaleFactor:1,reducedMotion:'no-preference'});
    page.on('pageerror',e=>errors.push(e.message));
    await page.addInitScript(()=>{
      // Native page timers are not modified. Only this test document keeps
      // JS lifecycle timers from canceling paused CSS while screenshots save.
      let next=1;const timers=new Map();window.__coverageTimers=timers;window.setTimeout=(fn,ms)=>{const id=next++;timers.set(id,{fn,ms});return id;};window.clearTimeout=id=>timers.delete(id);
    });
    await page.goto('http://127.0.0.1:'+server.address().port+'/CBS_2026/scorebug.html');
    await page.waitForFunction(()=>window.CBSReplayWipe);
    await page.addStyleTag({content:'html,body{background:transparent!important}#canvas>*:not(#cbs-replay-wipe){display:none!important}'});
    await page.evaluate(async()=>{await document.fonts.ready;await Promise.all(Array.from(document.images).map(img=>img.decode?img.decode().catch(()=>{}):Promise.resolve()));});
    const blank=coverage(await page.screenshot({omitBackground:true,animations:'allow'}));
    assert.strictEqual(blank.nontransparent_pixels,0,'Analysis page must have a completely transparent baseline.');
    const frames=[];
    for(const direction of ['in','out']){
      await page.evaluate(async ({direction,controlled})=>{
        const state={valid:true,gameStatus:'in_progress',away:{abbreviation:'KC'},home:{abbreviation:'DAL'},context:{paused:false,playSelection:false}};
        if(controlled){
          const before=__coverageTimers.size;
          if(!CBSReplayWipe.beginControlled(direction))throw new Error('Controlled animation failed to start.');
          if(__coverageTimers.size!==before)throw new Error('Controlled path started an internal lifecycle timer.');
        }else CBSReplayWipe.handleEvent({name:direction==='in'?'REPLAY_STARTED':'REPLAY_ENDED'},state);
        const root=document.getElementById('cbs-replay-wipe');void root.offsetWidth;
        window.__coverageAnimations=root.getAnimations({subtree:true});
        if(!__coverageAnimations.length)throw new Error('No actual CSS animations started.');
        for(const animation of __coverageAnimations){animation.pause();animation.currentTime=0;}
        await Promise.all(__coverageAnimations.map(animation=>animation.ready));
        await new Promise(requestAnimationFrame);
      },{direction,controlled});
      for(const ms of times[direction]){
        const styles=await page.evaluate(async ({ms,controlled})=>{
          if(controlled){if(!CBSReplayWipe.seekControlled(ms))throw new Error('Controlled seek failed.');}
          else for(const animation of __coverageAnimations){animation.pause();animation.currentTime=ms;}
          await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);
          const root=document.getElementById('cbs-replay-wipe');
          return {direction:CBSReplayWipe.direction(),running:CBSReplayWipe.isCovering(),root_hidden:root.hidden,
            animations:__coverageAnimations.map(a=>({name:a.animationName,current_time:a.currentTime,play_state:a.playState})),
            layers:Array.from(root.children).map(n=>{const s=getComputedStyle(n),r=n.getBoundingClientRect();return {class:n.getAttribute('class'),opacity:s.opacity,transform:s.transform,display:s.display,rect:{left:r.left,top:r.top,width:r.width,height:r.height}};})};
        },{ms,controlled});
        assert.strictEqual(styles.root_hidden,false);assert(styles.animations.every(a=>a.play_state==='paused'&&a.current_time===ms));
        const file=direction+'-'+String(ms).padStart(4,'0')+'.png',buffer=await page.screenshot({omitBackground:true,animations:'allow'});
        fs.writeFileSync(path.join(output,file),buffer);const measured=coverage(buffer);
        frames.push({direction,animation_time_ms:ms,file,...measured,styles});
        console.log(direction+' '+ms+'ms: alpha>0 '+(measured.nontransparent_fraction*100).toFixed(4)+'%; alpha>=.99 '+(measured.opaque_fraction*100).toFixed(4)+'%; mean '+measured.mean_alpha.toFixed(6));
      }
      await page.evaluate(()=>CBSReplayWipe.cancel());
    }
    const controlledChecks=await page.evaluate(async()=>{
      const results=[];function check(name,value){if(!value)throw new Error(name);results.push(name);}
      check('invalid direction rejected',CBSReplayWipe.beginControlled('invalid')===false);
      check('invalid direction leaves no active cover',!CBSReplayWipe.isCovering());
      for(const direction of ['in','out']){
        const timerCount=__coverageTimers.size;
        check(direction+' controlled starts',CBSReplayWipe.beginControlled(direction)===true);
        check(direction+' schedules no internal timers',__coverageTimers.size===timerCount);
        check(direction+' rejects non-finite seek',CBSReplayWipe.seekControlled(NaN)===false&&CBSReplayWipe.seekControlled(Infinity)===false);
        check(direction+' clamps negative seek',CBSReplayWipe.seekControlled(-20)===true&&document.getElementById('cbs-replay-wipe').getAnimations({subtree:true}).every(a=>a.currentTime===0));
        const coverTime=direction==='in'?880:40;
        check(direction+' seeks to cover',CBSReplayWipe.seekControlled(coverTime)===true);
        for(let frame=0;frame<12;frame++)await new Promise(requestAnimationFrame);
        check(direction+' holds paused cover across real frames',CBSReplayWipe.isCovering()&&document.getElementById('cbs-replay-wipe').getAnimations({subtree:true}).every(a=>a.currentTime===coverTime&&a.playState==='paused'));
        check(direction+' clamps end without implicit cancel',CBSReplayWipe.seekControlled(2000)===true&&CBSReplayWipe.isCovering()&&document.getElementById('cbs-replay-wipe').getAnimations({subtree:true}).every(a=>a.currentTime===1050));
        CBSReplayWipe.cancel();
        check(direction+' explicitly cancels',!CBSReplayWipe.isCovering()&&document.getElementById('cbs-replay-wipe').hidden);
        check(direction+' cannot seek canceled animation',CBSReplayWipe.seekControlled(40)===false);
      }
      return results;
    });
    // A fresh page retains real browser timers, proving the legacy path still
    // completes by itself rather than relying on the diagnostic timer shim.
    const legacyPage=await browser.newPage({viewport:{width:1920,height:1080},reducedMotion:'no-preference'});
    await legacyPage.goto('http://127.0.0.1:'+server.address().port+'/CBS_2026/scorebug.html');
    await legacyPage.waitForFunction(()=>window.CBSReplayWipe);
    const legacyStarted=await legacyPage.evaluate(()=>CBSReplayWipe.handleEvent({name:'REPLAY_ENDED'},{valid:true,gameStatus:'in_progress',away:{abbreviation:'KC'},home:{abbreviation:'DAL'},context:{paused:false,playSelection:false}})&&CBSReplayWipe.isCovering());
    assert(legacyStarted,'Legacy replay must start with normal browser timers.');
    await legacyPage.waitForFunction(()=>!CBSReplayWipe.isCovering(),null,{timeout:3000});
    const legacyStopped=await legacyPage.evaluate(()=>document.getElementById('cbs-replay-wipe').hidden);
    assert(legacyStopped,'Legacy replay must still self-cancel.');await legacyPage.close();
    assert.deepStrictEqual(errors,[],'No browser script errors.');assert.deepStrictEqual(hashSources(),sourceHashes,'Production sources must remain unchanged during capture.');
    const result={schema:1,controlled,measured_at:new Date().toISOString(),browser:await browser.version(),executablePath,
      method:'Actual shipped CBS scorebug.html in installed Edge; other presentation layers hidden in disposable page; real assets/CSS; each Web Animation paused and set to exact currentTime; transparent1920x1080 screenshots; alpha>=253/255 qualifies >=0.99.',
      source_hashes:sourceHashes,blank_baseline:blank,frames,controlled_checks:controlledChecks,legacy_real_timers:{started:legacyStarted,self_canceled:legacyStopped},
      peaks:['in','out'].map(direction=>{const peak=frames.filter(f=>f.direction===direction).sort((a,b)=>b.opaque_fraction-a.opaque_fraction)[0];return {direction,time_ms:peak.animation_time_ms,opaque_fraction:peak.opaque_fraction,nontransparent_fraction:peak.nontransparent_fraction,file:peak.file};})};
    fs.writeFileSync(path.join(output,'coverage.json'),JSON.stringify(result,null,2));
    console.log('PASS '+frames.length+' real browser samples; output '+output);
  }finally{await browser.close();server.close();}
}
main().catch(e=>{server.close();console.error(e.stack||e);process.exitCode=1;});
