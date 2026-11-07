/* CBS compact Eyebar, Presentation API v1. All values belong to the game.
   Reference geometry is separate from the illustrative preview fixtures. */
(function () {
  'use strict';
  var api=window.NFL2K5,bug=document.getElementById('eyebar');
  if(!api||!bug)return;
  var $=function(s){return bug.querySelector(s);},latest=null,visible=false,key='',entryTimer=0,flagTimer=0,messageTimer=0,flagUntil=0,message='';
  var band=$('.top-band'),down=document.getElementById('down'),quarter=document.getElementById('quarter'),gameClock=document.getElementById('game-clock'),playClock=document.getElementById('play-clock'),network=document.getElementById('network');
  var aliases={STL:'LAR',SD:'LAC',OAK:'LV',JAC:'JAX',WSH:'WAS'};
  var colors={ARI:'#970d2b',ATL:'#a71930',BAL:'#241773',BUF:'#00338d',CAR:'#0085ca',CHI:'#0b162a',CIN:'#fb4f14',CLE:'#311d00',DAL:'#002c5f',DEN:'#fb4f14',DET:'#0076b6',GB:'#174535',HOU:'#03202f',IND:'#002c5f',JAX:'#006778',KC:'#b90c22',LV:'#25272a',LAC:'#0080c6',LAR:'#003594',MIA:'#008e97',MIN:'#4f2683',NE:'#002244',NO:'#39342d',NYG:'#123e97',NYJ:'#006c54',PHI:'#004c54',PIT:'#242426',SF:'#aa0000',SEA:'#002244',TB:'#9c1222',TEN:'#0c2340',WAS:'#5a1414'};
  function code(t){var c=String(t&&t.abbreviation||'').toUpperCase();return aliases[c]||c;}
  function integer(v){return (typeof v==='number'||typeof v==='string'&&v.trim()!=='')&&Number.isFinite(Number(v))&&Math.floor(Number(v))===Number(v)?Number(v):null;}
  function score(v){var n=integer(v);return n!=null&&n>=0?String(n):'';}
  function clock(v){if(v==null||v==='')return '';var n=Number(v);if(!Number.isFinite(n)||n<0)return '';n=Math.ceil(n);return Math.floor(n/60)+':'+('0'+n%60).slice(-2);}
  function periodBoard(){return window.CBSPeriodBoard?CBSPeriodBoard.activeQuarter():0;}
  function permitted(s){var cx=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&(cx.scorebugVisible===true||periodBoard())&&!cx.paused&&!cx.playSelection&&!cx.replayActive);}
  function setText(n,text){text=String(text);if(n.textContent!==text)n.textContent=text;}
  function image(n,t,parent){
    var c=code(t),local=/^[A-Z0-9]{2,4}$/.test(c)?'assets/logos/'+c+'.png':'',src=local||t.logo||'';
    if(n.getAttribute('data-team-src')===src)return;
    n.setAttribute('data-team-src',src);parent.classList.remove('no-logo');
    n.onerror=function(){if(t.logo&&n.getAttribute('src')!==t.logo){n.setAttribute('src',t.logo);}else{n.onerror=null;n.removeAttribute('src');parent.classList.add('no-logo');}};
    if(src)n.setAttribute('src',src);else{n.removeAttribute('src');parent.classList.add('no-logo');}
  }
  function team(side,t,prev){
    var el=$('.team.'+side),c=code(t),color=typeof t.primaryColor==='string'&&/^#[\da-f]{6}$/i.test(t.primaryColor)?t.primaryColor:colors[c]||'#303842';
    bug.style.setProperty('--'+side,color);image(el.querySelector('.logo'),t,el);image($('.opening-logo.'+(side==='away'?'away-logo':'home-logo')),t,bug);
    el.querySelector('.logo').setAttribute('alt',((t.city||'')+' '+(t.name||c)).trim());
    setText(el.querySelector('.team-fallback'),c);
    var n=el.querySelector('.score'),text=score(t.score);if(window.CBSMotion)text=CBSMotion.scoreText(side,text);setText(n,text);n.classList.toggle('three-digits',text.length>2&&text[0]!=='+');
    setText(el.querySelector('.record'),typeof t.record==='string'?t.record:'');
    var count=integer(t.timeouts),marks=el.querySelectorAll('.timeouts i');
    marks.forEach(function(mark,i){mark.hidden=count==null||count<0||count>3;mark.classList.toggle('used',count!=null&&i>=count);});
    el.querySelector('.timeouts').setAttribute('aria-label',count!=null&&count>=0&&count<=3?count+' timeouts remaining':'Timeout count unavailable');
    el.classList.toggle('has-ball',latest.possession===side);
  }
  function ordinal(n,parent){
    parent.textContent='';parent.appendChild(document.createTextNode(String(n)));
    var small=document.createElement('small');small.textContent=['','ST','ND','RD','TH'][n]||'';parent.appendChild(small);
  }
  function center(){
    if(!latest)return;
    var possession=latest.possession;
    band.style.setProperty('--possession-color',possession==='away'||possession==='home'?bug.style.getPropertyValue('--'+possession):'#123e97');
    var text=String(latest.downDistanceText||'').toUpperCase(),liveFlag=latest.context&&latest.context.flag,flag=typeof liveFlag==='boolean'?liveFlag:flagUntil>Date.now();
    var boardInfo=window.CBSPeriodBoard&&CBSPeriodBoard.presentation?CBSPeriodBoard.presentation():null,boardQuarter=periodBoard(),q=boardQuarter||integer(latest.quarter),ending=!!boardQuarter||latest.context&&latest.context.periodEnding===true&&(q===1||q===3)&&latest.gameStatus!=='final';
    var branded=!message&&!flag&&(ending||latest.gameStatus==='final'||latest.phase===1||latest.phase===2||/^(KICKOFF|PUNT|FREE KICK)$/.test(text)||!text);
    band.classList.toggle('branding',branded);band.classList.toggle('flag',!!flag);
    band.classList.toggle('message',!!message);down.textContent='';
    if(flag)setText(down,'FLAG');else if(message)setText(down,message);else if(!branded){
      var m=/^([1-4])(?:ST|ND|RD|TH)\s*&\s*(.+)$/.exec(text);
      if(m){ordinal(+m[1],down);down.appendChild(document.createTextNode(' & '+m[2]));}else setText(down,text);
    }
    bug.classList.toggle('period-ending',ending);
    if(boardInfo){setText(quarter,boardInfo.displayCaption);}
    else if(ending){quarter.textContent='END ';var word=document.createElement('span');ordinal(q,word);quarter.appendChild(word);quarter.appendChild(document.createTextNode(' QTR'));}
    else if(latest.gameStatus==='final'){setText(quarter,'FINAL');}else if(q>=1&&q<=4){ordinal(q,quarter);}else setText(quarter,q>4?'OT':'');
    setText(gameClock,clock(latest.gameClock));
    var pc=integer(latest.playClock),hasClock=pc!=null&&pc>=0&&pc<=99&&!ending&&latest.gameStatus!=='final'&&!message;
    setText(playClock,hasClock?pc:'');playClock.classList.toggle('on',hasClock);playClock.classList.toggle('low',hasClock&&pc<=5);
  }
  function enter(){
    if(!visible)return;
    clearTimeout(entryTimer);bug.classList.remove('entering');void bug.offsetWidth;bug.classList.add('entering');
    var reduced=window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches&&document.documentElement.getAttribute('data-preview-motion')!=='full';
    if(window.CBSMotion)CBSMotion.report('entry','eye-and-logo-travel','running','CBS PIT/NYJ September 2025 entry GIF, 0–1.8s');
    entryTimer=setTimeout(function(){bug.classList.remove('entering');entryTimer=0;if(window.CBSMotion)CBSMotion.report('entry','settled','complete','CBS PIT/NYJ September 2025 entry GIF, 0–1.8s');},reduced?0:1850);
  }
  function cancel(){
    clearTimeout(entryTimer);clearTimeout(flagTimer);clearTimeout(messageTimer);entryTimer=flagTimer=messageTimer=0;flagUntil=0;message='';bug.classList.remove('entering');
    ['away','home'].forEach(function(side){$('.team.'+side+' .score').classList.remove('changed');});
  }
  function renderState(s,prev){
    // The game's explicit Presentation > Animations choice overrides the
    // desktop's window-animation preference. Preview fixtures without that
    // setting continue to respect prefers-reduced-motion.
    if(s.context&&typeof s.context.animationsEnabled==='boolean')document.documentElement.setAttribute('data-preview-motion',s.context.animationsEnabled?'full':'reduced');
    var nextKey=s.away&&s.home?String(s.matchId||'')+'|'+code(s.away)+'|'+code(s.home):'',changed=key&&nextKey&&key!==nextKey,was=visible;
    latest=s;if(window.CBSNativeReplay)CBSNativeReplay.updateState(s,prev);if(window.CBSPregame)CBSPregame.updateState(s,prev);if(window.CBSCameraMark)CBSCameraMark.updateState(s,prev);if(window.CBSReviewCard)CBSReviewCard.updateState(s,prev);if(window.CBSPeriodBoard)CBSPeriodBoard.updateState(s,prev);
    visible=permitted(s);if(!visible||changed)cancel();
    if(window.CBSStats)CBSStats.updateState(s,prev);
    if(window.CBSMotion&&!periodBoard())CBSMotion.updateState(s,prev);
    var cx=s.context||{},networkVisible=(visible||!!(s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&cx.replayActive===true&&!cx.paused&&!cx.playSelection))&&!cx.cornerClaimed;
    bug.classList.toggle('hidden',!visible);bug.classList.toggle('final',s.gameStatus==='final'||!!(window.CBSPeriodBoard&&CBSPeriodBoard.presentation&&CBSPeriodBoard.presentation()&&CBSPeriodBoard.presentation().presentationKind==='final'));bug.setAttribute('aria-hidden',String(!visible));network.classList.toggle('on',networkVisible);network.setAttribute('aria-hidden',String(!networkVisible));
    if(nextKey)key=nextKey;
    if(!s.away||!s.home)return;
    team('away',s.away,!changed&&prev&&prev.away);team('home',s.home,!changed&&prev&&prev.home);center();
    if(visible&&!periodBoard()&&(!was||changed)&&(!window.CBSMotion||!CBSMotion.snapshot().name)&&(!window.CBSStats||!CBSStats.snapshot().name))enter();
  }
  api.onState(renderState);
  api.onEvent(function(e){
    if(window.CBSNativeReplay&&CBSNativeReplay.handleEvent(e))return;
    if(window.CBSPregame&&CBSPregame.handleEvent(e))return;
    if(window.CBSCameraMark&&CBSCameraMark.handleEvent(e))return;
    if(window.CBSReviewCard&&CBSReviewCard.handleEvent(e))return;
    if(window.CBSPeriodBoard&&CBSPeriodBoard.handleEvent(e))return;
    if(window.CBSStats&&CBSStats.handleEvent(e))return;
    if(window.CBSMotion&&CBSMotion.handleEvent(e))return;
    if(!e||!visible)return;
    if(e.name==='GAME_STARTED'){if(window.CBSMotion)CBSMotion.cancel('return');enter();return;}
    if(e.name==='PENALTY'){clearTimeout(flagTimer);flagUntil=Date.now()+8000;center();flagTimer=setTimeout(function(){flagUntil=0;center();},8000);return;}
    if(e.name==='QUARTER_STARTED'||e.name==='PLAY_STARTED'){clearTimeout(messageTimer);message='';center();}
  });
  window.addEventListener('cbs-motion-change',function(){if(!latest||!latest.away||!latest.home)return;['away','home'].forEach(function(side){var n=$('.team.'+side+' .score'),text=score(latest[side].score);if(window.CBSMotion)text=CBSMotion.scoreText(side,text);setText(n,text);n.classList.toggle('three-digits',text.length>2&&text[0]!=='+');});center();});
  function resize(){document.documentElement.style.setProperty('--pad',Math.max(0,(window.innerWidth-1920)/2)+'px');}
  window.addEventListener('resize',resize);resize();
  window.CBSScorebug={visible:function(){return visible;},refresh:function(){if(latest)renderState(latest,latest);},reset:cancel,stopEntry:function(){clearTimeout(entryTimer);entryTimer=0;bug.classList.remove('entering');}};
})();
