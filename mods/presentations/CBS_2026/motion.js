/* CBS motion reconstructed from inspected broadcast clips. No video overlay.
   Unverified full-screen/period treatments report unavailable instead of
   borrowing another network's wipe. See docs/CBS-2026-MOTION.md. */
(function () {
  'use strict';
  var root=document.getElementById('cbs-motion'),bug=document.getElementById('eyebar');
  if(!root||!bug)return;
  var latest=null,key='',active='',timers=[],generation=0,holds={},seen={},phase='idle';
  var refs={entry:'CBS PIT/NYJ September 2025 entry GIF',touchdown:'CBS LV/NE September 7 2025, clip 15.4–22.2s',score:'CBS LV/NE September 7 2025, clip 21.0–22.2s'};
  root.innerHTML='<div id="cbs-td" aria-hidden="true">'+
    '<div class="td-chevrons left"><i></i><i></i><i></i></div><div class="td-chevrons right"><i></i><i></i><i></i></div>'+
    '<img class="td-logo" alt="">'+
    '<svg class="td-word" viewBox="0 0 1920 1080" aria-hidden="true"><defs><linearGradient id="td-glass" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".42"/><stop offset=".44" stop-color="#d9e5ef" stop-opacity=".07"/><stop offset="1" stop-color="#eff9ff" stop-opacity=".34"/></linearGradient></defs><text class="td-depth" x="960" y="1035" text-anchor="middle" textLength="1770" lengthAdjust="spacingAndGlyphs">TOUCHDOWN</text><text class="td-face" x="960" y="1031" text-anchor="middle" textLength="1770" lengthAdjust="spacingAndGlyphs">TOUCHDOWN</text><text class="td-edge" x="960" y="1028" text-anchor="middle" textLength="1755" lengthAdjust="spacingAndGlyphs">TOUCHDOWN</text></svg>'+
    '<div class="td-player" hidden><img class="td-portrait" alt=""><div class="td-name"></div></div><div class="td-rule"></div></div>';
  var td=document.getElementById('cbs-td'),logo=td.querySelector('.td-logo'),player=td.querySelector('.td-player'),portrait=td.querySelector('.td-portrait'),name=td.querySelector('.td-name');
  function reduced(){return window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches&&document.documentElement.getAttribute('data-preview-motion')!=='full';}
  function number(v){return (typeof v==='number'||typeof v==='string'&&v.trim()!=='')&&Number.isFinite(Number(v))&&Math.floor(Number(v))===Number(v)&&Number(v)>=0?Number(v):null;}
  function allowed(s){var c=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&!c.paused&&!c.playSelection);}
  function report(n,p,status,reference){
    phase=p;
    root.setAttribute('data-motion',n||'none');root.setAttribute('data-phase',p);
    if(window.parent!==window)window.parent.postMessage({cbsMotion:{name:n||'none',phase:p,status:status||'running',reference:reference||''}},'*');
  }
  function refresh(){
    ['away','home'].forEach(function(side){bug.classList.toggle('points-'+side,!!(holds[side]&&holds[side].stage==='points'));});
    bug.classList.toggle('score-points',!!(holds.away&&holds.away.stage==='points'||holds.home&&holds.home.stage==='points'));
    window.dispatchEvent(new CustomEvent('cbs-motion-change'));
  }
  function later(ms,fn){var token=generation;timers.push(setTimeout(function(){if(token===generation)fn();},ms));}
  function stop(reason){
    generation++;timers.forEach(clearTimeout);timers=[];active='';holds={};
    if(window.CBSScorebug)CBSScorebug.stopEntry();
    td.className='';td.setAttribute('aria-hidden','true');player.hidden=true;portrait.removeAttribute('src');
    bug.classList.remove('motion-contract','motion-covered','motion-return','score-points');
    if(window.CBSWipe)window.CBSWipe.cancel();
    if(window.CBSReplayWipe)window.CBSReplayWipe.cancel();
    if(window.CBSStats)window.CBSStats.cancel();
    report('',reason||'idle','idle');refresh();
  }
  function dedupe(e,team){
    var signature=e.name+'|'+team+'|'+String(latest&&latest[team]&&latest[team].score)+'|'+String(e.quarter||'');
    var now=Date.now(),old=seen[signature];seen[signature]=now;return old!=null&&now-old<1500;
  }
  function numericReturn(side,delay){
    later(delay,function(){
      if(!holds[side])return;
      var owner=holds[side];owner.stage='points';refresh();report('score-update','points','running',refs.score);
      later(850,function(){if(holds[side]!==owner||owner.stage!=='points')return;delete holds[side];refresh();report('score-update','settled','complete',refs.score);});
    });
  }
  function touchdown(e){
    var side=e.team;if(side!=='away'&&side!=='home')return false;
    if(dedupe(e,side))return true;
    var pending=holds[side];if(active||window.CBSWipe&&CBSWipe.isCovering()||window.CBSReplayWipe&&CBSReplayWipe.isCovering()){stop('interrupted');if(pending)holds[side]=pending;}
    active='touchdown';var team=latest[side],source=bug.querySelector('.team.'+side+' .logo');
    if(window.CBSScorebug)CBSScorebug.stopEntry();
    logo.src=source&&source.getAttribute('src')||team.logo||'';logo.onerror=function(){logo.removeAttribute('src');};
    var p=e.player||null,fullName=p&&(p.displayName||p.name)||'';
    player.hidden=!fullName;
    if(fullName){name.textContent=(p.position?p.position+' - ':'')+String(fullName).toUpperCase();
      if(p.portrait){portrait.src=p.portrait;portrait.hidden=false;portrait.onerror=function(){portrait.hidden=true;};}else{portrait.hidden=true;portrait.removeAttribute('src');}}
    var from=number(e.previousScore),to=number(team.score),h=holds[side];
    if(!h&&from!=null&&to!=null&&to>from)holds[side]={from:from,to:to,points:to-from,stage:'hold'};
    if(holds[side])holds[side].stage='hold';
    bug.classList.remove('entering');bug.classList.add('motion-contract');
    td.setAttribute('aria-hidden','false');td.className=reduced()?'reduced running':'running';
    report('touchdown','contract','running',refs.touchdown);refresh();
    var short=reduced();
    later(short?0:300,function(){bug.classList.add('motion-covered');report('touchdown','logo','running',refs.touchdown);});
    later(short?0:600,function(){report('touchdown','title','running',refs.touchdown);});
    later(short?0:1200,function(){report('touchdown','hold','running',refs.touchdown);});
    later(short?1100:4530,function(){td.classList.add('leaving');report('touchdown','exit','running',refs.touchdown);});
    later(short?1300:5170,function(){bug.classList.remove('motion-covered','motion-contract');bug.classList.add('motion-return');td.className='';td.setAttribute('aria-hidden','true');refresh();report('touchdown','return','running',refs.touchdown);});
    numericReturn(side,short?1300:5610);
    later(short?2300:6600,function(){active='';bug.classList.remove('motion-return');report('touchdown','settled','complete',refs.touchdown);refresh();});
    return true;
  }
  function updateState(s,prev){
    var next=s.away&&s.home?String(s.matchId||'')+'|'+String(s.away.abbreviation)+'|'+String(s.home.abbreviation):'';
    var changed=key&&next&&key!==next;latest=s;
    if(changed){seen={};stop('match-changed');}
    if(next)key=next;
    if(!allowed(s)){stop('suppressed');return;}
    ['away','home'].forEach(function(side){
      var a=number(prev&&prev[side]&&prev[side].score),b=number(s[side]&&s[side].score);
      if(!changed&&a!=null&&b!=null&&b>a&&!holds[side]){
        if(window.CBSScorebug)CBSScorebug.stopEntry();
        var owner=holds[side]={from:a,to:b,points:b-a,stage:'pending'};
        later(550,function(){if(holds[side]===owner&&owner.stage==='pending'){owner.stage='points';refresh();report('score-update','points','running',refs.score);later(850,function(){if(holds[side]!==owner||owner.stage!=='points')return;delete holds[side];refresh();report('score-update','settled','complete',refs.score);});}});
      }else if(holds[side]&&b!=null){holds[side].to=b;holds[side].points=b-holds[side].from;if(holds[side].points<=0)delete holds[side];}
    });
  }
  function handleEvent(e){
    if(!e)return false;
    if(!allowed(latest)){report(e.name,'suppressed','suppressed');return true;}
    if(e.name==='TOUCHDOWN')return touchdown(e);
    if(e.name==='SCORE_CHANGED')return true; // the authoritative state delta owns the number
    if(e.name==='PLAY_STARTED'){stop('play-started');return false;}
    if((e.name==='REPLAY_STARTED'||e.name==='REPLAY_ENDED')&&window.CBSReplayWipe){stop('replay-transition');return CBSReplayWipe.handleEvent(e,latest);}
    if(e.name==='CBS_REFERENCE_WIPE'||e.name==='CBS_HALFTIME_REPORT_READY'){
      if(window.CBSWipe){stop('transition');return CBSWipe.handleEvent(e,latest);}
      report(e.name,'reference-needed','unavailable');return true;
    }
    if(e.name==='TIMEOUT'||e.name==='TWO_MINUTE_WARNING'){
      report(e.name,'state-only','unavailable','Live timeout marks remain data-driven; dedicated motion reference not yet inspected');return true;
    }
    if(e.name==='QUARTER_ENDED'&&latest.context&&latest.context.periodEnding===true&&(latest.quarter===1||latest.quarter===3)){
      report(e.name,'eyebar-period-label','complete','CBS KC/BUF 2025, 1321.5–1325.0s; period-ending state retains scores and timeouts');return true;
    }
    if(e.name==='REPLAY_STARTED'||e.name==='REPLAY_ENDED'||e.name==='QUARTER_ENDED'||e.name==='HALFTIME'||e.name==='OVERTIME'){
      if(window.CBSWipe&&window.CBSWipe.handleEvent(e,latest))return true;
      report(e.name,'reference-needed','unavailable','No inspected current CBS motion for this treatment');return true;
    }
    return false;
  }
  window.CBSMotion={updateState:updateState,handleEvent:handleEvent,cancel:stop,
    scoreText:function(side,fallback){var h=holds[side];return h?(h.stage==='points'?'+'+h.points:String(h.from)):fallback;},
    snapshot:function(){return {name:active||(window.CBSWipe&&CBSWipe.isCovering()?'halftime-report-exit':window.CBSReplayWipe&&CBSReplayWipe.isCovering()?'replay-wipe':''),phase:phase,allowed:allowed(latest)};},report:report};
})();
