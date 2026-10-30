/* Browser-only controls. They send Presentation API messages to the package;
   they never connect to, launch, or control the native game. */
(function () {
  'use strict';
  var frame=document.getElementById('preview'),stage=document.getElementById('stage');
  var scene='live',paused=false,ready=false;
  var teams={DAL:{city:'Dallas',name:'Cowboys'},NE:{city:'New England',name:'Patriots'},LV:{city:'Las Vegas',name:'Raiders'},PIT:{city:'Pittsburgh',name:'Steelers'},NYJ:{city:'New York',name:'Jets'},ARI:{city:'Arizona',name:'Cardinals'},NYG:{city:'New York',name:'Giants'}};
  var titles={live:'Live scrimmage',kickoff:'Kickoff · CBS Sports',flag:'Flag', 'low-clock':'Low play clock',timeout:'Timeout',final:'Final score'};
  var descriptions={live:'Live down and distance with the game clock, play clock, possession and timeout indicators.',kickoff:'The center band switches to CBS Sports during a kickoff. It returns to down and distance for scrimmage plays.',flag:'The yellow FLAG band follows the live flag state. An event-only flag clears after eight seconds; a live state flag persists until the game clears it.', 'low-clock':'The play clock enters its warning state at five seconds or fewer.',timeout:'Timeout marks remain tied to the live count. A dedicated CBS timeout animation is not substituted without an inspected motion reference.',final:'The quarter label becomes FINAL and the play clock clears. The selected teams and scores remain live data fields.'};
  var motionTitles={touchdown:'Touchdown event', 'score-update':'One-point score update', 'replay-in':'Replay-in event', 'replay-out':'Replay-out event', 'period-end':'End-of-quarter event',halftime:'Halftime event'};
  var motionDescriptions={touchdown:'Adds six sample points to the side selected under Possession, then sends TOUCHDOWN. No player identity or portrait is fabricated. The package reports whether a reference-verified sequence is available.', 'score-update':'Adds one sample point to the selected side and sends SCORE_CHANGED with the new score. This is a numeric update fixture, not a fabricated touchdown or player event.', 'replay-in':'Sends REPLAY_STARTED. The current native source derives replay entry from popup text; this browser fixture proves event response only.', 'replay-out':'Sends REPLAY_ENDED. A reliable native replay-end producer has not been confirmed, so browser playback does not prove an automatic in-game return.', 'period-end':'Sends QUARTER_ENDED for the selected period at 0:00. The native bridge currently derives period events on period increment, not the first 0:00 frame.',halftime:'Sets the second-quarter sample clock to 0:00 and sends HALFTIME. Native quarter-ended, halftime and quarter-start events may arrive together; this isolates the halftime response.'};
  motionTitles['reference-wipe']='CBS halftime report exit wipe';
  motionDescriptions['period-end']='Sets the sample period-ending state at 0:00. Q1/Q3 use the observed CBS END 1st/3rd QTR Eyebar label. Native state arrives from the actual title flow; the separate period-increment event is not the timing source. No unverified quarter bumper is substituted.';
  motionDescriptions['reference-wipe']='Reconstructs the red/blue eye exit observed in the 2026 AFC halftime report. This reference control does not claim that the same transition is used for replay or every quarter break.';
  function el(id){return document.getElementById(id);}
  function send(payload){if(ready&&frame.contentWindow)frame.contentWindow.postMessage({nfl2k5:payload},'*');}
  function event(name,extra){send({type:'event',event:Object.assign({name:name,team:el('possession').value||'away'},extra||{})});}
  function number(id,min,max){var field=el(id),value=field.value.trim(),n=Number(value);return value!==''&&Number.isFinite(n)&&Number.isInteger(n)&&n>=min&&n<=max?n:null;}
  function team(side){var code=el(side+'-team').value,t=teams[code];return {abbreviation:code,city:t.city,name:t.name,logo:'assets/logos/'+code+'.png',score:number(side+'-score',0,999),timeouts:number(side+'-timeouts',0,3),record:''};}
  function clock(){var match=/^(\d{1,3}):([0-5]\d)$/.exec(el('game-clock').value.trim());return match?Number(match[1])*60+Number(match[2]):null;}
  function state(){
    var down=number('down',1,4),distance=number('distance',1,99),kickoff=scene==='kickoff';
    return {valid:true,gameStatus:scene==='final'?'final':'in_progress',matchId:'CBS-GALLERY',quarter:number('quarter',1,5),gameClock:scene==='final'?0:clock(),playClock:kickoff||scene==='final'?-1:number('play-clock',0,99),phase:kickoff?2:4,down:down,distance:distance,possession:el('possession').value||null,downDistanceText:kickoff?'KICKOFF':down&&distance?down+['','ST','ND','RD','TH'][down]+' & '+distance:'',away:team('away'),home:team('home'),context:{scorebugVisible:!paused,paused:paused,playSelection:paused,flag:scene==='flag'}};
  }
  function publish(){var sample=state();sample.context.periodEnding=scene==='period-end';send({type:'state',state:sample});}
  var statSample=null;
  var statFields=[
    [['completions','Completions'],['attempts','Attempts'],['passingYards','Passing yards',-1000],['passingTouchdowns','Pass TD'],['interceptions','Interceptions']],
    [['carries','Carries'],['rushingYards','Rush yards',-1000],['rushingTouchdowns','Rush TD']],
    [['receptions','Receptions'],['receivingYards','Receive yards',-1000],['receivingTouchdowns','Receive TD']],
    [['tackles','Tackles',0,.5],['sacks','Sacks',0,.5],['defensiveInterceptions','Interceptions']],
    [['fieldGoalsMade','Field goals made'],['fieldGoalsAttempted','Attempts'],['longestFieldGoal','Longest FG']]
  ];
  function seedStats(){
    var side=el('stat-side').value,code=el(side+'-team').value,k=el('stat-kind').value;
    statSample=CBSStatFixtures.player(k,code,side);if(!statSample)return;
    el('stat-name').value=statSample.player.name;el('stat-number').value=statSample.player.number==null?'':statSample.player.number;
    el('stat-fields').textContent='';statFields[statSample.player.kind].forEach(function(spec){
      var label=document.createElement('label'),input=document.createElement('input');label.textContent=spec[1];
      input.type='number';input.id='stat-'+spec[0];input.min=String(spec[2]||0);input.max='100000';input.step=String(spec[3]||1);input.value=statSample.player[spec[0]];label.htmlFor=input.id;label.appendChild(input);el('stat-fields').appendChild(label);
    });
  }
  function editedStats(){
    if(!statSample)return null;var e=JSON.parse(JSON.stringify(statSample)),k=e.player.kind;
    e.player.name=el('stat-name').value.trim();e.player.fullName=e.player.name;
    var numberValue=el('stat-number').value.trim();e.player.number=numberValue===''?null:Number(numberValue);
    statFields[k].forEach(function(spec){var value=el('stat-'+spec[0]).value.trim();e.player[spec[0]]=value===''?null:Number(value);});
    return e;
  }
  function statsGraphic(comparison){
    scene='live';publish();var payload=comparison?CBSStatFixtures.comparison(state()):editedStats();if(!payload)return;
    if(comparison&&Number(el('stat-kind').value)===0){
      var edited=editedStats(),index=el('stat-side').value==='away'?0:1,p=edited.player;
      p.portrait='';payload.players[index].player=p;payload.rows[0].values[index]=p.completions+'/'+p.attempts;payload.rows[1].values[index]=p.passingYards;payload.rows[2].values[index]=p.passingTouchdowns+' / '+p.interceptions;
    }
    el('scene-title').textContent=comparison?'CBS full-screen comparison':'CBS player stat insert';
    el('description').textContent=comparison?'Reference-based comparison layout with editable player identities and current sample statistics. This is a browser event fixture.':'Reference-based portrait/name/stat strip. Values come from the event, and the insert leaves after its requested display duration.';
    document.querySelectorAll('[data-scene]').forEach(function(button){button.setAttribute('aria-pressed','false');});
    send({type:'event',event:payload});stage.scrollIntoView({block:'start',behavior:'smooth'});
  }
  function extendedStats(name){
    scene='live';publish();
    var isDrive=name==='drive-native'||name==='drive-split';
    var payload=isDrive?CBSStatFixtures.drive(el('stat-side').value,name==='drive-split'):CBSStatFixtures.team(name);
    if(!payload)return;
    var labels={yardage:'Team yardage comparison',defense:'Team defensive comparison',possession:'Team possession comparison','drive-native':'Drive summary: native-compatible totals','drive-split':'Drive summary: sample rush/pass split'};
    el('scene-title').textContent=labels[name];
    el('description').textContent=isDrive?(name==='drive-native'?'SAMPLE DATA: six total plays, 47 yards and 3:17. Rush/pass counts are intentionally absent, matching the currently verified native summary fields.':'SAMPLE DATA: an optional five-rush/one-pass split. This fixture demonstrates the renderer; the current native summary does not supply this split.'):'SAMPLE DATA: both selected teams use the measured CBS comparison table. Team totals are independent event fields; missing values remain unavailable.';
    document.querySelectorAll('[data-scene]').forEach(function(button){button.setAttribute('aria-pressed','false');});
    send({type:'event',event:payload});stage.scrollIntoView({block:'start',behavior:'smooth'});
  }
  function status(){el('pause').setAttribute('aria-pressed',String(paused));el('visibility-status').textContent=paused?'Presentation hidden · pause / menu fixture':'Presentation visible';}
  function select(name){
    scene=name;paused=false;
    /* Clear transient events from the previous fixture before sending the next. */
    send({type:'state',state:{valid:false,gameStatus:'inactive',context:{scorebugVisible:false}}});
    if(name==='low-clock')el('play-clock').value='4';
    else if(el('play-clock').value==='4')el('play-clock').value='25';
    el('scene-title').textContent=titles[name];el('description').textContent=descriptions[name];
    document.querySelectorAll('[data-scene]').forEach(function(button){button.setAttribute('aria-pressed',String(button.getAttribute('data-scene')===name));});
    status();publish();if(name==='flag')event('PENALTY');if(name==='timeout')event('TIMEOUT');
  }
  function motion(name){
    var side=el('possession').value||'away',points=name==='touchdown'?6:name==='score-update'?1:0;
    scene=name;
    el('scene-title').textContent=motionTitles[name];el('description').textContent=motionDescriptions[name];
    document.querySelectorAll('[data-scene]').forEach(function(button){button.setAttribute('aria-pressed','false');});
    if(points){
      var previous=number(side+'-score',0,999);
      if(previous==null||previous+points>999){el('motion-title').textContent='Sample event not sent';el('motion-detail').textContent='Enter a valid score below '+(1000-points)+' for the selected side first.';el('motion-reference').textContent='';el('motion-report').setAttribute('data-status','unavailable');return;}
      el(side+'-score').value=String(previous+points);
    }
    if(name==='period-end'||name==='halftime')el('game-clock').value='0:00';
    if(name==='halftime')el('quarter').value='2';
    var quarter=number('quarter',1,5);
    publish();
    var eventNames={touchdown:'TOUCHDOWN','score-update':'SCORE_CHANGED','replay-in':'REPLAY_STARTED','replay-out':'REPLAY_ENDED','period-end':'QUARTER_ENDED',halftime:'HALFTIME'};
    eventNames['reference-wipe']='CBS_REFERENCE_WIPE';
    var extra=points?{team:side,points:points,score:number(side+'-score',0,999)}:{team:null};
    if(name==='period-end'||name==='halftime')extra.quarter=quarter;
    el('motion-title').textContent='Event sent: '+eventNames[name];
    el('motion-detail').textContent=paused?'Pause / menu is active. Presentation must remain suppressed.':'Waiting for the package to report its motion state.';
    el('motion-reference').textContent='';el('motion-report').setAttribute('data-status','pending');
    event(eventNames[name],extra);
  }
  function resize(){var width=Number(el('aspect').value);stage.style.aspectRatio=width+'/1080';frame.style.width=width+'px';frame.style.height='1080px';stage.style.setProperty('--scale',String(stage.clientWidth/width));}
  ['away','home'].forEach(function(side){var selectEl=el(side+'-team');Object.keys(teams).forEach(function(code){var option=document.createElement('option');option.value=code;option.textContent=code+' · '+teams[code].name;selectEl.appendChild(option);});selectEl.value=side==='away'?'DAL':'NE';});
  document.querySelectorAll('[data-scene]').forEach(function(button){button.addEventListener('click',function(){select(button.getAttribute('data-scene'));});});
  document.querySelectorAll('[data-motion]').forEach(function(button){button.addEventListener('click',function(){motion(button.getAttribute('data-motion'));});});
  el('stat-side').addEventListener('change',seedStats);el('stat-kind').addEventListener('change',seedStats);
  ['away-team','home-team'].forEach(function(id){el(id).addEventListener('change',seedStats);});
  el('show-player-stat').addEventListener('click',function(){statsGraphic(false);});
  el('show-player-feature').addEventListener('click',function(){scene='live';publish();var payload=editedStats();if(payload){payload.name='PLAYER_FEATURE';el('scene-title').textContent='CBS player feature';el('description').textContent='The measured raised name tab and multiline player-data treatment. This preview uses the same editable sample statistics.';send({type:'event',event:payload});stage.scrollIntoView({block:'start',behavior:'smooth'});}});
  el('show-player-comparison').addEventListener('click',function(){statsGraphic(true);});
  document.querySelectorAll('[data-stat-extra]').forEach(function(button){button.addEventListener('click',function(){extendedStats(button.getAttribute('data-stat-extra'));});});
  document.querySelectorAll('aside input, aside select').forEach(function(field){field.addEventListener('input',publish);field.addEventListener('change',publish);});
  el('aspect').addEventListener('change',resize);
  el('pause').addEventListener('click',function(){paused=!paused;status();publish();});
  el('return').addEventListener('click',function(){paused=false;status();publish();event('GAME_STARTED');});
  el('reset').addEventListener('click',function(){el('away-team').value='DAL';el('home-team').value='NE';el('away-score').value='21';el('home-score').value='14';el('away-timeouts').value='3';el('home-timeouts').value='2';el('quarter').value='1';el('game-clock').value='2:57';el('play-clock').value='25';el('down').value='1';el('distance').value='10';el('possession').value='away';select('live');seedStats();});
  frame.addEventListener('load',function(){ready=true;resize();publish();if(scene==='flag')event('PENALTY');if(scene==='timeout')event('TIMEOUT');});
  window.addEventListener('message',function(message){
    if(message.source!==frame.contentWindow||!message.data||!message.data.cbsMotion)return;
    var report=message.data.cbsMotion;
    if(typeof report!=='object')return;
    var name=typeof report.name==='string'?report.name:'CBS motion';
    var phase=typeof report.phase==='string'?report.phase:'';
    var reportStatus=typeof report.status==='string'?report.status:'';
    el('motion-title').textContent=name+(phase?' · '+phase:'');
    el('motion-detail').textContent=reportStatus?'Status: '+reportStatus:'Package motion state received.';
    var reference=report.reference;
    el('motion-reference').textContent=typeof reference==='string'&&reference?'Reference: '+reference:'';
    el('motion-report').setAttribute('data-status',reportStatus.toLowerCase());
  });
  if(window.ResizeObserver)new ResizeObserver(resize).observe(stage);else window.addEventListener('resize',resize);
  resize();status();seedStats();
})();
