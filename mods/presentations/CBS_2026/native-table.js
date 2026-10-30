/* Title-owned team tables: exact native strings, subject order and lifetime.
 * This is a CBS design-system adaptation, not a measured CBS halftime board.
 * The native bridge validates actual accepted slots/subjects before emitting
 * these events. A table request or a period number cannot acquire ownership. */
(function () {
  'use strict';
  var api=window.NFL2K5,canvas=document.getElementById('canvas');
  if(!api||!canvas)return;
  var root=document.createElement('section');root.id='cbs-native-table';root.hidden=true;
  root.setAttribute('aria-hidden','true');
  root.innerHTML='<div class="cbs-nt-plate"><header class="cbs-nt-title"><span></span><img src="assets/branding/cbs-sports.svg" alt="CBS Sports"></header><table><colgroup></colgroup><thead></thead><tbody></tbody></table><img class="cbs-nt-eye" src="assets/branding/cbs-eye.svg" alt=""></div>';
  canvas.appendChild(root);
  var latest=null,match='',token='',payload=null,phase='hidden',acknowledged=false,released=false;
  var version=0,timer=0,frames=[],entranceAt=0;
  function key(s){return s&&s.away&&s.home?String(s.matchId||'')+'|'+String(s.away.abbreviation||'')+'|'+String(s.home.abbreviation||''):'';}
  function allowed(s){var c=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&!c.paused&&!c.playSelection);}
  function id(e){var t=e&&e.nativeTableToken;return typeof t==='string'&&/^[1-9][0-9]{0,9}$/.test(t)&&Number(t)<=4294967295?t:'';}
  function send(action,t){if(api.host==='game'&&t)api.log('NFL2K5_NATIVE_TABLE_'+action+':'+t);}
  function replay(){return !!(window.CBSNativeReplay&&CBSNativeReplay.snapshot&&CBSNativeReplay.snapshot().token);}
  function reduced(){var c=latest&&latest.context||{};return typeof c.animationsEnabled==='boolean'?!c.animationsEnabled:document.documentElement.getAttribute('data-preview-motion')!=='full'&&window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches;}
  function stop(){version++;clearTimeout(timer);timer=0;frames.forEach(cancelAnimationFrame);frames=[];}
  function revoke(){if(acknowledged){send('HIDE',token);acknowledged=false;released=true;}}
  function cancel(){var old=token,notify=old&&!released;stop();token=match='';payload=null;phase='hidden';acknowledged=false;released=false;root.hidden=true;root.classList.remove('on','out');root.setAttribute('aria-hidden','true');if(notify)send('HIDE',old);}
  function current(v,t){return version===v&&token===t&&allowed(latest)&&key(latest)===match;}
  function frame(fn){var h=requestAnimationFrame(function(time){frames=frames.filter(function(n){return n!==h;});fn(time);});frames.push(h);}
  function defer(){stop();revoke();phase='deferred';root.hidden=true;root.classList.remove('on','out');root.setAttribute('aria-hidden','true');}
  function imageReady(src){return new Promise(function(resolve,reject){var n=new Image();n.onload=function(){n.naturalWidth>0?resolve():reject(new Error(src));};n.onerror=function(){reject(new Error(src));};n.src=src;});}
  function assets(){
    if(!document.fonts||!document.fonts.ready)return Promise.reject(new Error('font-api'));
    var fonts=document.fonts.load?document.fonts.load('700 30px "CBS Preview Bold"').then(function(){return document.fonts.ready;}):Promise.resolve(document.fonts.ready);
    return Promise.all([fonts,imageReady('assets/branding/cbs-eye.svg'),imageReady('assets/branding/cbs-sports.svg')]).then(function(){if(document.fonts.status!=='loaded'||!document.fonts.check('700 30px "CBS Preview Bold"'))throw new Error('font-unavailable');});
  }
  function node(tag,parent,value){var n=document.createElement(tag);if(value!=null)n.textContent=value;parent.appendChild(n);return n;}
  function render(p){
    root.setAttribute('data-motion',reduced()?'reduced':'full');root.setAttribute('data-columns',String(p.headers.length));
    root.setAttribute('aria-label',p.title);root.querySelector('.cbs-nt-title span').textContent=p.title;
    var table=root.querySelector('table');table.setAttribute('aria-label',p.title);
    var cols=root.querySelector('colgroup');cols.textContent='';p.headers.forEach(function(){node('col',cols);});
    var head=root.querySelector('thead');head.textContent='';var header=node('tr',head);
    p.headers.forEach(function(value){node('th',header,value).setAttribute('scope','col');});
    var body=root.querySelector('tbody');body.textContent='';
    p.rows.forEach(function(values){var row=node('tr',body);values.forEach(function(value,index){var cell=node(index?'td':'th',row,value);if(!index)cell.setAttribute('scope','row');});});
  }
  function painted(){
    var r=root.getBoundingClientRect();if(!root.isConnected||root.hidden||!(r.width>0&&r.height>0&&r.left>=0&&r.top>=0&&r.right<=innerWidth&&r.bottom<=innerHeight))return false;
    for(var n=root;n&&n.nodeType===1;n=n.parentElement){var s=getComputedStyle(n);if(n.hidden||s.display==='none'||s.visibility==='hidden'||s.visibility==='collapse'||Number(s.opacity)<.999)return false;}
    return true;
  }
  function acknowledge(v,t){frame(function(){frame(function inspect(){
    if(!current(v,t))return;if(replay()){defer();return;}
    if(!painted()){if(performance.now()-entranceAt<1000){frame(inspect);return;}if(api.log)api.log('CBS_NATIVE_TABLE_FALLBACK:not-visible-after-entrance');cancel();return;}
    phase='live';if(payload.native&&api.host==='game'&&!acknowledged){acknowledged=true;released=false;send('SHOW',t);}
  });});}
  function present(update){
    if(!token||!allowed(latest)||key(latest)!==match)return;if(replay()){defer();return;}
    stop();revoke();phase='loading';var v=version,t=token;if(!update){root.hidden=true;root.classList.remove('on','out');}
    assets().then(function(){if(!current(v,t))return;if(replay()){defer();return;}render(payload);root.hidden=false;root.setAttribute('aria-hidden','false');void root.offsetWidth;root.classList.remove('out');root.classList.add('on');phase='entering';entranceAt=performance.now();timer=setTimeout(function(){timer=0;if(current(v,t))acknowledge(v,t);},update||reduced()?0:200);}).catch(function(e){if(!current(v,t))return;if(api.log)api.log('CBS_NATIVE_TABLE_FALLBACK:'+String(e&&e.message||e));cancel();});
  }
  function parse(e){
    var native=e.source==='native'&&e.nativeVisible===true,sample=api.host!=='game'&&e.source==='sample'&&e.preview===true;
    if(!native&&!sample||typeof e.title!=='string'||!e.title.trim()||e.title.length>256||!Array.isArray(e.headers)||e.headers.length<2||e.headers.length>3||e.headers.some(function(v){return typeof v!=='string'||v.length>128;})||!Array.isArray(e.rows)||!e.rows.length||e.rows.length*e.headers.length>24||e.rows.some(function(row){return !Array.isArray(row)||row.length!==e.headers.length||row.some(function(v){return typeof v!=='string'||v.length>128;});}))return null;
    return {native:native,title:e.title,headers:e.headers.slice(),rows:e.rows.map(function(row){return row.slice();})};
  }
  function close(){if(phase==='exiting')return;stop();revoke();phase='exiting';root.classList.remove('on');root.classList.add('out');var v=version,t=token;timer=setTimeout(function(){timer=0;if(version===v&&token===t)cancel();},reduced()?0:160);}
  function handleEvent(e){
    if(!e||(e.name!=='NATIVE_TABLE_SHOW'&&e.name!=='NATIVE_TABLE_HIDE'))return false;
    var t=id(e);if(e.name==='NATIVE_TABLE_HIDE'){if(e.source==='native'&&e.nativeVisible===false&&t&&t===token)close();return true;}
    var p=parse(e);if(!p||!t||!allowed(latest)){if(e.source==='native'&&e.nativeVisible===true)cancel();return true;}
    if(token===t&&match===key(latest)&&(phase==='exiting'||JSON.stringify(payload)===JSON.stringify(p)))return true;
    var update=token===t&&match===key(latest)&&phase!=='exiting';if(!update)cancel();token=t;match=key(latest);payload=p;present(update);return true;
  }
  function updateState(s){latest=s;if(!token)return;if(!allowed(s)||key(s)!==match){cancel();return;}if(phase==='exiting')return;if(replay()){if(phase!=='deferred')defer();}else if(phase==='deferred')present(false);}
  window.CBSNativeTable={updateState:updateState,handleEvent:handleEvent,cancel:cancel,snapshot:function(){return {active:!!token,token:token,phase:phase,acknowledged:acknowledged,match:match,payload:payload?JSON.parse(JSON.stringify(payload)):null};}};
})();
