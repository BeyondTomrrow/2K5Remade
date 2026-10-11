/* NBC broadcast opens, timeout scores, player profiles and the corner bug.
 * Supplied game data owns every identity and value. No media or biographical
 * facts are guessed. Ordinary compact stat inserts remain a separate family. */
(function () {
  'use strict';
  var api = window.NFL2K5, canvas = document.getElementById('canvas');
  if (!api || !canvas) return;
  var latest = api.state, current = null, timer = 0, profileTimers = [], watermarkTimer = 0, matchKey = '', opened = false, pendingIntro = null, replay = false;
  var ALIAS = {STL:'LAR',SD:'LAC',OAK:'LV',JAC:'JAX',WSH:'WAS'};
  var COLORS = {LAR:'#1f4fae',CIN:'#da541b',ARI:'#97233f',ATL:'#a71930',BAL:'#3b2a8f',BUF:'#00338d',CAR:'#0085ca',CHI:'#c83803',CLE:'#ff3c00',DAL:'#003594',DEN:'#fb4f14',DET:'#0076b6',GB:'#2f5a3e',HOU:'#a71930',IND:'#0b4a8f',JAX:'#006778',KC:'#e31837',LV:'#6b6f74',LAC:'#0080c6',MIA:'#008e97',MIN:'#4f2683',NE:'#0c2f5e',NO:'#9f8958',NYG:'#0b2265',NYJ:'#125740',PHI:'#004c54',PIT:'#d9a300',SF:'#aa0000',SEA:'#1d4f91',TB:'#d50a0a',TEN:'#4b92db',WAS:'#7a1a1a'};
  function node(tag, cls, parent, value) { var n = document.createElement(tag);if(cls)n.className=cls;if(value!=null)n.textContent=String(value);if(parent)parent.appendChild(n);return n; }
  function img(cls,parent) { var n=node('img',cls,parent);n.setAttribute('alt','');return n; }
  function value(v) { return v != null && (typeof v === 'string' && v.trim() !== '' || typeof v === 'number' && isFinite(v)); }
  function code(t) { var c=String(t&&t.abbreviation||'').toUpperCase();return ALIAS[c]||c; }
  function color(t) { return t&&t.primaryColor||COLORS[code(t)]||'#354554'; }
  function rgb(t) { var c=color(t).replace('#','');return /^[a-f\d]{6}$/i.test(c)?[parseInt(c.slice(0,2),16),parseInt(c.slice(2,4),16),parseInt(c.slice(4,6),16)].join(','):'53,69,84'; }
  function logo(t) { return code(t)?'assets/logos/'+code(t)+'.png':t&&t.logo||''; }
  function setImage(n,src,fallback) { n.onerror=function(){if(fallback&&n.getAttribute('src')!==fallback){n.setAttribute('src',fallback);return;}n.style.visibility='hidden';n.removeAttribute('src');};n.style.visibility=src?'':'hidden';if(src)n.setAttribute('src',src);else n.removeAttribute('src'); }
  function teamImage(n,t) { setImage(n,logo(t),t&&t.logo); }
  function activeMatch() { var s=latest,cx=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&!cx.playSelection&&!cx.paused); }
  function pregameStage() { return latest&&latest.context&&latest.context.pregameStage||''; }
  function pregameWindow() {
    if(!activeMatch()||latest.phase!==0||!Number.isInteger(latest.quarter)||latest.quarter<0||latest.quarter>1||latest.gameStatus==='final')return false;
    var length=Number(latest.periodLength),clock=Number(latest.gameClock);
    return Number.isFinite(length)&&length>0&&Number.isFinite(clock)&&clock>=length&&clock<=length+.5;
  }
  function introWindow() { return pregameWindow()&&['warmup','panning'].indexOf(pregameStage())>=0; }
  function notify() { if(document.dispatchEvent&&typeof CustomEvent==='function')document.dispatchEvent(new CustomEvent('snf-broadcast-change',{detail:{active:current}})); }
  function time(sec) { if(!value(sec)||Number(sec)<0)return '';var n=Math.ceil(Number(sec));return Math.floor(n/60)+':'+('0'+n%60).slice(-2); }
  function period(s) { var q=Number(s&&s.quarter);return q>=1&&q<=4?q+['','ST','ND','RD','TH'][q]:q>4?'OT':''; }
  function brand(parent,cls) { var n=node('div',cls,parent);var p=img('nbc-brand',n);p.setAttribute('src','assets/branding/nbc-peacock-user.png');var mark=img('snf-wordmark',n);mark.setAttribute('alt','SNF');mark.setAttribute('src','assets/branding/snf-wordmark.svg');return n; }
  var watermark=brand(canvas,'snf-corner');watermark.setAttribute('id','snf-watermark');watermark.setAttribute('aria-hidden','true');
  function updateWatermark() { var on=activeMatch()&&current!=='intro'&&!replay;watermark.classList.toggle('on',on);watermark.setAttribute('aria-hidden',String(!on)); }
  function reset() { clearTimeout(timer);timer=0;profileTimers.forEach(clearTimeout);profileTimers=[];profileRows.classList.remove('swap');[intro,timeout,profile].forEach(function(n){n.classList.remove('on');n.setAttribute('aria-hidden','true');});if(current!==null){current=null;notify();}updateWatermark(); }
  function show(kind,element,e,fallback) { clearTimeout(timer);current=kind;element.classList.remove('on');void element.offsetWidth;element.classList.add('on');element.setAttribute('aria-hidden','false');updateWatermark();notify();var n=Number(e.duration||e.displaySeconds);timer=setTimeout(reset,isFinite(n)&&n>0?Math.min(n,30)*1000:fallback); }

  var intro=node('div','snf-broadcast-scene',canvas);intro.setAttribute('id','snf-intro');intro.setAttribute('aria-hidden','true');
  node('div','intro-shadow',intro);node('div','intro-silver left',intro);node('div','intro-silver right',intro);
  var introBrand=brand(intro,'intro-brand');introBrand.querySelector('.snf-wordmark').setAttribute('src','assets/branding/sunday-night-football-wordmark.svg');img('intro-nfl',introBrand).setAttribute('src','assets/branding/nfl-shield.svg');var introTitle=node('div','intro-title',intro);
  var introTeams=['away','home'].map(function(side){
    var n=node('div','intro-team '+side,intro),logoNode=img('intro-logo',n),players=node('div','intro-players',n),playerNode=node('div','intro-player',players);
    var portrait=img('intro-player-portrait',playerNode),playerName=node('div','intro-player-name',playerNode),position=node('div','intro-player-position',playerNode),caption=node('div','intro-team-caption',n);
    return {root:n,logo:logoNode,portrait:portrait,playerName:playerName,position:position,city:node('span','intro-city',caption),name:node('span','intro-name',caption),record:node('div','intro-record',n)};
  });
  var introVenue=node('div','intro-venue',intro);
  function bestPlayer(team,e,side) {
    var supplied=e.players&&e.players[side],players=Array.isArray(supplied)?supplied:Array.isArray(team.featuredPlayers)?team.featuredPlayers:Array.isArray(team.players)?team.players:[];
    return players.map(function(p,i){return {player:p,index:i};}).filter(function(entry){
      var p=entry.player;
      return p&&typeof p.fullName==='string'&&p.fullName.trim()&&typeof p.overall==='number'&&Number.isFinite(p.overall)&&p.overall>=0&&p.overall<=100&&(!p.teamAbbreviation||code({abbreviation:p.teamAbbreviation})===code(team));
    }).sort(function(a,b){return b.player.overall-a.player.overall||a.index-b.index;})[0];
  }
  function open(e) {
    if(!introWindow()||opened)return false;
    reset();
    introTeams.forEach(function(n,i){
      var side=i?'home':'away',t=latest[side],entry=bestPlayer(t,e,side),p=entry&&entry.player;
      intro.style.setProperty(i?'--intro-home':'--intro-away',color(t));teamImage(n.logo,t);
      n.city.textContent=String(t.city||'').toUpperCase();n.name.textContent=String(t.name||code(t)).toUpperCase();n.record.textContent=t.record||'';
      n.root.classList.toggle('no-player',!p);n.root.classList.toggle('no-photo',!p||!(p.render||p.portrait));
      n.root.classList.toggle('portrait-only',!!p&&!p.render&&p.portraitType!=='bust');
      setImage(n.portrait,p&&(p.render||p.portrait)||'');n.playerName.textContent=p?p.fullName.toUpperCase():'';
      n.position.textContent=p?[p.position,Number.isInteger(p.number)&&p.number>=0&&p.number<=99?p.number:null].filter(value).join('  '):'';
    });
    introTitle.textContent=e.title||'';introVenue.textContent=e.venue||latest.venue||latest.stadiumName||'';
    intro.classList.toggle('no-venue',!introVenue.textContent);opened=true;pendingIntro=null;show('intro',intro,e,6000);return true;
  }
  function requestIntro(e) {
    if(opened||!pregameWindow()||['crowd','warmup','panning'].indexOf(pregameStage())<0)return;
    pendingIntro=e; if(introWindow())open(e);
  }

  var timeout=node('div','snf-broadcast-scene',canvas);timeout.setAttribute('id','snf-timeout');timeout.setAttribute('aria-hidden','true');
  node('div','timeout-glass',timeout);node('div','timeout-curve',timeout);brand(timeout,'timeout-brand');
  var timeoutTitle=node('div','timeout-title',timeout),timeoutClock=node('div','timeout-clock',timeout);
  var timeoutTeams=['away','home'].map(function(side){var n=node('div','timeout-team '+side,timeout);return {root:n,logo:img('timeout-logo',n),name:node('div','timeout-name',n),score:node('div','timeout-score',n),marks:node('div','timeout-marks',n)};});
  var timeoutDetail=node('div','timeout-detail',timeout);
  function updateTimeout() { if(!latest)return;timeoutTeams.forEach(function(n,i){var t=latest[i?'home':'away'];n.root.style.setProperty('--timeout-team',color(t));teamImage(n.logo,t);n.name.textContent=code(t);n.score.textContent=value(t.score)?String(t.score):'\u2014';n.marks.innerHTML='';if(Number.isInteger(t.timeouts)&&t.timeouts>=0&&t.timeouts<=3){for(var j=0;j<3;j++)node('i',j>=t.timeouts?'used':'',n.marks);}});timeoutClock.textContent=[period(latest),time(latest.gameClock)].filter(Boolean).join('  '); }
  function showTimeout(e) { if(!activeMatch())return;reset();updateTimeout();var t=latest[e.team];timeoutTitle.textContent='TIMEOUT';timeoutDetail.textContent=t?(t.city&&t.name?t.city+' '+t.name:code(t)).toUpperCase():'';show('timeout',timeout,e,5000); }

  var profile=node('div','snf-broadcast-scene',canvas);profile.setAttribute('id','snf-player-profile');profile.setAttribute('aria-hidden','true');
  node('div','profile-glass',profile);node('div','profile-flag',profile);node('div','profile-curve',profile);
  var profileLogo=img('profile-logo',profile),profilePortrait=img('profile-portrait',profile),profileTab=node('div','profile-position',profile),profileName=node('div','profile-name',profile),profileCaption=node('div','profile-caption',profile),profileRows=node('div','profile-rows',profile);brand(profile,'profile-brand');
  function profileData(p,e) { var rows=[],known={};function add(label,v){if(!value(v)||known[label])return;known[label]=true;rows.push({label:label,value:v});}
    add('HEIGHT',p.height);add('WEIGHT',p.weight);add('AGE',p.age);add('EXPERIENCE',p.experience!=null?p.experience:Number.isInteger(p.yearsPro)&&p.yearsPro>=0?p.yearsPro+' YEARS':null);add('COLLEGE',p.college);add('HOMETOWN',p.hometown);
    var supplied=e.rows||p.profile||[];if(Array.isArray(supplied))supplied.forEach(function(r){if(r&&value(r.label))add(String(r.label).toUpperCase(),r.value!=null?r.value:Array.isArray(r.values)?r.values[0]:null);});
    if(Array.isArray(p.fields))p.fields.forEach(function(r){if(r&&value(r.label))add(String(r.label).toUpperCase(),r.value);});return rows.slice(0,24); }
  function showProfile(e) { reset();if(!activeMatch()||!(latest.context&&latest.context.scorebugVisible===true)||!e.player)return;var p=Object.assign({},e.player),name=p.fullName||p.name;if(!value(name))return;
    if(p.fullName&&p.name&&String(p.fullName).trim().split(/\s+/).pop().toLowerCase()!==String(p.name).trim().split(/\s+/).pop().toLowerCase()){name=p.name;p.portrait='';p.render='';p.number=null;p.position='';}
    var t=e.teamData||latest[e.team];profile.style.setProperty('--profile-team',color(t));profile.style.setProperty('--profile-rgb',rgb(t));teamImage(profileLogo,t);setImage(profilePortrait,p.render||p.portrait||'');profile.classList.toggle('no-photo',!(p.render||p.portrait));profileName.textContent=String(name).toUpperCase();profileTab.textContent=[p.position,Number.isInteger(p.number)&&p.number>=0&&p.number<=99?p.number:null].filter(value).join('\n');
    var rows=profileData(p,e),pages=Math.max(1,Math.ceil(rows.length/8)),caption=e.caption||e.title||'PLAYER PROFILE';
    function page(index) { profileCaption.textContent=caption+(pages>1?'  \u00b7  '+(index+1)+' / '+pages:'');profileRows.innerHTML='';rows.slice(index*8,index*8+8).forEach(function(r,i){var row=node('div','profile-row',profileRows);row.style.setProperty('--row',i);node('span','',row,r.label);node('strong','',row,r.value);}); }
    page(0);profile.classList.toggle('identity-only',!rows.length);var suppliedSeconds=Number(e.duration||e.displaySeconds),total=isFinite(suppliedSeconds)&&suppliedSeconds>0?Math.min(suppliedSeconds,30)*1000:Math.max(7500,pages*4000);
    show('profile',profile,e,total);for(var i=1;i<pages;i++)(function(index){profileTimers.push(setTimeout(function(){profileRows.classList.add('swap');profileTimers.push(setTimeout(function(){page(index);profileRows.classList.remove('swap');},180));},index*total/pages));})(i); }

  window.SNFBroadcastScenes={active:function(){return current;},reset:reset};
  api.onState(function(s,prev){
    latest=s;var key=s.away&&s.home?String(s.matchId||'')+'|'+code(s.away)+'|'+code(s.home):'';
    var changed=key&&matchKey&&key!==matchKey||prev&&s.quarter>0&&s.quarter<prev.quarter;
    if(!activeMatch()||changed){reset();pendingIntro=null;clearTimeout(watermarkTimer);replay=false;if(changed||!s.valid)opened=false;else opened=true;}
    if(key)matchKey=key;
    if(!pregameWindow()||['coin_toss','kickoff'].indexOf(pregameStage())>=0){pendingIntro=null;if(activeMatch())opened=true;if(current==='intro')reset();}
    if(current==='intro'&&!introWindow())reset();
    if(introWindow()&&!opened&&!(window.SNFPregameInserts&&window.SNFPregameInserts.active()))open(pendingIntro||{});
    if(current==='timeout')updateTimeout();updateWatermark();
  });
  document.addEventListener('snf-pregame-change',function(){if(window.SNFPregameInserts&&window.SNFPregameInserts.active()){pendingIntro=null;opened=true;reset();}});
  api.onEvent(function(e){if(!e)return;
    if(e.name==='MATCHUP_INTRO'||e.name==='BROADCAST_INTRO'||e.name==='PREGAME_INTRO_READY'||e.name==='GAME_STARTED'){requestIntro(e);return;}
    if(e.name==='COIN_TOSS'||e.name==='PLAYER_FACT'||e.name==='PLAY_STARTED'){pendingIntro=null;reset();return;}
    if(e.name==='TIMEOUT'){showTimeout(e);return;}
    if(e.name==='PLAYER_PROFILE'||e.name==='PLAYER_BIO'){showProfile(e);return;}
    if(e.name==='REPLAY_STARTED'||e.name==='REPLAY_ENDED'){reset();if(activeMatch()){replay=true;clearTimeout(watermarkTimer);updateWatermark();watermarkTimer=setTimeout(function(){replay=false;updateWatermark();},1100);}return;}
    if(['LINEUP','PLAYER_STAT','PLAYER_FEATURE','PLAYER_COMPARISON','TEAM_STATS','TOUCHDOWN','FIELD_GOAL','SAFETY','EXTRA_POINT','TWO_POINT_CONVERSION','GAME_ENDED','QUARTER_ENDED'].indexOf(e.name)>=0)reset();
  });
})();
