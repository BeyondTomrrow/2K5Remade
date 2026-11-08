/* Exercise the shipped review-card component in real Edge. Test-owned
 * pages receive copied event fixtures; no native game/build is involved.
 */
'use strict';
const fs=require('fs'),path=require('path'),http=require('http'),assert=require('assert');
let playwright;try{playwright=require('playwright');}catch(_){playwright=require(path.resolve(path.dirname(process.execPath),'../node_modules/playwright'));}
const root=path.resolve(__dirname,'../../mods/presentations'),output=path.resolve(__dirname,'../../outputs/cbs-2026/review-card-browser-20261019');
let checks=0;const errors=[];
function equal(actual,expected,label){assert.deepStrictEqual(actual,expected,label);checks++;}
function yes(value,label){assert.ok(value,label);checks++;}
const state={valid:true,matchId:'review-test-A',gameStatus:'in_progress',quarter:2,gameClock:85,playClock:29,phase:4,possession:'home',down:2,distance:7,downDistanceText:'2ND & 7',away:{abbreviation:'KC',score:7,timeouts:2},home:{abbreviation:'DAL',score:14,timeouts:3},context:{scorebugVisible:true,paused:false,playSelection:false}};
function show(token='17',extra={}){return Object.assign({name:'REVIEW_SHOW',source:'native',nativeVisible:true,nativeReviewToken:token,heading:'Challenge',message:'Dallas is challenging the ruling on the field.',reason:'The previous play is under review.',reviewStage:'review'},extra);}
function hide(token='17',extra={}){return Object.assign({name:'REVIEW_HIDE',source:'native',nativeVisible:false,nativeReviewToken:token},extra);}
function installedBrowser(){if(process.env.PLAYWRIGHT_BROWSER_PATH)return process.env.PLAYWRIGHT_BROWSER_PATH;const bundled=playwright.chromium.executablePath();if(fs.existsSync(bundled))return bundled;const core=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft/EdgeCore');if(fs.existsSync(core))for(const version of fs.readdirSync(core).filter(n=>/^\d+(?:\.\d+)+$/.test(n)).sort((a,b)=>b.localeCompare(a,undefined,{numeric:true}))){const exe=path.join(core,version,'msedge.exe');if(fs.existsSync(exe))return exe;}throw new Error('No installed Edge/Chromium found.');}
const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.png':'image/png','.woff2':'font/woff2'};
const server=http.createServer((req,res)=>{let file;try{file=path.resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://127.0.0.1').pathname));}catch(_){res.writeHead(400);res.end();return;}if(!file.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}fs.readFile(file,(error,data)=>{if(error){res.writeHead(404);res.end();return;}res.writeHead(200,{'Content-Type':mime[path.extname(file)]||'application/octet-stream'});res.end(data);});});
async function main(){
  fs.mkdirSync(output,{recursive:true});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const browser=await playwright.chromium.launch({headless:true,executablePath:installedBrowser()});
  const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
  async function until(page,predicate,arg){const deadline=Date.now()+5000;do{if(await page.evaluate(predicate,arg))return;await delay(15);}while(Date.now()<deadline);console.error('Review condition snapshot: '+JSON.stringify(await page.evaluate(()=>({owner:window.CBSReviewCard&&CBSReviewCard.snapshot(),messages:window.__reviewMessages,state:window.NFL2K5&&NFL2K5.state,replay:window.__reviewReplay,fonts:document.fonts.status}))));throw new Error('Browser condition timed out: '+predicate);}
  async function fixture(options={}){
    const page=await browser.newPage({viewport:{width:1920,height:1080},reducedMotion:options.reduced?'reduce':'no-preference'});page.on('pageerror',e=>errors.push(e.message));
    if(options.brokenAsset)await page.route('**/assets/branding/cbs-eye.svg',route=>route.abort());
    await page.addInitScript(options=>{
      window.NFL2K5_HOST=options.host||'game';window.__reviewMessages=[];window.__reviewReplay='';
      const real=document.fonts;window.__realFonts=real;let resolve,reject;const ready=new Promise((a,b)=>{resolve=a;reject=b;});
      window.__resolveFonts=()=>resolve();window.__rejectFonts=()=>reject(new Error('test font failure'));
      Object.defineProperty(document,'fonts',{configurable:true,value:{get ready(){const result=ready.then(()=>real.ready);result.catch(()=>{});return result;},get status(){return real.status;},load:q=>real.load(q),check:q=>!options.fontCheckFails&&real.check(q)}});
      let id=0;const frames=new Map(),realFrame=window.requestAnimationFrame.bind(window);
      window.requestAnimationFrame=fn=>{frames.set(++id,fn);return id;};window.cancelAnimationFrame=n=>frames.delete(n);
      window.__frame=()=>new Promise(done=>realFrame(t=>{const batch=Array.from(frames);frames.clear();batch.forEach(([,fn])=>fn(t));done();}));
      window.__frameCount=()=>frames.size;
    },options);
    await page.goto('http://127.0.0.1:'+server.address().port+'/CBS_2026/scorebug.html');
    await until(page,()=>!!window.NFL2K5);
    if(!await page.evaluate(()=>!!window.CBSReviewCard))await page.addScriptTag({path:path.join(root,'CBS_2026/review-card.js')});
    await page.addStyleTag({path:path.join(root,'CBS_2026/review-card.css')});
    await page.evaluate(async s=>{window.__reviewShowTimes=[];window.__reviewEntranceTick=0;const root=document.getElementById('cbs-review-card');new MutationObserver(()=>{if(root.classList.contains('on')&&!__reviewEntranceTick)__reviewEntranceTick=performance.now();}).observe(root,{attributes:true,attributeFilter:['class']});NFL2K5.log=text=>{__reviewMessages.push(text);if(text.startsWith('NFL2K5_REVIEW_SHOW:'))__reviewShowTimes.push(performance.now());};await __realFonts.ready;const original=window.CBSNativeReplay||{};window.CBSNativeReplay=Object.assign({},original,{snapshot:()=>({token:__reviewReplay})});NFL2K5.dispatch({type:'state',state:s});CBSReviewCard.updateState(s);},state);
    await page.evaluate(()=>__realFonts.ready);return page;
  }
  async function event(p,e){await p.evaluate(e=>CBSReviewCard.handleEvent(e),e);}
  async function update(p,change){await p.evaluate(change=>{const next=JSON.parse(JSON.stringify(NFL2K5.state));Object.assign(next,change);NFL2K5.dispatch({type:'state',state:next});CBSReviewCard.updateState(next);},change);}
  async function frame(p){await p.evaluate(()=>__frame());}
  async function paint(p){await frame(p);await frame(p);}
  async function snapshot(p){return p.evaluate(()=>{const root=document.getElementById('cbs-review-card'),r=root.getBoundingClientRect(),bug=document.getElementById('eyebar').getBoundingClientRect();return {owner:CBSReviewCard.snapshot(),messages:__reviewMessages.slice(),hidden:root.hidden,motion:root.getAttribute('data-motion'),opacity:getComputedStyle(root).opacity,heading:root.querySelector('.cbs-review-heading span').textContent,message:root.querySelector('.cbs-review-message').textContent,reason:root.querySelector('.cbs-review-reason').textContent,rect:{left:r.left,top:r.top,right:r.right,bottom:r.bottom,width:r.width,height:r.height},bug:{left:bug.left,top:bug.top,right:bug.right,bottom:bug.bottom}};});}
  async function finishEntrance(p){
    await until(p,()=>__frameCount()>0);
    for(let attempt=0;attempt<12;attempt++){await frame(p);if((await snapshot(p)).owner.phase==='live')return;await delay(20);}
    throw new Error('Review card did not finish visible entrance: '+JSON.stringify(await snapshot(p)));
  }
  async function reveal(p,e=show(),options={}){await event(p,e);await p.evaluate(()=>__resolveFonts());await until(p,()=>CBSReviewCard.snapshot().phase==='entering');if(!options.reduced)await delay(225);else await delay(10);await finishEntrance(p);}
  try{
    let p=await fixture();await event(p,show());let s=await snapshot(p);
    equal(s.owner.active,true,'Native SHOW claims copied token');equal(s.hidden,true,'Unresolved assets/fonts preserve native fallback');equal(s.messages,[],'Unresolved fonts cannot acknowledge');
    await p.evaluate(()=>__resolveFonts());await until(p,()=>CBSReviewCard.snapshot().phase==='entering');
    equal((await snapshot(p)).messages.filter(m=>m.startsWith('NFL2K5_REVIEW_SHOW:')).length,0,'Asset readiness alone cannot acquire native draw ownership');
    equal((await snapshot(p)).owner.acknowledged,false,'Entrance cannot acknowledge before visible paint');
    await delay(225);await until(p,()=>__frameCount()>0);await frame(p);equal((await snapshot(p)).owner.acknowledged,false,'First entrance-complete frame does not acknowledge');
    await frame(p);s=await snapshot(p);equal(s.owner.acknowledged,true,'Second visible frame after entrance acknowledges');
    yes(await p.evaluate(()=>__reviewShowTimes[0]-__reviewEntranceTick>=195),'Measured acknowledgment follows complete 200ms entrance (within browser clock/observer precision)');
    equal(s.messages.filter(m=>m==='NFL2K5_REVIEW_SHOW:17').length,1,'Exactly one painted SHOW acknowledgment');
    equal(s.heading,'Challenge','Actual native heading preserved');equal(s.message,show().message,'Actual native body preserved');equal(s.reason,show().reason,'Actual native reason preserved');
    yes(s.rect.bottom<s.bug.top,'Card is above Eyebar without overlap');yes(s.rect.left>=0&&s.rect.top>=0&&s.rect.right<=1920&&s.rect.bottom<=1080,'Entire readable card lies in broadcast frame');
    await p.screenshot({path:path.join(output,'challenge-review.png'),omitBackground:true});
    await delay(6100);s=await snapshot(p);yes(s.owner.active&&s.owner.acknowledged&&!s.hidden,'Native review remains present beyond six seconds without invented timeout');
    await event(p,show());equal((await snapshot(p)).messages.filter(m=>m==='NFL2K5_REVIEW_SHOW:17').length,1,'Duplicate SHOW does not replay entrance or acknowledgment');
    await event(p,hide('18'));yes((await snapshot(p)).owner.active,'Stale HIDE cannot release current review');
    await event(p,hide('17',{nativeVisible:true}));yes((await snapshot(p)).owner.active,'Malformed visible HIDE cannot release current review');
    await event(p,hide());s=await snapshot(p);equal(s.owner.active,false,'Exact native HIDE releases owner');equal(s.hidden,true,'Exact native HIDE removes card');equal(s.messages.filter(m=>m==='NFL2K5_REVIEW_HIDE:17').length,1,'Release reports exact HIDE once');await event(p,hide());equal((await snapshot(p)).messages.filter(m=>m==='NFL2K5_REVIEW_HIDE:17').length,1,'Duplicate HIDE is idempotent');await p.close();

    p=await fixture();await reveal(p,show('4294967295',{heading:'Challenge',reviewStage:'ruling',message:'The ruling on the field stands.',reason:'Dallas is charged a timeout.'}));s=await snapshot(p);equal(s.owner.reviewStage,'ruling','Ruling stage uses explicit native stage');equal(s.message,'The ruling on the field stands.','Native ruling text remains verbatim');await p.screenshot({path:path.join(output,'challenge-ruling.png'),omitBackground:true});
    await event(p,show('19',{message:'The ruling on the field is reversed.',reason:'<script>alert("not executable")</script>\nNative text & punctuation.'}));await until(p,()=>CBSReviewCard.snapshot().phase==='entering');await delay(225);await paint(p);s=await snapshot(p);
    equal(s.owner.token,'19','New token replaces old native instance');equal(s.messages.filter(m=>m==='NFL2K5_REVIEW_HIDE:4294967295').length,1,'Replacement releases old token exactly once');
    equal(s.reason,'<script>alert("not executable")</script>\nNative text & punctuation.','Native reason is preserved as full literal text');equal(await p.evaluate(()=>document.querySelector('#cbs-review-card .cbs-review-reason script')),null,'Native strings never become executable markup');
    await event(p,hide('4294967295'));yes((await snapshot(p)).owner.active,'Late old-token HIDE cannot close replacement');await p.close();

    p=await fixture();await p.evaluate(()=>__resolveFonts());
    for(const token of ['0','01','+1','-1','4294967296','1.0',17,null,'1e2',' 17','17 ']){await event(p,show(token));equal((await snapshot(p)).owner.active,false,'Reject malformed native token '+JSON.stringify(token));}
    for(const extra of [{source:'fixture'},{nativeVisible:false},{heading:''},{heading:17},{message:17},{reason:17},{reviewStage:'unknown'}]){await event(p,show('17',extra));equal((await snapshot(p)).owner.active,false,'Reject malformed native notice');}await p.close();
    for(const [label,change] of [['pause',{context:{paused:true}}],['play selection',{context:{playSelection:true}}],['invalid match',{valid:false}],['inactive',{gameStatus:'inactive'}],['match identity',{matchId:'review-test-B'}],['team identity',{home:{abbreviation:'BUF'}}]]){
      p=await fixture();await event(p,show());await update(p,change);await p.evaluate(()=>__resolveFonts());await paint(p);s=await snapshot(p);
      equal(s.owner.active,false,label+' cancels owner');equal(s.hidden,true,label+' hides review');equal(s.messages.filter(m=>m==='NFL2K5_REVIEW_SHOW:17').length,0,label+' cancels pending paint ack');equal(s.messages.filter(m=>m==='NFL2K5_REVIEW_HIDE:17').length,1,label+' reports release');await p.close();
    }
    p=await fixture();await p.evaluate(()=>__reviewReplay='77');await event(p,show());s=await snapshot(p);
    equal(s.owner.deferred,true,'Active native replay defers review');equal(s.hidden,true,'Deferred review does not cover replay');equal(s.messages,[],'Deferred review does not acknowledge unseen art');
    await p.evaluate(()=>{__reviewReplay='';__resolveFonts();});await update(p,{});await until(p,()=>CBSReviewCard.snapshot().phase==='entering');await delay(225);await paint(p);s=await snapshot(p);
    yes(s.owner.acknowledged&&!s.hidden,'Pending native review paints after replay ends');
    await p.evaluate(()=>__reviewReplay='78');await update(p,{});s=await snapshot(p);yes(s.owner.active&&s.owner.deferred&&!s.owner.acknowledged&&s.hidden,'New replay revokes painted acknowledgment but retains pending native instance');equal(s.messages.filter(m=>m==='NFL2K5_REVIEW_HIDE:17').length,1,'Deferral revokes native draw suppression once');
    await p.evaluate(()=>__reviewReplay='');await update(p,{});await until(p,()=>CBSReviewCard.snapshot().phase==='entering');await delay(225);await paint(p);equal((await snapshot(p)).messages.filter(m=>m==='NFL2K5_REVIEW_SHOW:17').length,2,'Exact token can repaint after replay without invented native event');await p.close();
    p=await fixture();await p.evaluate(()=>__reviewReplay='77');await event(p,show());await event(p,hide());await p.evaluate(()=>{__reviewReplay='';__resolveFonts();});await update(p,{});await paint(p);equal((await snapshot(p)).owner.active,false,'Native HIDE while deferred cannot resurrect after replay');await p.close();
    p=await fixture();await event(p,show());await event(p,hide());await p.evaluate(()=>__resolveFonts());await delay(250);await paint(p);equal((await snapshot(p)).messages.filter(m=>m==='NFL2K5_REVIEW_SHOW:17').length,0,'Closed asset/paint promise cannot acknowledge late');await p.close();
    for(const options of [{brokenAsset:true},{fontCheckFails:true}]){p=await fixture(options);await event(p,show());await p.evaluate(()=>__resolveFonts());await until(p,()=>!CBSReviewCard.snapshot().active);s=await snapshot(p);equal(s.owner.acknowledged,false,'Failed asset/font retains native artwork');yes(s.messages.some(m=>m.startsWith('CBS_REVIEW_FALLBACK:')),'Failed asset/font is diagnosable');await p.close();}
    p=await fixture();await event(p,show());await p.evaluate(()=>__rejectFonts());await until(p,()=>!CBSReviewCard.snapshot().active);equal((await snapshot(p)).messages.filter(m=>m==='NFL2K5_REVIEW_SHOW:17').length,0,'Rejected fonts never acknowledge native replacement');await p.close();
    p=await fixture({host:'browser'});await reveal(p);s=await snapshot(p);equal(s.owner.acknowledged,false,'Standalone browser preview never claims native replacement');equal(s.messages.filter(m=>m.startsWith('NFL2K5_REVIEW_')).length,0,'Standalone browser preview sends no native protocol');await event(p,hide());equal((await snapshot(p)).messages.filter(m=>m.startsWith('NFL2K5_REVIEW_')).length,0,'Standalone release sends no native protocol');await p.close();
    p=await fixture({reduced:true});await update(p,{context:{scorebugVisible:true,animationsEnabled:false}});await reveal(p,show(),{reduced:true});equal((await snapshot(p)).motion,'reduced','Explicit game Animations OFF shows static readable notice');yes((await snapshot(p)).owner.acknowledged,'Static reduced-motion notice can acknowledge painted text');await p.close();
    p=await fixture({reduced:true});await update(p,{context:{scorebugVisible:true,animationsEnabled:true}});await reveal(p);equal((await snapshot(p)).motion,'full','Explicit game Animations ON overrides reduced-motion OS for notice');await p.close();
    equal(errors,[],'No browser script errors');fs.writeFileSync(path.join(output,'results.json'),JSON.stringify({checks,scope:'Real Edge component tests; native fixtures are illustrative, not a captured game review or measured CBS challenge graphic.'},null,2)+'\n');console.log('PASS '+checks+' CBS review-card assertions in real Chromium/Edge.');
  }finally{await browser.close();server.close();}
}
main().catch(error=>{server.close();console.error(error.stack||error);process.exitCode=1;});
