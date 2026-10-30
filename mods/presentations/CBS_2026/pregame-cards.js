/* CBS design-system adaptation of title-owned coin-toss, player and notice cards.
 * Native metadata supplies the words, choices, subject and lifetime. This
 * component does not choose a toss result or send controller input. */
(function () {
  'use strict';
  var api=window.NFL2K5,canvas=document.getElementById('canvas');
  if(!api||!canvas)return;
  var root=document.createElement('section');root.id='cbs-pregame-card';root.hidden=true;
  root.setAttribute('aria-hidden','true');
  root.innerHTML='<div class="cbs-pg-plate"><div class="cbs-pg-team"><img alt=""></div><img class="cbs-pg-portrait" alt=""><div class="cbs-pg-content"><div class="cbs-pg-heading"><span></span><img src="assets/branding/cbs-sports.svg" alt="CBS Sports"></div><div class="cbs-pg-player"><span class="cbs-pg-name"></span><span class="cbs-pg-position"></span></div><div class="cbs-pg-caption"></div><div class="cbs-pg-text"></div><div class="cbs-pg-columns"></div><div class="cbs-pg-choices"></div></div><img class="cbs-pg-eye" src="assets/branding/cbs-eye.svg" alt=""></div>';
  canvas.appendChild(root);
  var latest=null,match='',token='',slot=-1,payload=null,phase='hidden',acknowledged=false,released=false;
  var version=0,timer=0,frames=[],entranceAt=0;
  function text(v){return typeof v==='string'&&v.trim().length>0;}
  function code(t){return t&&text(t.abbreviation)?t.abbreviation.trim().toUpperCase():'';}
  function key(s){return s&&s.away&&s.home?String(s.matchId||'')+'|'+code(s.away)+'|'+code(s.home):'';}
  function base(s){var c=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&s.gameStatus!=='final'&&!c.paused&&!c.playSelection);}
  function permitted(s,p){return base(s)&&!!p&&(p.kind==='coin-toss'?s.phase===0:p.kind==='notice'?Number.isInteger(p.nativePhase)&&p.nativePhase===s.phase:p.nativePhase===s.phase||s.phase===0);}
  function replay(){return !!(window.CBSNativeReplay&&CBSNativeReplay.snapshot&&CBSNativeReplay.snapshot().token);}
  function id(e){var n=e&&e.replacementToken;return Number.isInteger(n)&&n>0&&n<=4294967295?String(n):'';}
  function send(action,t,kind){if(api.host==='game'&&t)api.log((kind==='notice'?'NFL2K5_NOTICE_':'NFL2K5_PREGAME_')+action+':'+t);}
  function stop(){version++;clearTimeout(timer);timer=0;frames.forEach(cancelAnimationFrame);frames=[];}
  function revoke(){if(acknowledged){send('HIDE',token,payload&&payload.kind);acknowledged=false;released=true;}}
  function cancel(){var old=token,kind=payload&&payload.kind,notify=old&&!released;stop();token=match='';slot=-1;payload=null;phase='hidden';acknowledged=false;released=false;root.hidden=true;root.classList.remove('on','out');root.setAttribute('aria-hidden','true');if(notify)send('HIDE',old,kind);}
  function current(v,t){return version===v&&token===t&&permitted(latest,payload)&&key(latest)===match;}
  function reduced(){var c=latest&&latest.context||{};return typeof c.animationsEnabled==='boolean'?!c.animationsEnabled:document.documentElement.getAttribute('data-preview-motion')!=='full'&&window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches;}
  function frame(fn){var h=requestAnimationFrame(function(time){frames=frames.filter(function(n){return n!==h;});fn(time);});frames.push(h);}
  function defer(){stop();revoke();phase='deferred';root.hidden=true;root.classList.remove('on','out');root.setAttribute('aria-hidden','true');}
  function asset(src){return text(src)&&/^(?:https?:\/\/|assets\/)/i.test(src)&&!/[\u0000-\u001f]/.test(src)?src:'';}
  function team(e){if(e.team!=='away'&&e.team!=='home')return null;var t=latest&&latest[e.team];if(!t||!/^[A-Z0-9]{2,4}$/.test(code(t)))return null;if(e.teamData&&code(e.teamData)!==code(t))return null;return e.teamData||t;}
  var aliases={STL:'LAR',SD:'LAC',OAK:'LV',JAC:'JAX',WSH:'WAS'};
  function teamLogo(t){var c=code(t);return c?'assets/logos/'+(aliases[c]||c)+'.png':'';}
  function setImage(n,src){if(src)n.setAttribute('src',src);else n.removeAttribute('src');n.hidden=!src;}
  function imageReady(src){return new Promise(function(resolve,reject){if(!src){resolve('');return;}var n=new Image();n.onload=function(){n.naturalWidth>0?resolve(src):reject(new Error(src));};n.onerror=function(){reject(new Error(src));};n.src=src;});}
  function assets(p){
    if(!document.fonts||!document.fonts.ready)return Promise.reject(new Error('font-api'));
    var fonts=document.fonts.load?document.fonts.load('700 30px "CBS Preview Bold"').then(function(){return document.fonts.ready;}):Promise.resolve(document.fonts.ready);
    var logo=teamLogo(p.team),fallback=asset(p.team&&p.team.logo);
    return Promise.all([fonts,imageReady('assets/branding/cbs-eye.svg'),imageReady('assets/branding/cbs-sports.svg'),imageReady(logo).catch(function(err){if(fallback&&fallback!==logo)return imageReady(fallback);throw err;}),imageReady(p.portrait).catch(function(){return '';})]).then(function(values){if(document.fonts.status!=='loaded'||!document.fonts.check('700 30px "CBS Preview Bold"'))throw new Error('font-unavailable');return {logo:values[3],portrait:values[4]};});
  }
  function append(parent,cls,value){var n=document.createElement('div');n.className=cls;n.textContent=value;parent.appendChild(n);return n;}
  function render(p,a){
    root.setAttribute('data-kind',p.kind);root.setAttribute('data-motion',reduced()?'reduced':'full');root.setAttribute('aria-label',p.kind==='coin-toss'?'Coin toss':p.kind==='notice'?'Broadcast notice':'Player information');
    root.classList.toggle('has-team',!!a.logo);root.classList.toggle('has-portrait',!!a.portrait);
    var color=p.team&&(p.team.primaryColor||p.team.color);root.style.setProperty('--cbs-pg-team',typeof color==='string'&&/^#[\da-f]{6}$/i.test(color)?color:'#163b61');
    setImage(root.querySelector('.cbs-pg-team img'),a.logo);setImage(root.querySelector('.cbs-pg-portrait'),a.portrait);
    root.querySelector('.cbs-pg-heading span').textContent=p.kind==='player-fact'?p.category||'PLAYER PROFILE':p.title;
    root.querySelector('.cbs-pg-name').textContent=p.name||'';root.querySelector('.cbs-pg-position').textContent=p.position||'';
    root.querySelector('.cbs-pg-player').hidden=p.kind!=='player-fact';root.querySelector('.cbs-pg-caption').textContent=p.caption||'';root.querySelector('.cbs-pg-caption').hidden=!p.caption;
    root.querySelector('.cbs-pg-text').textContent=p.text;root.querySelector('.cbs-pg-text').hidden=!p.text;
    var cols=root.querySelector('.cbs-pg-columns');cols.textContent='';cols.hidden=!p.columns.length;cols.style.setProperty('--cbs-pg-columns',p.columns.length||1);
    p.columns.forEach(function(c){var cell=append(cols,'cbs-pg-column','');append(cell,'cbs-pg-label',c.label);append(cell,'cbs-pg-value',c.value);});
    var choices=root.querySelector('.cbs-pg-choices');choices.textContent='';choices.hidden=!p.choices.length;
    p.choices.forEach(function(c){var row=append(choices,'cbs-pg-choice'+(c.selected?' selected':''),'');row.setAttribute('aria-current',c.selected?'true':'false');var label=p.kind==='notice'?append(row,'cbs-pg-choice-label',''):row;append(label,'cbs-pg-choice-marker','');append(label,'cbs-pg-choice-text',c.text);if(c.button)append(label,'cbs-pg-choice-button',c.button);if(p.kind==='notice'){row.setAttribute('data-outcomes',String(c.cells.length));c.cells.forEach(function(value){append(row,'cbs-pg-choice-outcome',value);});}});
  }
  function painted(){var r=root.getBoundingClientRect();if(!root.isConnected||root.hidden||!(r.width>0&&r.height>0&&r.left>=0&&r.top>=0&&r.right<=innerWidth&&r.bottom<=innerHeight))return false;for(var n=root;n&&n.nodeType===1;n=n.parentElement){var s=getComputedStyle(n);if(n.hidden||s.display==='none'||s.visibility==='hidden'||s.visibility==='collapse'||Number(s.opacity)<.999)return false;}return true;}
  function acknowledge(v,t){frame(function(){frame(function inspect(){if(!current(v,t))return;if(replay()){defer();return;}if(!painted()){if(performance.now()-entranceAt<1000){frame(inspect);return;}if(api.log)api.log('CBS_PREGAME_FALLBACK:not-visible-after-entrance');cancel();return;}phase='live';if(payload.native&&api.host==='game'&&!acknowledged){acknowledged=true;released=false;send('SHOW',t,payload.kind);}});});}
  function present(update){
    if(!token||!permitted(latest,payload)||key(latest)!==match)return;if(replay()){defer();return;}
    stop();revoke();phase='loading';var v=version,t=token;
    if(!update){root.hidden=true;root.classList.remove('on','out');}
    assets(payload).then(function(a){if(!current(v,t))return;if(replay()){defer();return;}render(payload,a);root.hidden=false;root.setAttribute('aria-hidden','false');void root.offsetWidth;root.classList.remove('out');root.classList.add('on');phase='entering';entranceAt=performance.now();timer=setTimeout(function(){timer=0;if(current(v,t))acknowledge(v,t);},update||reduced()?0:200);}).catch(function(e){if(!current(v,t))return;if(api.log)api.log('CBS_PREGAME_FALLBACK:'+String(e&&e.message||e));cancel();});
  }
  function columns(a){if(a==null)return [];if(!Array.isArray(a)||a.length>8||a.some(function(c){return !c||!text(c.label)||!(text(c.value)||typeof c.value==='number'&&isFinite(c.value));}))return null;return a.map(function(c){return {label:c.label,value:String(c.value)};});}
  function choices(a){if(a==null)return [];if(!Array.isArray(a)||a.length>4||a.some(function(c){return !c||!text(c.text)||c.selected!=null&&typeof c.selected!=='boolean'||c.button!=null&&typeof c.button!=='string';}))return null;return a.map(function(c){return {text:c.text,selected:c.selected===true,button:text(c.button)?c.button:''};});}
  function noticeChoices(a,interactive){var result=choices(a);if(!result)return null;if(interactive&&(result.length!==2||a.filter(function(c){return c.selected===true;}).length!==1||a.some(function(c){return typeof c.selected!=='boolean';})))return null;if(result.length&&a.some(function(c){return c.cells!=null&&(!Array.isArray(c.cells)||c.cells.length>2||c.cells.some(function(value){return typeof value!=='string';}))||interactive&&(!Array.isArray(c.cells)||c.cells.length!==2);}))return null;return result.map(function(c,i){c.cells=a[i].cells?a[i].cells.slice():[];return c;});}
  function parse(e){
    var native=e.source==='native'&&e.nativeVisible===true,sample=api.host!=='game'&&e.source==='sample'&&e.preview===true;
    if(!native&&!sample)return null;var t=team(e),hasTeam=e.team!=null&&e.team!=='';
    if(hasTeam&&!t)return null;
    var p={kind:e.name==='COIN_TOSS'?'coin-toss':e.name==='BROADCAST_NOTICE'?'notice':'player-fact',native:native,nativePhase:Number.isInteger(e.phase)?e.phase:null,team:t,text:typeof e.text==='string'?e.text:'',title:'',caption:'',category:'',name:'',position:'',portrait:'',columns:[],choices:[]};
    if(p.kind==='coin-toss'){if(!text(p.text)||!text(e.title))return null;p.title=e.title;p.choices=choices(e.choices);if(!p.choices)return null;}
    else if(p.kind==='notice'){if(!text(e.title)||typeof e.text!=='string'||e.interactive!=null&&typeof e.interactive!=='boolean')return null;p.title=e.title;p.interactive=e.interactive===true;p.choices=noticeChoices(e.choices,p.interactive);if(!p.choices||!text(p.text)&&!p.choices.length)return null;}
    else {var player=e.player;p.columns=columns(e.columns);if(!t||!player||!text(player.fullName)||!p.columns||!text(p.text)&&!p.columns.length)return null;if(text(player.name)&&player.name.trim().split(/\s+/).pop().toLowerCase()!==player.fullName.trim().split(/\s+/).pop().toLowerCase())return null;p.name=player.fullName;p.position=[text(player.position)?player.position:'',Number.isInteger(player.number)&&player.number>=0&&player.number<=99?'#'+player.number:''].filter(Boolean).join(' · ');p.portrait=asset(player.portrait);p.caption=text(e.caption)?e.caption:'';p.category=text(e.category)?e.category:'';}
    return p;
  }
  function close(){if(phase==='exiting')return;stop();revoke();phase='exiting';root.classList.remove('on');root.classList.add('out');var v=version,t=token;timer=setTimeout(function(){timer=0;if(version===v&&token===t)cancel();},reduced()?0:160);}
  function handleEvent(e){
    if(!e||!/^((COIN_TOSS|PLAYER_FACT|BROADCAST_NOTICE)(_HIDE)?)$/.test(e.name))return false;
    var t=id(e),kind=e.name.indexOf('COIN_TOSS')===0?'coin-toss':e.name.indexOf('BROADCAST_NOTICE')===0?'notice':'player-fact';
    if(/_HIDE$/.test(e.name)){if(e.source!=='native'||e.nativeVisible!==false||!payload||payload.kind!==kind||!t||t!==token)return true;close();return true;}
    var p=parse(e);if(!p||!t||!permitted(latest,p)){if(e.source==='native'&&e.nativeVisible===true)cancel();return true;}
    if(token===t&&payload&&payload.kind===kind&&match===key(latest)&&(phase==='exiting'||JSON.stringify(payload)===JSON.stringify(p)))return true;
    var update=token===t&&match===key(latest)&&payload&&payload.kind===kind&&phase!=='exiting';
    if(!update)cancel();token=t;match=key(latest);slot=Number.isInteger(e.slot)?e.slot:-1;payload=p;present(update);return true;
  }
  function updateState(s){latest=s;if(!token)return;if(!permitted(s,payload)||key(s)!==match){cancel();return;}if(phase==='exiting')return;if(replay()){if(phase!=='deferred')defer();}else if(phase==='deferred')present(false);}
  window.CBSPregame={updateState:updateState,handleEvent:handleEvent,cancel:cancel,snapshot:function(){return {active:!!token,token:token,slot:slot,kind:payload?payload.kind:'',phase:phase,acknowledged:acknowledged,match:match,payload:payload?JSON.parse(JSON.stringify(payload)):null};}};
})();
