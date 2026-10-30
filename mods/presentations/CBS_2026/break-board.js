/* Stacked CBS break scoreboard reconstructed from the user's legacy CBS
 * crop, not a claim of CBS 2026 identity or reference-measured animation.
 * Live teams/scores/caption/clock belong to the caller. This component sends
 * no native ACK, input or guest command. The caller owns its whole lifetime. */
(function () {
  'use strict';
  var canvas=document.getElementById('canvas');if(!canvas)return;
  var root=document.createElement('section');root.id='cbs-break-board';root.hidden=true;
  root.setAttribute('aria-hidden','true');root.setAttribute('aria-label','CBS break scoreboard');
  root.innerHTML='<div class="cbs-bb-design"><div class="cbs-bb-body"><div class="cbs-bb-row away"><div class="cbs-bb-face"><img class="cbs-bb-logo" alt=""><div class="cbs-bb-gloss"></div><div class="cbs-bb-score"></div></div></div><div class="cbs-bb-row home"><div class="cbs-bb-face"><img class="cbs-bb-logo" alt=""><div class="cbs-bb-gloss"></div><div class="cbs-bb-score"></div></div></div></div><footer class="cbs-bb-footer"><div class="cbs-bb-brand"><span class="cbs-bb-cbs"><img src="assets/branding/cbs-sports.svg" alt="CBS"></span><i></i><span class="cbs-bb-nfl">NFL</span></div><div class="cbs-bb-status"><span class="cbs-bb-caption"></span><i> | </i><span class="cbs-bb-clock"></span></div></footer></div>';
  canvas.appendChild(root);
  var token='',payload=null,phase='hidden',ready=false,version=0,timer=0,frames=[],resolvePending=null,entranceAt=0,failure='';
  var aliases={STL:'LAR',SD:'LAC',OAK:'LV',JAC:'JAX',WSH:'WAS'};
  var colors={ARI:'#97233f',ATL:'#a71930',BAL:'#241773',BUF:'#15346c',CAR:'#0085ca',CHI:'#0b162a',CIN:'#c94b0c',CLE:'#49281a',DAL:'#003594',DEN:'#df4b14',DET:'#0076b6',GB:'#203b2c',HOU:'#17324a',IND:'#003a80',JAX:'#006778',KC:'#b51b25',LV:'#34383c',LAC:'#0080c6',LAR:'#003594',MIA:'#008e97',MIN:'#4f2683',NE:'#142b47',NO:'#8e7b52',NYG:'#0b2265',NYJ:'#125740',PHI:'#004c54',PIT:'#27282a',SF:'#aa0000',SEA:'#173f61',TB:'#b51c25',TEN:'#0c2340',WAS:'#5a1414'};
  function text(v){return typeof v==='string'&&v.trim().length>0;}
  function validToken(t){return typeof t==='string'&&t.length>0&&t.length<=96&&!/[\u0000-\u0020\u007f]/.test(t);}
  function asset(src){return text(src)&&/^(?:https?:\/\/|assets\/)/i.test(src)&&!/[\u0000-\u001f]/.test(src)?src:'';}
  function team(t){
    if(!t||!text(t.abbreviation)||!Number.isInteger(t.score)||t.score<0||t.score>999)return null;
    var code=t.abbreviation.trim().toUpperCase();if(!/^[A-Z0-9]{2,4}$/.test(code))return null;code=aliases[code]||code;
    var color=t.primaryColor||t.color;
    return {abbreviation:code,score:t.score,color:typeof color==='string'&&/^#[\da-f]{6}$/i.test(color)?color:colors[code]||'#303b49',logo:asset(t.logo),localLogo:'assets/logos/'+code+'.png',label:[t.city,t.name].filter(text).join(' ')||code};
  }
  function quarter(q){return q<=4?['','1ST','2ND','3RD','4TH'][q]+' QTR':q===5?'OT':'OT'+(q-4);}
  function parse(input){
    if(!input||!validToken(input.token)||(input.kind!=='quarter_end'&&input.kind!=='timeout')||!Number.isInteger(input.quarter)||input.quarter<1||input.quarter>255||typeof input.clockSeconds!=='number'||!isFinite(input.clockSeconds)||input.clockSeconds<0||input.clockSeconds>86400||input.caption!=null&&(typeof input.caption!=='string'||!input.caption.trim()||input.caption.length>64))return null;
    var away=team(input.away),home=team(input.home);if(!away||!home)return null;
    var seconds=Math.ceil(input.clockSeconds);
    return {token:input.token,kind:input.kind,quarter:input.quarter,clockSeconds:input.clockSeconds,caption:text(input.caption)?input.caption:quarter(input.quarter),clock:Math.floor(seconds/60)+':'+('0'+seconds%60).slice(-2),away:away,home:home};
  }
  function motionReduced(){var api=window.NFL2K5,c=api&&api.state&&api.state.context||{};if(typeof c.animationsEnabled==='boolean')return !c.animationsEnabled;return document.documentElement.getAttribute('data-preview-motion')!=='full'&&window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches;}
  function settle(value){var fn=resolvePending;resolvePending=null;if(fn)fn(value);}
  function stop(){version++;clearTimeout(timer);timer=0;frames.forEach(cancelAnimationFrame);frames=[];settle(false);}
  function cancel(){stop();token='';payload=null;phase='hidden';ready=false;root.hidden=true;root.classList.remove('on','out');root.setAttribute('aria-hidden','true');}
  function current(v,t){return version===v&&token===t&&phase!=='hidden'&&phase!=='exiting';}
  function frame(fn){var h=requestAnimationFrame(function(time){frames=frames.filter(function(n){return n!==h;});fn(time);});frames.push(h);}
  function imageReady(src){return new Promise(function(resolve,reject){var n=new Image();n.onload=function(){n.naturalWidth>0?resolve(src):reject(new Error(src));};n.onerror=function(){reject(new Error(src));};n.src=src;});}
  function logoReady(t){var src=t.logo||t.localLogo;return imageReady(src).catch(function(error){if(src!==t.localLogo)return imageReady(t.localLogo);throw error;});}
  function assets(p){
    if(!document.fonts||!document.fonts.ready||!document.fonts.load)return Promise.reject(new Error('font-api'));
    return Promise.all([document.fonts.load('900 100px "CBS Preview Heavy"').then(function(){return document.fonts.ready;}),imageReady('assets/branding/cbs-sports.svg'),logoReady(p.away),logoReady(p.home)]).then(function(result){if(document.fonts.status!=='loaded'||!document.fonts.check('900 100px "CBS Preview Heavy"'))throw new Error('score-font-unavailable');return {away:result[2],home:result[3]};});
  }
  function render(p,images){
    root.setAttribute('data-kind',p.kind);root.setAttribute('data-motion',motionReduced()?'reduced':'full');
    ['away','home'].forEach(function(side){var t=p[side],row=root.querySelector('.cbs-bb-row.'+side),image=row.querySelector('.cbs-bb-logo'),score=row.querySelector('.cbs-bb-score');row.style.setProperty('--cbs-bb-team',t.color);image.setAttribute('src',images[side]);image.setAttribute('alt',t.label);score.textContent=String(t.score);score.setAttribute('data-digits',String(String(t.score).length));});
    root.querySelector('.cbs-bb-caption').textContent=p.caption;root.querySelector('.cbs-bb-clock').textContent=p.clock;
    var status=root.querySelector('.cbs-bb-status');status.style.fontSize='25px';
    /* END 1ST QTR is longer than the crop's 1ST QTR. Preserve its words;
       adapt the footer type size rather than clipping or inventing text. */
    var needed=status.scrollWidth,available=status.clientWidth;if(needed>available)status.style.fontSize=Math.max(14,25*available/needed)+'px';
    root.setAttribute('aria-label',p.away.label+' '+p.away.score+', '+p.home.label+' '+p.home.score+', '+p.caption+', '+p.clock);
  }
  function visible(){
    var r=root.getBoundingClientRect();if(root.hidden||!root.isConnected||!(r.width>0&&r.height>0&&r.left>=0&&r.top>=0&&r.right<=innerWidth&&r.bottom<=innerHeight))return false;
    for(var n=root;n&&n.nodeType===1;n=n.parentElement){var s=getComputedStyle(n);if(n.hidden||s.display==='none'||s.visibility==='hidden'||s.visibility==='collapse'||Number(s.opacity)<.999)return false;}
    if(!document.fonts||document.fonts.status!=='loaded'||!document.fonts.check('900 100px "CBS Preview Heavy"'))return false;
    if(Array.from(root.querySelectorAll('img')).some(function(n){return !n.complete||!n.naturalWidth;}))return false;
    var status=root.querySelector('.cbs-bb-status');return status.scrollWidth<=status.clientWidth+1;
  }
  function painted(t){return !!(validToken(t)&&t===token&&ready&&phase==='live'&&visible());}
  function fail(v,t,reason){if(!current(v,t))return;failure=reason;cancel();}
  function waitForPaint(v,t){frame(function(){frame(function inspect(){
    if(!current(v,t))return;
    if(!visible()){if(performance.now()-entranceAt<1000){frame(inspect);return;}fail(v,t,'not-visible-after-entrance');return;}
    phase='live';ready=true;settle(true);
  });});}
  function show(input){
    var p=parse(input);failure='';if(!p){failure='unsupported-payload';cancel();return Promise.resolve(false);}
    if(token===p.token&&phase==='live'&&JSON.stringify(payload)===JSON.stringify(p)&&painted(p.token))return Promise.resolve(true);
    var update=token===p.token&&payload&&phase==='live'&&p.away.abbreviation===payload.away.abbreviation&&p.home.abbreviation===payload.home.abbreviation&&p.away.logo===payload.away.logo&&p.home.logo===payload.home.logo;
    stop();token=p.token;payload=p;phase='loading';ready=false;var v=version,t=token;
    if(!update){root.hidden=true;root.classList.remove('on','out');root.setAttribute('aria-hidden','true');}
    return new Promise(function(resolve){resolvePending=resolve;assets(p).then(function(images){if(!current(v,t))return;root.hidden=false;root.setAttribute('aria-hidden','false');render(p,images);void root.offsetWidth;root.classList.remove('out');root.classList.add('on');phase='entering';entranceAt=performance.now();timer=setTimeout(function(){timer=0;if(current(v,t))waitForPaint(v,t);},update||motionReduced()?0:200);}).catch(function(error){fail(v,t,String(error&&error.message||error));});});
  }
  function hide(t){
    if(!validToken(t)||t!==token)return false;if(phase==='exiting')return true;
    stop();ready=false;phase='exiting';root.classList.remove('on');root.classList.add('out');var v=version;
    timer=setTimeout(function(){timer=0;if(version===v&&token===t)cancel();},motionReduced()?0:180);return true;
  }
  window.CBSBreakBoard={show:show,hide:hide,cancel:cancel,painted:painted,snapshot:function(){return {active:!!token,token:token,kind:payload?payload.kind:'',phase:phase,ready:ready,painted:painted(token),failure:failure,payload:payload?JSON.parse(JSON.stringify(payload)):null,anchor:{x:96,y:400,width:660,height:463},reference:'User-supplied legacy CBS cropped break scoreboard; screen anchor and 200/180ms motion are adaptations, not reference measurements.'};}};
})();
