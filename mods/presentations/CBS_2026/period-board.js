/* Own the real native end-of-quarter board, independently of the later
 * QUARTER_ENDED state delta. The host keeps ESPN art unless this exact token
 * has painted. No guest memory, clock, camera or football state is changed. */
(function () {
  'use strict';
  var bug=document.getElementById('eyebar'),latest=null,owner='',token='',quarter=0,generation=0,acknowledged=false,ackPending=false,presentation=null;
  if(!bug)return;
  function key(s){return s&&s.away&&s.home?String(s.matchId||'')+'|'+String(s.away.abbreviation||'')+'|'+String(s.home.abbreviation||''):'';}
  function allowed(s,p){var c=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&(s.gameStatus!=='final'||p&&p.presentationKind==='final')&&!c.paused&&!c.playSelection);}
  function nativeToken(e){var t=e&&e.nativePeriodToken;return typeof t==='string'&&/^[1-9][0-9]{0,9}$/.test(t)&&Number(t)<=4294967295?t:'';}
  function message(action,t){if(t&&window.NFL2K5&&NFL2K5.host==='game')NFL2K5.log('NFL2K5_PERIOD_BOARD_'+action+':'+t);}
  function refresh(){if(window.CBSScorebug&&CBSScorebug.refresh)CBSScorebug.refresh();}
  function release(render){
    if(!token)return;
    var old=token;generation++;token='';quarter=0;owner='';acknowledged=false;ackPending=false;presentation=null;
    message('HIDE',old);if(render)refresh();
  }
  function owned(){return !!(token&&allowed(latest,presentation)&&key(latest)===owner);}
  /* The native formatter's literal branch is authoritative. In particular,
     '4th' does not become Final and a rounded zero clock does not mean HALF.
     Missing fields remain compatible only with the old Q1/Q3 producer. */
  function parsePresentation(e){
    var q=e.quarter,kind=e.presentationKind,raw=e.nativeCaption;
    if(!Number.isInteger(q)||q<1||q>255)return null;
    if(kind===undefined&&raw===undefined){if(q!==1&&q!==3)return null;kind='quarter_end';raw=q===1?'1st':'3rd';}
    if(typeof kind!=='string'||typeof raw!=='string')return null;
    var caption='';
    if(kind==='quarter_end'&&q<=4&&raw===['','1st','2nd','3rd','4th'][q])caption='END '+['','1ST','2ND','3RD','4TH'][q]+' QTR';
    else if(kind==='halftime'&&q===2&&raw==='HALF')caption='HALFTIME';
    else if(kind==='final'&&raw==='Final')caption='FINAL';
    else if(kind==='regulation_end'&&q===4&&raw==='4th')caption='END 4TH QTR';
    else if(kind==='overtime'&&q>=5&&raw===(q===5?'OT':'OT'+(q-4)))caption='END '+raw;
    return caption?{quarter:q,nativeCaption:raw,presentationKind:kind,displayCaption:caption}:null;
  }
  function painted(element){
    if(!element||!element.isConnected)return false;
    var r=element.getBoundingClientRect();
    if(!(r.width>0&&r.height>0&&r.right>0&&r.bottom>0&&r.left<window.innerWidth&&r.top<window.innerHeight))return false;
    for(var n=element;n&&n.nodeType===1;n=n.parentElement){
      var style=getComputedStyle(n);
      if(n.hidden||style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse'||Number(style.opacity)<.999)return false;
    }
    return true;
  }
  function acknowledge(version,t){
    if(ackPending||!window.NFL2K5||NFL2K5.host!=='game'||!document.fonts||!document.fonts.ready)return;
    ackPending=true;
    Promise.resolve(document.fonts.ready).then(function(){
      requestAnimationFrame(function(){requestAnimationFrame(function(){
        if(version!==generation||token!==t)return;
        ackPending=false;
        if(acknowledged||!owned()||bug.classList.contains('hidden')||bug.classList.contains('entering'))return;
        // FontFaceSet.ready also resolves after failed downloads. A failed
        // score face must retain the native board instead of acknowledging.
        if(document.fonts.status!=='loaded'||!document.fonts.check('900 91px "CBS Preview Heavy"'))return;
        var label=document.getElementById('quarter'),brand=document.getElementById('center-brand');
        if(!label||!presentation||label.textContent.replace(/\s+/g,' ').trim()!==presentation.displayCaption||
           !brand||!brand.complete||!brand.naturalWidth||!painted(bug.querySelector('.live-plate'))||!painted(label)||!painted(brand))return;
        acknowledged=true;message('SHOW',t);
      });});
    }).catch(function(){if(version===generation&&token===t)ackPending=false;/* Keep the original board when font readiness fails. */});
  }
  function updateState(s){
    latest=s;
    if(token&&(!allowed(s,presentation)||key(s)!==owner))release(false);
    else if(token&&!acknowledged)acknowledge(generation,token);
  }
  function handleEvent(e){
    if(!e)return false;
    if(e.name==='PERIOD_BOARD_HIDE'){
      if(e.source==='native'&&nativeToken(e)===token)release(true);
      return true;
    }
    if(e.name==='PERIOD_BOARD_SHOW'){
      var next=nativeToken(e),info=parsePresentation(e);
      if(!next||e.source!=='native'||e.nativeVisible!==true||!info||!allowed(latest,info))return true;
      if(token===next&&owner===key(latest)&&JSON.stringify(presentation)===JSON.stringify(info))return true;
      release(false);generation++;token=next;quarter=e.quarter;presentation=info;owner=key(latest);acknowledged=false;ackPending=false;
      if(window.CBSStats)CBSStats.cancel();
      if(window.CBSMotion)CBSMotion.cancel('native-period-board');
      if(window.CBSScorebug){CBSScorebug.reset();CBSScorebug.stopEntry();}
      refresh();acknowledge(generation,next);return true;
    }
    // The real popup owns this interval. A later period-number delta or
    // queued stat insert must not start a second graphic over the board.
    return owned();
  }
  window.CBSPeriodBoard={updateState:updateState,handleEvent:handleEvent,
    activeQuarter:function(){return owned()?quarter:0;},
    presentation:function(){return owned()?Object.assign({},presentation):null;},
    displayCaption:function(){return owned()?presentation.displayCaption:'';},
    snapshot:function(){return {active:owned(),quarter:owned()?quarter:0,token:token,match:owner,acknowledged:acknowledged,presentation:owned()?Object.assign({},presentation):null,displayCaption:owned()?presentation.displayCaption:''};}};
})();
