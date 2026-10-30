/* Token-owned, live starting-unit roster. This is an explicitly labelled
 * CBS design-system adaptation, not a measured television lineup replica.
 * The native ticker owns the sequence/lifetime; no player-cycle timer runs.
 */
(function () {
  'use strict';
  var api=window.NFL2K5,canvas=document.getElementById('canvas');
  if(!api||!canvas)return;
  var root=document.createElement('section');root.id='cbs-lineup';root.hidden=true;
  root.setAttribute('aria-hidden','true');root.setAttribute('aria-label','Starting lineup');
  root.innerHTML='<div class="lu-panel"><header class="lu-head"><img class="lu-team-logo" alt=""><span class="lu-team-code"></span><div class="lu-heading"><div class="lu-unit"></div><div class="lu-team-name"></div></div><img class="lu-brand" src="assets/branding/cbs-sports.svg" alt="CBS Sports"></header><div class="lu-grid"></div></div>';
  canvas.appendChild(root);
  var latest=null,token='',owner='',payload=null,phase='hidden',acknowledged=false,version=0;
  var frames=[],entranceTimer=0,assetTimer=0,startedAt=0;
  function key(s){return s&&s.away&&s.home?String(s.matchId||'')+'|'+String(s.away.abbreviation||'')+'|'+String(s.home.abbreviation||''):'';}
  function valid(s){return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&s.gameStatus!=='final');}
  function blocked(s){var c=s&&s.context||{};return !!(c.paused||c.playSelection||c.replayActive||(window.CBSNativeReplay&&CBSNativeReplay.snapshot&&CBSNativeReplay.snapshot().token));}
  function id(e){var t=e&&e.nativeLineupToken;return typeof t==='string'&&/^[1-9][0-9]{0,9}$/.test(t)&&Number(t)<=4294967295?t:'';}
  function send(action,t){if(api.host==='game'&&t)api.log('NFL2K5_LINEUP_'+action+':'+t);}
  function stop(){version++;clearTimeout(entranceTimer);clearTimeout(assetTimer);entranceTimer=assetTimer=0;frames.forEach(cancelAnimationFrame);frames=[];}
  function release(){var old=token;stop();token=owner='';payload=null;acknowledged=false;phase='hidden';root.hidden=true;root.classList.remove('on');root.setAttribute('aria-hidden','true');if(old)send('HIDE',old);}
  function current(v,t){return version===v&&token===t&&valid(latest)&&key(latest)===owner;}
  function frame(fn){var h=requestAnimationFrame(function(t){frames=frames.filter(function(n){return n!==h;});fn(t);});frames.push(h);}
  function defer(){stop();if(acknowledged)send('HIDE',token);acknowledged=false;phase='deferred';root.hidden=true;root.classList.remove('on');root.setAttribute('aria-hidden','true');}
  function fail(v,t,reason){if(!current(v,t))return;if(api.log)api.log('CBS_LINEUP_FALLBACK:'+reason);release();}
  function text(v,max){return typeof v==='string'&&v.length<=max?v.trim():'';}
  function safeImage(src){return typeof src==='string'&&src.length<=2048&&!/^\s*(?:javascript|data|file):/i.test(src)?src:'';}
  function copy(e){
    if((e.team!=='away'&&e.team!=='home')||(e.unit!=='offense'&&e.unit!=='defense')||!Array.isArray(e.players)||!e.players.length||e.players.length>11)return null;
    var players=[];
    for(var i=0;i<e.players.length;i++){
      var p=e.players[i];if(!p||typeof p!=='object')return null;
      var first=text(p.first,80),last=text(p.last,80),name=first&&last?first+' '+last:text(p.name,160),position=text(p.position,16);
      if(!name||name.length>160||!position||!Number.isInteger(p.number)||p.number<0||p.number>99)return null;
      players.push({name:name,position:position,number:p.number,college:text(p.college,160),portrait:safeImage(p.portrait)});
    }
    return {team:e.team,unit:e.unit,players:players};
  }
  function image(node,src,required,v,t){return new Promise(function(resolve,reject){
    if(!src){node.hidden=true;resolve();return;}
    var probe=new Image();probe.onload=function(){if(!current(v,t)){resolve();return;}if(!(probe.naturalWidth>0&&probe.naturalHeight>0)){probe.onerror();return;}node.src=src;node.hidden=false;resolve();};
    probe.onerror=function(){if(!current(v,t)){resolve();return;}node.hidden=true;node.removeAttribute('src');if(required)reject(new Error('required-image'));else resolve();};probe.src=src;
  });}
  function reduced(){var c=latest&&latest.context||{};if(typeof c.animationsEnabled==='boolean')return !c.animationsEnabled;return document.documentElement.getAttribute('data-preview-motion')!=='full'&&window.matchMedia&&matchMedia('(prefers-reduced-motion: reduce)').matches;}
  function populated(){
    var r=root.getBoundingClientRect();if(root.hidden||!root.isConnected||document.visibilityState==='hidden'||!document.fonts||document.fonts.status!=='loaded'||!(r.width>0&&r.height>0&&r.left>=0&&r.top>=0&&r.right<=window.innerWidth&&r.bottom<=window.innerHeight))return false;
    for(var n=root;n&&n.nodeType===1;n=n.parentElement){var s=getComputedStyle(n);if(n.hidden||s.display==='none'||s.visibility==='hidden'||s.visibility==='collapse'||Number(s.opacity)<.999)return false;}
    var logo=root.querySelector('.lu-brand');if(!logo.complete||logo.naturalWidth<=0||logo.hidden)return false;
    return Array.from(root.querySelectorAll('.lu-name')).every(function(n){return n.textContent&&n.scrollWidth<=n.clientWidth+1;});
  }
  function fit(){
    return Array.from(root.querySelectorAll('.lu-name,.lu-college,.lu-team-name')).every(function(n){
      if(n.hidden)return true;var base=n.classList.contains('lu-name')?28:n.classList.contains('lu-college')?18:32,min=n.classList.contains('lu-college')?14:18;
      n.style.fontSize=base+'px';if(n.scrollWidth>n.clientWidth)n.style.fontSize=Math.max(min,Math.floor(base*n.clientWidth/n.scrollWidth))+'px';
      return n.scrollWidth<=n.clientWidth+1;
    });
  }
  function monitor(v,t){frame(function(){if(!current(v,t))return;if(blocked(latest)){defer();return;}if(!populated()){fail(v,t,'paint-lost');return;}monitor(v,t);});}
  function acknowledge(v,t){frame(function(){frame(function inspect(){
    if(!current(v,t))return;if(blocked(latest)){defer();return;}
    if(!populated()){if(performance.now()-startedAt<1000){frame(inspect);return;}fail(v,t,'not-painted');return;}
    phase='live';if(api.host==='game'&&!acknowledged){acknowledged=true;send('SHOW',t);}monitor(v,t);
  });});}
  function present(){
    if(!token||!valid(latest)||key(latest)!==owner)return;if(blocked(latest)){defer();return;}
    stop();var v=version,t=token,team=latest[payload.team]||{},grid=root.querySelector('.lu-grid'),promises=[];
    acknowledged=false;phase='loading';root.hidden=true;root.classList.remove('on');grid.replaceChildren();
    root.querySelector('.lu-unit').textContent='STARTING '+payload.unit.toUpperCase();
    root.querySelector('.lu-team-name').textContent=((team.city||'')+' '+(team.name||'')).trim()||String(team.abbreviation||'');
    root.querySelector('.lu-team-code').textContent=String(team.abbreviation||'');
    var code=String(team.abbreviation||'').toUpperCase(),src=/^[A-Z0-9]{2,4}$/.test(code)?'assets/logos/'+code+'.png':safeImage(team.logo);
    promises.push(image(root.querySelector('.lu-team-logo'),src,false,v,t));
    promises.push(image(root.querySelector('.lu-brand'),'assets/branding/cbs-sports.svg',true,v,t));
    payload.players.forEach(function(p){
      var row=document.createElement('div');row.className='lu-player';
      row.innerHTML='<img class="lu-portrait" alt=""><span class="lu-position"></span><span class="lu-number"></span><div class="lu-person"><span class="lu-name"></span><span class="lu-college"></span></div>';
      row.querySelector('.lu-position').textContent=p.position;row.querySelector('.lu-number').textContent=String(p.number);
      row.querySelector('.lu-name').textContent=p.name;row.querySelector('.lu-college').textContent=p.college;row.querySelector('.lu-college').hidden=!p.college;
      grid.appendChild(row);promises.push(image(row.querySelector('.lu-portrait'),p.portrait,false,v,t));
    });
    if(!document.fonts||!document.fonts.load||!document.fonts.ready){fail(v,t,'font-api');return;}
    promises.push(document.fonts.load('700 28px "CBS Preview Bold"').then(function(){return document.fonts.ready;}).then(function(){if(document.fonts.status!=='loaded'||!document.fonts.check('700 28px "CBS Preview Bold"'))throw new Error('font-unavailable');}));
    assetTimer=setTimeout(function(){fail(v,t,'asset-timeout');},1500);
    Promise.all(promises).then(function(){
      if(!current(v,t))return;clearTimeout(assetTimer);assetTimer=0;if(blocked(latest)){defer();return;}
      root.setAttribute('data-motion',reduced()?'reduced':'full');root.hidden=false;root.setAttribute('aria-hidden','false');
      if(!fit()){fail(v,t,'text-does-not-fit');return;}
      void root.offsetWidth;root.classList.add('on');phase='entering';startedAt=performance.now();
      entranceTimer=setTimeout(function(){entranceTimer=0;if(current(v,t))acknowledge(v,t);},reduced()?0:200);
    }).catch(function(e){fail(v,t,String(e&&e.message||e));});
  }
  function handleEvent(e){
    if(e&&(e.name==='GAME_ENDED'||e.name==='GAME_STARTED')){release();return false;}
    if(!e||(e.name!=='LINEUP'&&e.name!=='LINEUP_HIDE'))return false;
    var t=id(e);if(!t||e.source!=='native')return true;
    if(e.name==='LINEUP_HIDE'){if(e.nativeVisible===false&&t===token)release();return true;}
    if(e.nativeVisible!==true||!valid(latest))return true;var next=copy(e);if(!next)return true;
    if(token===t&&owner===key(latest)&&JSON.stringify(payload)===JSON.stringify(next))return true;
    release();token=t;owner=key(latest);payload=next;present();return true;
  }
  function updateState(s){latest=s;if(!token)return;if(!valid(s)||key(s)!==owner){release();return;}if(blocked(s)){if(phase!=='deferred')defer();}else if(phase==='deferred')present();}
  window.CBSLineup={updateState:updateState,handleEvent:handleEvent,cancel:release,
    snapshot:function(){return {active:!!token,token:token,phase:phase,deferred:phase==='deferred',acknowledged:acknowledged,team:payload?payload.team:'',unit:payload?payload.unit:'',players:payload?payload.players.map(function(p){return Object.assign({},p);}):[]};}};
})();
