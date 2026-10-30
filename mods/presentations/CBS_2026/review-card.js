/* Native Challenge / ruling notice, adapted to the existing CBS design
 * system. This is not a measured reconstruction of a CBS challenge graphic.
 * The title owns every word and the lifetime; this page owns only its art.
 * SHOW acknowledgment follows asset readiness, entrance and visible paint.
 */
(function () {
  'use strict';
  var api=window.NFL2K5,canvas=document.getElementById('canvas');
  if(!api||!canvas)return;
  var root=document.createElement('section');root.id='cbs-review-card';root.hidden=true;
  root.setAttribute('aria-hidden','true');root.setAttribute('aria-label','Replay review');
  root.innerHTML='<div class="cbs-review-plate"><img class="cbs-review-eye" src="assets/branding/cbs-eye.svg" alt=""><div class="cbs-review-heading"><span></span><img src="assets/branding/cbs-sports.svg" alt="CBS Sports"></div><div class="cbs-review-message"></div><div class="cbs-review-reason"></div></div>';
  canvas.appendChild(root);
  var latest=null,owner='',token='',version=0,phase='hidden',acknowledged=false,payload=null;
  var timer=0,frames=[],entranceAt=0;
  function key(s){return s&&s.away&&s.home?String(s.matchId||'')+'|'+String(s.away.abbreviation||'')+'|'+String(s.home.abbreviation||''):'';}
  function allowed(s){var c=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&!c.paused&&!c.playSelection);}
  function replay(){return !!(window.CBSNativeReplay&&CBSNativeReplay.snapshot&&CBSNativeReplay.snapshot().token);}
  function id(e){var t=e&&e.nativeReviewToken;return typeof t==='string'&&/^[1-9][0-9]{0,9}$/.test(t)&&Number(t)<=4294967295?t:'';}
  function send(action,t){if(api.host==='game'&&t)api.log('NFL2K5_REVIEW_'+action+':'+t);}
  function stopWork(){version++;clearTimeout(timer);timer=0;frames.forEach(cancelAnimationFrame);frames=[];}
  function release(){var old=token;stopWork();token=owner='';payload=null;acknowledged=false;phase='hidden';root.hidden=true;root.classList.remove('on');root.setAttribute('aria-hidden','true');if(old)send('HIDE',old);}
  function current(v,t){return version===v&&token===t&&allowed(latest)&&key(latest)===owner;}
  function reduced(){var c=latest&&latest.context||{};if(typeof c.animationsEnabled==='boolean')return !c.animationsEnabled;return document.documentElement.getAttribute('data-preview-motion')!=='full'&&window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches;}
  function defer(){
    stopWork();if(acknowledged)send('HIDE',token);acknowledged=false;phase='deferred';
    root.hidden=true;root.classList.remove('on');root.setAttribute('aria-hidden','true');
  }
  function frame(fn){var handle=requestAnimationFrame(function(t){frames=frames.filter(function(n){return n!==handle;});fn(t);});frames.push(handle);}
  function painted(){
    var r=root.getBoundingClientRect();
    if(!root.isConnected||root.hidden||!(r.width>0&&r.height>0&&r.left>=0&&r.top>=0&&r.right<=window.innerWidth&&r.bottom<=window.innerHeight))return false;
    for(var n=root;n&&n.nodeType===1;n=n.parentElement){var style=getComputedStyle(n);if(n.hidden||style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse'||Number(style.opacity)<.999)return false;}
    return true;
  }
  function fallback(v,t,reason){if(!current(v,t))return;if(api.log)api.log('CBS_REVIEW_FALLBACK:'+reason);release();}
  function acknowledge(v,t){
    frame(function(){frame(function inspect(){
      if(!current(v,t))return;if(replay()){defer();return;}
      if(!painted()){
        if(performance.now()-entranceAt<1000){frame(inspect);return;}
        fallback(v,t,'not-visible-after-entrance');return;
      }
      phase='live';if(api.host==='game'&&!acknowledged){acknowledged=true;send('SHOW',t);}
    });});
  }
  function assets(){
    if(!document.fonts||!document.fonts.ready)return Promise.reject(new Error('font-api'));
    var fonts=document.fonts.load?document.fonts.load('700 30px "CBS Preview Bold"').then(function(){return document.fonts.ready;}):Promise.resolve(document.fonts.ready);
    var images=['assets/branding/cbs-eye.svg','assets/branding/cbs-sports.svg'].map(function(src){return new Promise(function(resolve,reject){var image=new Image();image.onload=function(){if(image.naturalWidth>0)resolve();else reject(new Error(src));};image.onerror=function(){reject(new Error(src));};image.src=src;});});
    return Promise.all([fonts].concat(images)).then(function(){if(document.fonts.status!=='loaded'||!document.fonts.check('700 30px "CBS Preview Bold"'))throw new Error('font-unavailable');});
  }
  function present(){
    if(!token||!allowed(latest)||key(latest)!==owner)return;
    if(replay()){defer();return;}
    stopWork();phase='loading';root.hidden=true;root.classList.remove('on');
    var v=version,t=token;
    assets().then(function(){
      if(!current(v,t))return;if(replay()){defer();return;}
      root.setAttribute('data-review-stage',payload.reviewStage);
      root.querySelector('.cbs-review-heading span').textContent=payload.heading;
      root.querySelector('.cbs-review-message').textContent=payload.message;
      root.querySelector('.cbs-review-reason').textContent=payload.reason;
      root.querySelector('.cbs-review-message').hidden=!payload.message;
      root.querySelector('.cbs-review-reason').hidden=!payload.reason;
      root.setAttribute('data-motion',reduced()?'reduced':'full');
      root.hidden=false;root.setAttribute('aria-hidden','false');void root.offsetWidth;
      root.classList.add('on');phase='entering';entranceAt=performance.now();
      timer=setTimeout(function(){timer=0;if(current(v,t))acknowledge(v,t);},reduced()?0:200);
    }).catch(function(error){fallback(v,t,String(error&&error.message||error));});
  }
  function handleEvent(e){
    if(!e||(e.name!=='REVIEW_SHOW'&&e.name!=='REVIEW_HIDE'))return false;
    var t=id(e);if(!t||e.source!=='native')return true;
    if(e.name==='REVIEW_HIDE'){if(e.nativeVisible===false&&t===token)release();return true;}
    if(e.nativeVisible!==true||!allowed(latest)||typeof e.heading!=='string'||!e.heading.trim()||
       typeof e.message!=='string'||(e.reason!=null&&typeof e.reason!=='string')||
       (e.reviewStage!=='review'&&e.reviewStage!=='ruling'))return true;
    var next={heading:e.heading,message:e.message,reason:e.reason||'',reviewStage:e.reviewStage};
    if(token===t&&owner===key(latest)&&JSON.stringify(payload)===JSON.stringify(next))return true;
    release();token=t;owner=key(latest);payload=next;present();return true;
  }
  function updateState(s){latest=s;if(!token)return;if(!allowed(s)||key(s)!==owner){release();return;}if(replay()){if(phase!=='deferred')defer();}else if(phase==='deferred')present();}
  window.CBSReviewCard={updateState:updateState,handleEvent:handleEvent,cancel:release,
    snapshot:function(){return {active:!!token,token:token,phase:phase,deferred:phase==='deferred',acknowledged:acknowledged,reviewStage:payload?payload.reviewStage:'',heading:payload?payload.heading:'',message:payload?payload.message:'',reason:payload?payload.reason:''};}};
})();
