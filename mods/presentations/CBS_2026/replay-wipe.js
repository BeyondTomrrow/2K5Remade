/* Observed CBS replay directions, not one animation played backwards.
 * Official NFL Chiefs/Cowboys full game, PLR04ljZ5DI:
 * replay-in 94.42–95.43s; replay-out 107.43–108.43s.
 * Editable layers reproduce measured motion; original 3D source meshes are
 * unavailable. Existing CBS/NFL vector assets remain replaceable images.
 */
(function () {
  'use strict';
  var canvas = document.getElementById('canvas');
  if (!canvas) return;
  var root = document.createElement('div');
  root.id = 'cbs-replay-wipe'; root.hidden = true;
  root.setAttribute('aria-hidden', 'true');
  root.innerHTML = '<div class="cr-lens"></div><div class="cr-return-cover"></div>' +
    '<svg class="cr-orbits" viewBox="0 0 1920 1080" aria-hidden="true"><circle cx="960" cy="540" r="360"/><path d="M210 540 Q960 -330 1710 540 Q960 1410 210 540Z"/></svg>' +
    '<div class="cr-shield"><img class="cr-shield-depth" src="assets/branding/nfl-shield.svg" alt=""><img class="cr-shield-face" src="assets/branding/nfl-shield.svg" alt=""></div>' +
    '<div class="cr-wordmark"><div class="cr-word-depth"></div><div class="cr-word-face"></div>' +
    '<svg class="cr-word-outline" viewBox="0 0 217.666667 24" aria-hidden="true"><defs><filter id="cr-contour" x="-3%" y="-15%" width="106%" height="130%"><feMorphology in="SourceAlpha" operator="dilate" radius=".12" result="wide"/><feComposite in="wide" in2="SourceAlpha" operator="out" result="edge"/><feFlood flood-color="#e4f5ff"/><feComposite in2="edge" operator="in"/></filter></defs><image href="assets/branding/cbs-sports.svg" width="217.666667" height="24" filter="url(#cr-contour)"/></svg></div>';
  canvas.appendChild(root);
  var generation=0, timers=[], running=false, direction='', controlledAnimations=[];
  var reference='CBS Chiefs/Cowboys Thanksgiving 2025, official NFL full game PLR04ljZ5DI; in94.42–95.43s, out107.43–108.43s';
  function report(phase,status){root.setAttribute('data-phase',phase);if(window.CBSMotion)window.CBSMotion.report('replay-wipe',phase,status,reference);}
  function allowed(s){var c=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&!c.paused&&!c.playSelection);}
  function reduced(){return window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches&&document.documentElement.getAttribute('data-preview-motion')!=='full';}
  function cancel(){generation++;timers.forEach(clearTimeout);timers=[];controlledAnimations=[];running=false;direction='';root.hidden=true;root.className='';root.removeAttribute('data-controlled-time-ms');root.setAttribute('data-phase','idle');}
  function later(ms,fn){var token=generation;timers.push(setTimeout(function(){if(token===generation)fn();},ms));}
  function handleEvent(e,state){
    if(!e||(e.name!=='REPLAY_STARTED'&&e.name!=='REPLAY_ENDED'))return false;
    cancel();
    if(!allowed(state)){report('suppressed','suppressed');return true;}
    if(reduced()){report('settled','complete');return true;}
    direction=e.name==='REPLAY_STARTED'?'in':'out';running=true;root.hidden=false;
    void root.offsetWidth;root.className='running replay-'+direction;
    report(direction==='in'?'shield-emergence':'wordmark-cover','running');
    if(direction==='in'){
      later(180,function(){report('shield-eye-orbits','running');});
      later(600,function(){report('shield-approach','running');});
      later(800,function(){report('shield-cover','running');});
      later(880,function(){report('replay-reveal','running');});
    }else{
      later(130,function(){report('wordmark-resolve','running');});
      later(300,function(){report('eye-orbits-expand','running');});
      later(690,function(){report('wordmark-contour','running');});
    }
    later(1050,function(){cancel();report('settled','complete');});
    return true;
  }
  // Native ownership drives this same editable animation explicitly. It can
  // hold a painted cover while the guest performs its actual camera handoff;
  // this path never starts wall-clock lifecycle or cancellation timers.
  function beginControlled(nextDirection){
    cancel();
    if(nextDirection!=='in'&&nextDirection!=='out')return false;
    if(reduced()||!root.getAnimations)return false;
    direction=nextDirection;running=true;root.hidden=false;
    void root.offsetWidth;root.className='running replay-'+direction;
    void root.offsetWidth;
    controlledAnimations=root.getAnimations({subtree:true});
    if(!controlledAnimations.length){cancel();return false;}
    controlledAnimations.forEach(function(animation){animation.pause();animation.currentTime=0;});
    root.setAttribute('data-controlled-time-ms','0');
    report(direction==='in'?'shield-emergence':'wordmark-cover','running');
    return true;
  }
  function seekControlled(ms){
    if(!running||!controlledAnimations.length||typeof ms!=='number'||!isFinite(ms))return false;
    var time=Math.max(0,Math.min(1050,ms));
    controlledAnimations.forEach(function(animation){animation.pause();animation.currentTime=time;});
    root.setAttribute('data-controlled-time-ms',String(time));
    return true;
  }
  window.CBSReplayWipe={handleEvent:handleEvent,cancel:cancel,beginControlled:beginControlled,seekControlled:seekControlled,isCovering:function(){return running;},duration:1050,direction:function(){return direction;}};
})();
