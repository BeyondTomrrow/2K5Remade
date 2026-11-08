/* Real Chromium/Edge test of the shipped page, fonts, CSS and paint gate.
 * No native game, build, state injection into a process or generated edits.
 * Run with the bundled Node runtime: node tools/cbs-scorebug/verify-period-board.js
 * Optional PLAYWRIGHT_BROWSER_PATH overrides the installed browser binary. */
'use strict';
const fs=require('fs'),path=require('path'),http=require('http'),assert=require('assert');
let playwright;
try{playwright=require('playwright');}catch(_){playwright=require(path.resolve(path.dirname(process.execPath),'../node_modules/playwright'));}
const root=path.resolve(__dirname,'../../mods/presentations');
let checks=0;
function equal(a,b,label){assert.deepStrictEqual(a,b,label);checks++;}
function yes(v,label){assert.ok(v,label);checks++;}
const state={valid:true,matchId:'period-test-A',gameStatus:'in_progress',quarter:1,gameClock:0,playClock:-1,phase:4,possession:'away',down:1,distance:10,downDistanceText:'1ST & 10',
  away:{abbreviation:'DAL',city:'Dallas',name:'Cowboys',score:7,timeouts:2,primaryColor:'#002c5f'},
  home:{abbreviation:'NE',city:'New England',name:'Patriots',score:3,timeouts:3,primaryColor:'#002244'},
  context:{scorebugVisible:true,periodEnding:false,paused:false,playSelection:false}};
function show(token='17',extra={}){return Object.assign({name:'PERIOD_BOARD_SHOW',source:'native',nativeVisible:true,nativePeriodToken:token,quarter:1},extra);}
function hide(token='17',extra={}){return Object.assign({name:'PERIOD_BOARD_HIDE',source:'native',nativePeriodToken:token},extra);}
function installedBrowser(){
  if(process.env.PLAYWRIGHT_BROWSER_PATH)return process.env.PLAYWRIGHT_BROWSER_PATH;
  const bundled=playwright.chromium.executablePath();if(fs.existsSync(bundled))return bundled;
  const core=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft/EdgeCore');
  if(fs.existsSync(core)){const versions=fs.readdirSync(core).filter(n=>/^\d+(?:\.\d+)+$/.test(n)).sort((a,b)=>a.localeCompare(b,undefined,{numeric:true})).reverse();
    for(const v of versions){const exe=path.join(core,v,'msedge.exe');if(fs.existsSync(exe))return exe;}}
  throw new Error('No installed Chromium/Edge found. Set PLAYWRIGHT_BROWSER_PATH.');
}
const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.png':'image/png','.woff2':'font/woff2'};
const server=http.createServer((req,res)=>{
  let file;try{file=path.resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://127.0.0.1').pathname));}catch(_){res.writeHead(400);res.end();return;}
  if(!file.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}
  fs.readFile(file,(error,data)=>{if(error){res.writeHead(404);res.end();return;}res.writeHead(200,{'Content-Type':mime[path.extname(file)]||'application/octet-stream'});res.end(data);});
});
async function main(){
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const browser=await playwright.chromium.launch({headless:true,executablePath:installedBrowser()});
  const errors=[];
  async function fixture(options={}){
    const page=await browser.newPage({viewport:{width:1920,height:1080},reducedMotion:'reduce'});
    page.on('pageerror',e=>errors.push(e.message));
    await page.addInitScript(options=>{
      window.NFL2K5_HOST=options.host||'game';window.__periodMessages=[];
      const realFonts=document.fonts;window.__periodRealFonts=realFonts;
      let resolve,reject;const pending=new Promise((a,b)=>{resolve=a;reject=b;});
      window.__periodResolveFonts=()=>resolve();window.__periodRejectFonts=()=>reject(new Error('test font failure'));
      Object.defineProperty(document,'fonts',{configurable:true,value:{ready:pending,get status(){return realFonts.status;},check:q=>!options.fontCheckFails&&realFonts.check(q)}});
      const nativeFrame=window.requestAnimationFrame.bind(window),frames=[];
      window.requestAnimationFrame=fn=>{frames.push(fn);return frames.length;};
      window.__periodFrame=()=>new Promise(done=>nativeFrame(t=>{const batch=frames.splice(0);batch.forEach(fn=>fn(t));done();}));
      window.__periodFrameCount=()=>frames.length;
    },options);
    await page.goto('http://127.0.0.1:'+server.address().port+'/CBS_2026/scorebug.html');
    await page.waitForFunction(()=>window.CBSPeriodBoard&&window.CBSScorebug&&window.NFL2K5.listening);
    await page.evaluate(async s=>{NFL2K5.log=t=>__periodMessages.push(t);await __periodRealFonts.ready;NFL2K5.dispatch({type:'state',state:s});},state);
    // Font downloads begin when the real score text is first populated.
    await page.evaluate(()=>__periodRealFonts.ready);
    return page;
  }
  async function event(p,e){await p.evaluate(e=>NFL2K5.dispatch({type:'event',event:e}),e);}
  async function update(p,change){await p.evaluate(change=>{const next=JSON.parse(JSON.stringify(NFL2K5.state));Object.assign(next,change);NFL2K5.dispatch({type:'state',state:next});},change);}
  async function snap(p){return p.evaluate(()=>({owner:CBSPeriodBoard.snapshot(),label:document.getElementById('quarter').textContent,visible:CBSScorebug.visible(),hidden:document.getElementById('eyebar').classList.contains('hidden'),entering:document.getElementById('eyebar').classList.contains('entering'),messages:__periodMessages.slice(),frames:__periodFrameCount(),stats:CBSStats.snapshot(),motion:CBSMotion.snapshot(),ending:document.getElementById('eyebar').classList.contains('period-ending')}));}
  async function fonts(p){await p.evaluate(()=>__periodResolveFonts());}
  async function frame(p){await p.evaluate(()=>__periodFrame());}
  async function paint(p){await fonts(p);await frame(p);await frame(p);}
  try{
    const manifest=JSON.parse(fs.readFileSync(path.join(root,'CBS_2026/mod.json'),'utf8'));
    yes(manifest.replaces.includes('period_boards')&&manifest.period_boards_ack===true,'Manifest declares token-acknowledged period boards');
    let p=await fixture();
    await event(p,{name:'PLAYER_STAT',team:'away',player:{name:'Q. Carter',position:'QB',completions:1,attempts:2,passingYards:17}});
    equal((await snap(p)).stats.name,'player-stat','Real stat card is active before ownership');
    await event(p,show());let s=await snap(p);
    equal(s.owner.active,true,'Actual native board acquires ownership');equal(s.owner.quarter,1,'Owner keeps event quarter');
    equal(s.stats.name,'','Acquisition cancels competing player card');equal(s.motion.name,'','Acquisition cancels motion');equal(s.entering,false,'Board does not replay normal scorebug entrance');
    equal(s.messages.filter(t=>t.startsWith('NFL2K5_PERIOD_BOARD_SHOW:')).length,0,'No ack with unresolved fonts');equal(s.frames,0,'Unresolved fonts do not schedule paint');
    await fonts(p);equal((await snap(p)).frames,1,'Font readiness schedules first browser frame');
    await frame(p);equal((await snap(p)).messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_SHOW:17').length,0,'First frame is not a paint ack');
    await frame(p);s=await snap(p);equal(s.messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_SHOW:17').length,1,'Second real browser frame acknowledges painted board');
    equal(s.owner.acknowledged,true,'Snapshot reports completed acknowledgment');equal(s.label.replace(/\s+/g,' '),'END 1ST QTR','CBS renders END 1st QTR');
    await update(p,{quarter:2,gameClock:300,context:{scorebugVisible:false,periodEnding:false,paused:false,playSelection:false}});s=await snap(p);
    equal(s.owner.quarter,1,'Period increment cannot mutate owner quarter');equal(s.label.replace(/\s+/g,' '),'END 1ST QTR','Original event label survives period increment');yes(s.visible&&!s.hidden&&s.ending,'Owned board remains painted when native scorebug is off');
    for(const e of [{name:'TEAM_STAT',scope:'this_game',category:'yardage',teams:{away:{totalYards:17},home:{totalYards:0}}},{name:'TOUCHDOWN',team:'home'},{name:'QUARTER_ENDED',quarter:1},{name:'REPLAY_STARTED'}])await event(p,e);
    s=await snap(p);equal(s.stats.name,'','Queued stats stay suppressed during board');equal(s.motion.name,'','Queued scoring/wipe stays suppressed');yes(s.owner.active,'Competing events do not release native ownership');
    await event(p,show());await event(p,hide('18'));s=await snap(p);yes(s.owner.active,'Mismatched hide cannot release current token');equal(s.messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_SHOW:17').length,1,'Duplicate show does not restart ack');
    const output=path.resolve(__dirname,'../../outputs/cbs-2026/period-board-20261018');fs.mkdirSync(output,{recursive:true});
    await p.screenshot({path:path.join(output,'owned-end-q1.png')});
    await event(p,hide());s=await snap(p);equal(s.owner.active,false,'Exact HIDE releases ownership');equal(s.messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_HIDE:17').length,1,'Release logs exact token once');yes(!s.visible&&s.hidden,'Visibility returns to native scorebug context');
    await event(p,hide());equal((await snap(p)).messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_HIDE:17').length,1,'Duplicate hide is idempotent');await p.close();

    p=await fixture();await fonts(p);
    for(const t of ['0','01','+1','-1','4294967296','1.0',17,null,'1e2',' 17','17 ','0000000017']){await event(p,show(t));equal((await snap(p)).owner.active,false,'Reject noncanonical/out-of-range token '+JSON.stringify(t));}
    for(const e of [show('7',{quarter:2}),show('7',{quarter:4}),show('7',{quarter:'1'}),show('7',{source:'fixture'}),show('7',{nativeVisible:false})]){await event(p,e);equal((await snap(p)).owner.active,false,'Reject malformed source/visibility/quarter');}
    equal((await snap(p)).messages.length,0,'Invalid requests never acknowledge');await p.close();

    p=await fixture();await event(p,show('4294967295',{quarter:3}));await paint(p);s=await snap(p);equal(s.label.replace(/\s+/g,' '),'END 3RD QTR','Maximum uint32 token and third-quarter label accepted');yes(s.owner.acknowledged,'Valid max token paints');
    await event(p,show('19',{quarter:1}));await frame(p);await frame(p);s=await snap(p);equal(s.owner.token,'19','New token replaces old owner');equal(s.messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_HIDE:4294967295').length,1,'Replacement releases only old token');
    await event(p,hide('4294967295'));yes((await snap(p)).owner.active,'Late old-token close does not remove replacement');await p.close();

    for(const [label,change] of [['pause',{context:{paused:true}}],['play selection',{context:{playSelection:true}}],['invalid match',{valid:false}],['final',{gameStatus:'final'}],['inactive',{gameStatus:'inactive'}],['match identity',{matchId:'period-test-B'}],['team identity',{home:{abbreviation:'KC',score:3,timeouts:3}}]]){
      p=await fixture();await event(p,show());await update(p,change);await paint(p);s=await snap(p);equal(s.owner.active,false,label+' releases owner');equal(s.messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_SHOW:17').length,0,label+' invalidates pending frame ack');equal(s.messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_HIDE:17').length,1,label+' reports release');await p.close();
    }
    p=await fixture();await event(p,show());await event(p,hide());await paint(p);equal((await snap(p)).messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_SHOW:17').length,0,'Closed request cannot acknowledge from stale RAF');await p.close();
    for(const [label,css] of [['hidden plate','.live-plate{display:none!important}'],['zero rectangle','.live-plate{width:0!important;height:0!important}'],['transparent ancestor','#canvas{opacity:0!important}']]){
      p=await fixture();await p.addStyleTag({content:css});await event(p,show());await paint(p);equal((await snap(p)).messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_SHOW:17').length,0,label+' retains native fallback');await p.close();
    }
    p=await fixture();await event(p,show());await p.evaluate(()=>__periodRejectFonts());await frame(p);await frame(p);equal((await snap(p)).messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_SHOW:17').length,0,'Rejected font readiness cannot ack');await p.close();
    p=await fixture({fontCheckFails:true});await event(p,show());await paint(p);equal((await snap(p)).messages.filter(t=>t==='NFL2K5_PERIOD_BOARD_SHOW:17').length,0,'Resolved but failed FontFaceSet check cannot ack');await p.close();
    p=await fixture({host:'browser'});await event(p,show());await paint(p);s=await snap(p);equal(s.messages.length,0,'Browser preview never sends host ownership messages');equal(s.owner.acknowledged,false,'Preview does not claim host ack');await p.close();
    p=await fixture();await update(p,{context:{scorebugVisible:true,periodEnding:true,paused:false,playSelection:false}});s=await snap(p);equal(s.label.replace(/\s+/g,' '),'END 1ST QTR','Original periodEnding label still works without board event');equal(s.owner.active,false,'State-only ending never claims native board ownership');await p.close();
    const extendedOutput=path.resolve(__dirname,'../../outputs/cbs-2026/period-captions-page-20261020');fs.mkdirSync(extendedOutput,{recursive:true});
    async function settleCaption(page){await fonts(page);await new Promise(resolve=>setTimeout(resolve,310));await frame(page);await frame(page);}
    async function styleSnapshot(page){return page.evaluate(()=>{const bug=document.getElementById('eyebar'),label=document.getElementById('quarter'),range=document.createRange();range.selectNodeContents(label);const text=range.getBoundingClientRect(),box=label.getBoundingClientRect();return {final:bug.classList.contains('final'),ending:bug.classList.contains('period-ending'),fontSize:getComputedStyle(label).fontSize,fontFamily:getComputedStyle(label).fontFamily,clock:getComputedStyle(document.getElementById('game-clock')).display,playClock:getComputedStyle(document.getElementById('play-clock')).display,brand:getComputedStyle(document.getElementById('center-brand')).display,timeouts:Array.from(bug.querySelectorAll('.timeouts')).map(n=>getComputedStyle(n).visibility),textFits:text.left>=box.left-.5&&text.right<=box.right+.5&&text.top>=box.top-.5&&text.bottom<=box.bottom+.5};});}
    p=await fixture();await event(p,show('41',{quarter:2,presentationKind:'halftime',nativeCaption:'HALF'}));await settleCaption(p);s=await snap(p);equal(s.label,'HALFTIME','Real scorebug.js consumes explicit HALF semantic');yes(s.owner.acknowledged,'Real HALF label and branding acquire native paint ownership');let styles=await styleSnapshot(p);yes(styles.ending&&!styles.final&&styles.clock==='none'&&styles.playClock==='none','HALFTIME uses center period styling without live clocks');yes(styles.textFits,'HALFTIME caption fits actual CBS center bounds');await p.screenshot({path:path.join(extendedOutput,'actual-page-halftime-sample.png')});await update(p,{quarter:3,gameClock:900,context:{scorebugVisible:false,periodEnding:false,paused:false,playSelection:false}});s=await snap(p);equal(s.label,'HALFTIME','Real renderer keeps frozen HALF across quarter transition');yes(s.owner.active&&s.visible,'Owned HALF survives disabled normal live bug');await event(p,hide('41'));s=await snap(p);yes(!s.owner.active&&!s.visible,'Native HALF close restores normal visibility lifetime');await p.close();
    p=await fixture();await update(p,{quarter:4,gameClock:0});await event(p,show('42',{quarter:4,presentationKind:'quarter_end',nativeCaption:'4th'}));await settleCaption(p);s=await snap(p);equal(s.label,'END 4TH QTR','Real renderer does not infer FINAL from Q4 zero');yes(s.owner.acknowledged,'Real Q4 literal can paint-ack');styles=await styleSnapshot(p);yes(!styles.final&&styles.textFits,'Q4 nonfinal style remains readable and distinct from final');await p.screenshot({path:path.join(extendedOutput,'actual-page-regulation-sample.png')});await event(p,hide('42'));equal((await snap(p)).owner.active,false,'Q4 native close releases only its instance');await p.close();
    p=await fixture();await event(p,show('43',{quarter:4,presentationKind:'final',nativeCaption:'Final'}));await settleCaption(p);s=await snap(p);equal(s.label,'FINAL','Real explicit Final branch renders before API gameStatus delta');yes(s.owner.acknowledged,'Real explicit Final label can paint-ack');styles=await styleSnapshot(p);yes(styles.final&&styles.ending,'Explicit native Final sets final styling before API state');equal(styles.timeouts,['hidden','hidden'],'Real Final suppresses timeouts before gameStatus delta');yes(styles.clock==='none'&&styles.playClock==='none'&&styles.textFits,'Final+period-ending styles have no live-clock or text clipping overlap');await p.screenshot({path:path.join(extendedOutput,'actual-page-final-sample.png')});await update(p,{gameStatus:'final',quarter:4,gameClock:0});s=await snap(p);yes(s.owner.active&&s.owner.acknowledged&&s.label==='FINAL','Actual final game state does not invalidate native Final owner');await event(p,hide('43'));s=await snap(p);equal(s.owner.active,false,'Native Final close releases exact native ownership');equal(s.label,'FINAL','Real final scoreline remains driven by actual final game state');styles=await styleSnapshot(p);yes(styles.final&&!styles.ending,'Final close clears period style while retaining normal final style');equal(styles.timeouts,['hidden','hidden'],'Final timeouts remain hidden after native owner closes');await p.close();
    for(const [q,raw,label] of [[5,'OT','END OT'],[6,'OT2','END OT2']]){p=await fixture();await event(p,show('44',{quarter:q,presentationKind:'overtime',nativeCaption:raw}));await settleCaption(p);s=await snap(p);equal(s.label,label,'Real renderer consumes exact native overtime caption');yes(s.owner.acknowledged,'Overtime caption can paint-ack with real page');yes((await styleSnapshot(p)).textFits,'Native overtime caption fits actual center');await event(p,hide('44'));equal((await snap(p)).owner.active,false,'Native overtime close releases lifetime');await p.close();}
    equal(errors,[],'No browser script errors');console.log('PASS '+checks+' CBS period-board assertions in real Chromium/Edge.');
  }finally{await browser.close();server.close();}
}
main().catch(error=>{server.close();console.error(error.stack||error);process.exitCode=1;});
