/* Acknowledge the existing painted CBS corner for one native camera-mark
 * instance. This adapter creates no artwork and never labels a camera.
 * The native popup remains the fallback until its exact token has painted.
 */
(function () {
  'use strict';
  var api=window.NFL2K5,network=document.getElementById('network');
  if(!api||!network)return;
  var latest=null,token='',owner='',acknowledged=false,painted=false;
  var frame=0,version=0,paintFrames=0,startedAt=0,fallbackLogged=false;
  function key(s){return s&&s.away&&s.home?String(s.matchId||'')+'|'+String(s.away.abbreviation||'')+'|'+String(s.home.abbreviation||''):'';}
  function allowed(s){var c=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&c.replayActive===true&&!c.paused&&!c.playSelection);}
  function id(e){var t=e&&e.nativeCameraToken;return typeof t==='string'&&/^[1-9][0-9]{0,9}$/.test(t)&&Number(t)<=4294967295?t:'';}
  function send(action,t){if(api.host==='game'&&t)api.log('NFL2K5_CAMERA_MARK_'+action+':'+t);}
  function visible(){
    var image=network.querySelector('img'),r=network.getBoundingClientRect();
    if(!network.isConnected||network.getAttribute('aria-hidden')==='true'||!image||!image.complete||image.naturalWidth<=0||image.naturalHeight<=0||
       !document.fonts||document.fonts.status!=='loaded'||document.visibilityState==='hidden'||
       !(r.width>0&&r.height>0&&r.left>=0&&r.top>=0&&r.right<=window.innerWidth&&r.bottom<=window.innerHeight))return false;
    var ir=image.getBoundingClientRect();
    if(!(ir.width>0&&ir.height>0&&ir.left>=r.left&&ir.top>=r.top&&ir.right<=r.right&&ir.bottom<=r.bottom))return false;
    for(var n=image;n&&n.nodeType===1;n=n.parentElement){var style=getComputedStyle(n);if(n.hidden||style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse'||Number(style.opacity)<.999)return false;}
    return true;
  }
  function revoke(){paintFrames=0;painted=false;if(acknowledged)send('HIDE',token);acknowledged=false;}
  function release(){var old=token;version++;if(frame)cancelAnimationFrame(frame);frame=0;token=owner='';acknowledged=painted=false;paintFrames=0;fallbackLogged=false;if(old)send('HIDE',old);}
  function inspect(v){
    frame=0;if(v!==version||!token)return;
    if(!allowed(latest)||key(latest)!==owner){release();return;}
    if(visible()){
      paintFrames++;
      if(paintFrames>=2){painted=true;if(api.host==='game'&&!acknowledged){acknowledged=true;send('SHOW',token);}}
    }else{
      revoke();
      if(!fallbackLogged&&performance.now()-startedAt>=1000){fallbackLogged=true;if(api.log)api.log('CBS_CAMERA_MARK_FALLBACK:not-painted');}
    }
    frame=requestAnimationFrame(function(){inspect(v);});
  }
  function monitor(){if(!frame&&token){var v=version;frame=requestAnimationFrame(function(){inspect(v);});}}
  function handleEvent(e){
    if(!e||(e.name!=='CAMERA_MARK_SHOW'&&e.name!=='CAMERA_MARK_HIDE'))return false;
    var t=id(e);if(!t||e.source!=='native')return true;
    if(e.name==='CAMERA_MARK_HIDE'){if(e.nativeVisible===false&&t===token)release();return true;}
    if(e.nativeVisible!==true||!allowed(latest))return true;
    if(token===t&&owner===key(latest)){monitor();return true;}
    release();token=t;owner=key(latest);startedAt=performance.now();monitor();return true;
  }
  function updateState(s){latest=s;if(!token)return;if(!allowed(s)||key(s)!==owner){release();return;}if(!visible())revoke();monitor();}
  window.CBSCameraMark={updateState:updateState,handleEvent:handleEvent,cancel:release,
    snapshot:function(){return {active:!!token,token:token,acknowledged:acknowledged,painted:painted,phase:!token?'hidden':painted?'live':'waiting'};}};
})();
