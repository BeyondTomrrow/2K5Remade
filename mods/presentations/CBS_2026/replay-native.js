/* Token-bound adapter for the title's actual automatic replay screen.
 * Standalone preview wipes remain in replay-wipe.js. The guest retains its
 * camera/actor/cleanup calls; this page acknowledges painted visual frames.
 * No prerecorded video and no guest memory/callback access from JavaScript. */
(function () {
  'use strict';
  var api=window.NFL2K5,latest=null,owner='',token='',direction='',version=0;
  var raf=0,watchdog=0,readySent=false,starting=false,started=false,covered=false,handed=false,done=false;
  var startTime=0,returnTime=0;
  function key(s){return s&&s.away&&s.home?String(s.matchId||'')+'|'+String(s.away.abbreviation||'')+'|'+String(s.home.abbreviation||''):'';}
  function allowed(s){var c=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&c.replayActive===true&&c.animationsEnabled!==false&&!c.paused&&!c.playSelection);}
  function id(e){var t=e&&e.replayToken;return typeof t==='string'&&/^[1-9][0-9]{0,9}$/.test(t)&&Number(t)<=4294967295?t:'';}
  function send(action,t){if(api&&api.host==='game'&&t)api.log('NFL2K5_REPLAY_'+action+':'+t);}
  function wipe(){return window.CBSReplayWipe;}
  function cancel(notify){
    var old=token;version++;cancelAnimationFrame(raf);clearTimeout(watchdog);raf=watchdog=0;
    token=direction=owner='';readySent=starting=started=covered=handed=done=false;
    if(wipe())wipe().cancel();if(notify&&old)send('CANCEL',old);
  }
  function current(v,t){return version===v&&token===t&&allowed(latest)&&key(latest)===owner;}
  function painted(v,t,fn){requestAnimationFrame(function(){requestAnimationFrame(function(){if(current(v,t))fn();});});}
  function visible(){
    var r=document.getElementById('cbs-replay-wipe');if(!r||r.hidden||!r.isConnected)return false;
    var b=r.getBoundingClientRect();if(!(b.width>0&&b.height>0))return false;
    for(var n=r;n&&n.nodeType===1;n=n.parentElement){var s=getComputedStyle(n);if(n.hidden||s.display==='none'||s.visibility==='hidden'||Number(s.opacity)===0)return false;}
    return true;
  }
  function fail(v,t,reason){if(current(v,t)){if(api)api.log('CBS_REPLAY_FALLBACK:'+String(reason||'unavailable'));cancel(true);}}
  function ready(v,t){
    if(!document.fonts||!document.fonts.ready||!wipe()||!wipe().beginControlled||!wipe().seekControlled){fail(v,t,'controlled-API-or-fonts-missing');return;}
    var images=['assets/branding/nfl-shield.svg','assets/branding/cbs-sports.svg'].map(function(src){return new Promise(function(resolve,reject){var i=new Image();i.onload=function(){if(i.naturalWidth>0)resolve();else reject(new Error(src));};i.onerror=function(){reject(new Error(src));};i.src=src;});});
    Promise.all([document.fonts.ready].concat(images)).then(function(){
      if(!current(v,t)||document.fonts.status!=='loaded')return;
      readySent=true;send('READY',t);
    }).catch(function(error){fail(v,t,'asset:'+String(error&&error.message||error));});
  }
  function frame(now){
    var v=version,t=token;if(!current(v,t)||!started){cancel(true);return;}
    var cover=direction==='in'?880:40;
    var at=handed?Math.min(1050,cover+now-returnTime):Math.min(cover,now-startTime);
    if(!wipe().seekControlled(at)||!visible()){cancel(true);return;}
    if(!covered&&at===cover){
      covered=true;
      painted(v,t,function(){if(visible())send('COVER',t);else fail(v,t);});
    }
    if(handed&&at>=1050){
      // Paint the completed transparent frame before allowing native end.
      done=true;painted(v,t,function(){send('DONE',t);});return;
    }
    raf=requestAnimationFrame(frame);
  }
  function handleEvent(e){
    // Popup-title heuristics may echo the same replay. The token-bound
    // native sequence owns its motion and must not be restarted by them.
    if(e&&token&&(e.name==='REPLAY_STARTED'||e.name==='REPLAY_ENDED'))return true;
    if(!e||String(e.name).indexOf('NATIVE_REPLAY_')!==0)return false;
    var t=id(e);if(!t||e.source!=='native_automatic_replay')return true;
    if(e.name==='NATIVE_REPLAY_CANCEL'){if(token===t)cancel(false);return true;}
    if(e.name==='NATIVE_REPLAY_PREPARE'){
      if(!allowed(latest)||(e.direction!=='in'&&e.direction!=='out'))return true;
      if(token===t&&owner===key(latest))return true;
      cancel(true);token=t;direction=e.direction;owner=key(latest);var v=version;
      if(window.CBSMotion)CBSMotion.cancel('native-replay');if(window.CBSStats)CBSStats.cancel();
      var reduced=window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches&&document.documentElement.getAttribute('data-preview-motion')!=='full';
      if(reduced){fail(v,t,'reduced-motion');return true;}
      watchdog=setTimeout(function(){if(current(v,t))cancel(true);},4200);
      ready(v,t);return true;
    }
    if(t!==token||e.direction!==direction||!current(version,t))return true;
    if(e.name==='NATIVE_REPLAY_START'&&readySent&&!starting&&!started){
      starting=true;
      if(!wipe().beginControlled(direction)||!visible()){cancel(true);return true;}
      var v=version;
      painted(v,t,function(){
        if(!visible()){fail(v,t);return;}
        send('START',t);starting=false;started=true;startTime=performance.now();raf=requestAnimationFrame(frame);
      });return true;
    }
    if(e.name==='NATIVE_REPLAY_HANDOFF'&&covered&&!handed){handed=true;returnTime=performance.now();return true;}
    return true;
  }
  function updateState(s){latest=s;if(token&&(!allowed(s)||key(s)!==owner))cancel(true);}
  window.CBSNativeReplay={handleEvent:handleEvent,updateState:updateState,cancel:function(){cancel(true);},
    snapshot:function(){return {token:token,direction:direction,started:started,covered:covered,handed:handed,done:done};}};
})();
