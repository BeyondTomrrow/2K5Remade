/* CBS player inserts and comparison ownership. Values come from events;
 * fixtures live in stats-fixtures.js and are never loaded by the game entry.
 * Reference measurements and native producer limits: docs/CBS-2026-STATS.md.
 */
(function () {
  'use strict';
  var canvas=document.getElementById('canvas'),bug=document.getElementById('eyebar');
  if(!canvas||!bug)return;
  var root=document.createElement('div');root.id='cbs-stats';root.hidden=true;
  root.setAttribute('aria-hidden','true');canvas.appendChild(root);
  var latest=null,match='',active='',generation=0,timers=[],identity='',deadline=0,nativeToken='';
  var reference='CBS Chiefs/Cowboys Thanksgiving 2025, official NFL full game PLR04ljZ5DI; compact player insert around 41–49s';
  var aliases={STL:'LAR',SD:'LAC',OAK:'LV',JAC:'JAX',WSH:'WAS'};
  var families={QB:0,RB:1,FB:1,WR:2,TE:2,DEF:3,DE:3,DT:3,LB:3,MLB:3,OLB:3,CB:3,FS:3,SS:3,S:3,K:4};
  function text(v,max){return typeof v==='string'?v.trim().slice(0,max||100):'';}
  function numeric(v,min,max,fraction){
    if(typeof v!=='number'&&typeof v!=='string'||typeof v==='string'&&!v.trim())return null;
    var n=Number(v);return Number.isFinite(n)&&n>=min&&n<=max&&(fraction||Number.isInteger(n))?n:null;
  }
  function node(tag,cls,value){var n=document.createElement(tag);if(cls)n.className=cls;if(value!=null)n.textContent=String(value);return n;}
  function allowed(s){var c=s&&s.context||{};return !!(s&&s.valid===true&&s.away&&s.home&&s.gameStatus!=='inactive'&&!c.paused&&!c.playSelection&&!c.replayActive&&!c.periodEnding&&!(window.CBSPeriodBoard&&CBSPeriodBoard.activeQuarter()));}
  function reduced(){return window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches&&document.documentElement.getAttribute('data-preview-motion')!=='full';}
  function report(phase,status,source){if(window.CBSMotion)CBSMotion.report(active||'player-stat',phase,status,source||reference);}
  function later(ms,fn){var owner=generation;timers.push(setTimeout(function(){if(owner===generation)fn();},ms));}
  function cardToken(event){var token=event&&event.nativeCardToken;return typeof token==='string'&&/^[1-9][0-9]{0,9}$/.test(token)&&Number(token)<=4294967295?token:'';}
  function nativeMessage(action,token){
    if(token&&window.NFL2K5&&NFL2K5.host==='game')NFL2K5.log('NFL2K5_PLAYER_CARD_'+action+':'+token);
  }
  function acknowledge(owner,token){
    if(!token)return;
    Promise.resolve(document.fonts&&document.fonts.ready).then(function(){
      requestAnimationFrame(function(){requestAnimationFrame(function(){
        if(owner!==generation||nativeToken!==token||active!=='player-stat'||root.hidden||!root.classList.contains('live')||!allowed(latest)||!(latest.context||{}).scorebugVisible)return;
        var plate=root.querySelector('.cbs-stat-detail'),rect=plate&&plate.getBoundingClientRect();
        if(!rect||rect.width<=0||rect.height<=0||getComputedStyle(plate).visibility==='hidden')return;
        nativeMessage('SHOW',token);
      });});
    }).catch(function(){/* Keep native artwork if fonts/paint readiness fails. */});
  }
  function cancel(){
    nativeMessage('HIDE',nativeToken);nativeToken='';
    generation++;timers.forEach(clearTimeout);timers=[];active='';identity='';deadline=0;
    root.hidden=true;root.className='';root.replaceChildren();root.setAttribute('aria-hidden','true');
    bug.classList.remove('stat-portrait-away','stat-portrait-home');canvas.classList.remove('cbs-large-stats-active','cbs-team-opening');
  }
  function kind(p){var k=numeric(p.kind,0,4);return k!=null?k:families[text(p.position,8).toUpperCase()];}
  function metrics(p){
    var k=kind(p),a,b,y,extra=[];
    function n(key,min,max,fraction){return numeric(p[key],min==null?0:min,max==null?100000:max,fraction);}
    function suffix(key,label){var v=n(key);if(v!=null)extra.push(v+' '+label);}
    if(k===0){a=n('completions');b=n('attempts');y=n('passingYards',-1000);if(a==null||b==null||y==null||a>b)return null;suffix('passingTouchdowns','TD');suffix('interceptions','INT');return [a+'/'+b+', '+y+' YDS',extra.join(' / ')];}
    if(k===1){a=n('carries');y=n('rushingYards',-1000);if(a==null||y==null)return null;suffix('rushingTouchdowns','TD');return [a+' CARRIES, '+y+' YDS',extra.join(' / ')];}
    if(k===2){a=n('receptions');y=n('receivingYards',-1000);if(a==null||y==null)return null;suffix('receivingTouchdowns','TD');return [a+' REC, '+y+' YDS',extra.join(' / ')];}
    if(k===3){a=n('tackles',0,1000,true);b=n('sacks',0,1000,true);y=n('defensiveInterceptions');if(a==null)return null;if(b!=null)extra.push(b+' SACK'+(b===1?'':'S'));if(y!=null)extra.push(y+' INT');return [a+' TACKLES',extra.join(' / ')];}
    if(k===4){a=n('fieldGoalsMade');b=n('fieldGoalsAttempted');y=n('longestFieldGoal',0,100);if(a==null||b==null||a>b)return null;return [a+'/'+b+' FIELD GOALS',y!=null?'LONG: '+y+' YDS':''];}
    return null;
  }
  function imageSource(src){
    src=text(src,1000);
    if(!src||/[\u0000-\u001f]/.test(src)||/^\s*(?:javascript|data|vbscript):/i.test(src))return '';
    return src;
  }
  function columns(p){
    var k=kind(p),v=metrics(p);if(!v)return null;
    function value(key){return numeric(p[key],/^(passing|rushing|receiving)Yards$/.test(key)?-1000:0,100000,true);}
    if(k===0)return [['COMP/ATT',value('completions')+'/'+value('attempts')],['PASS YDS',value('passingYards')],['TD',value('passingTouchdowns')],['INT',value('interceptions')]];
    if(k===1)return [['RUSH',value('carries')],['RUSH YDS',value('rushingYards')]];
    if(k===2)return [['REC',value('receptions')],['YDS',value('receivingYards')],['AVG',p.receptions>0?(p.receivingYards/p.receptions).toFixed(1):null],['TD',value('receivingTouchdowns')]];
    if(k===3)return [['TACKLES',value('tackles')],['SACKS',value('sacks')],['INT',value('defensiveInterceptions')]];
    return [['FG',value('fieldGoalsMade')+'/'+value('fieldGoalsAttempted')],['LONG',value('longestFieldGoal')]];
  }
  function small(event){
    if(event.name==='DRIVE_SUMMARY')return drive(event);
    var p=event.player,side=event.team;if(!p||typeof p!=='object'||(side!=='away'&&side!=='home'))return false;
    var name=text(p.name,80)||text(p.fullName,80),lines=metrics(p);if(!name||!lines)return false;
    var team=latest[side],teamCode=text(team.abbreviation,4).toUpperCase();teamCode=aliases[teamCode]||teamCode;
    var color=typeof team.primaryColor==='string'&&/^#[\da-f]{6}$/i.test(team.primaryColor)?team.primaryColor:null;
    root.style.setProperty('--stat-team',color||getComputedStyle(bug).getPropertyValue('--'+side).trim()||'#002c5f');
    var panel=node('section','cbs-stat-small'+(side==='away'?' on-left':''));panel.setAttribute('aria-label','Player statistics');
    var plate=node('div','cbs-stat-detail'),heading=node('div','cbs-stat-name');
    heading.appendChild(node('span','',name.toUpperCase()));var number=numeric(p.number,0,99);
    if(number!=null)heading.appendChild(node('span','stat-jersey',number));
    plate.appendChild(heading);
    var body=node('div',event.name==='PLAYER_FEATURE'?'cbs-stat-lines':'cbs-stat-columns');
    if(event.name==='PLAYER_FEATURE'){
      lines.filter(Boolean).forEach(function(line){body.appendChild(node('div','',line));});
      var caption=text(event.caption,50);if(caption)body.appendChild(node('div','stat-context',caption.toUpperCase()));
    }else{
      columns(p).forEach(function(pair){var field=node('div','cbs-stat-column'),value=pair[1]==null?'—':String(pair[1]);field.appendChild(node('div','cbs-stat-label',pair[0]));field.appendChild(node('div','cbs-stat-value'+(value.length>=6?' longest-stat':value.length>=5?' long-stat':''),value));body.appendChild(field);});
    }
    plate.appendChild(body);panel.appendChild(plate);
    var portraitSource=imageSource(p.portrait);
    if(portraitSource){
      var portrait=node('img','cbs-stat-portrait'),owner=generation;
      portrait.alt='';portrait.onload=function(){if(owner!==generation||active!=='player-stat')return;panel.classList.add('has-portrait');bug.classList.add('stat-portrait-'+side);};
      portrait.onerror=function(){if(owner!==generation)return;portrait.hidden=true;panel.classList.remove('has-portrait');bug.classList.remove('stat-portrait-'+side);};
      portrait.src=portraitSource;panel.appendChild(portrait);
    }
    root.appendChild(panel);return true;
  }
  function drive(event){
    var d=event.drive,side=event.team;
    if(!d||d.source!=='game'&&event.fixture!==true||(side!=='away'&&side!=='home'))return false;
    var plays=numeric(d.plays,1,100),yards=numeric(d.yards,-100,200),seconds=numeric(d.timeOfPossession,0,7200);
    if(plays==null||yards==null||seconds==null)return false;
    var color=latest[side].primaryColor;
    root.style.setProperty('--stat-team',typeof color==='string'&&/^#[\da-f]{6}$/i.test(color)?color:getComputedStyle(bug).getPropertyValue('--'+side).trim()||'#002c5f');
    var panel=node('section','cbs-stat-small cbs-stat-drive'+(side==='away'?' on-left':'')),plate=node('div','cbs-stat-detail');
    plate.appendChild(node('div','cbs-stat-name',event.caption==='THIS DRIVE'?'THIS DRIVE':'DRIVE SUMMARY'));
    var lines=node('div','cbs-stat-lines');
    var rush=numeric(d.rushPlays,0,100),pass=numeric(d.passPlays,0,100);
    lines.appendChild(node('div','',rush!=null&&pass!=null&&rush+pass===plays?rush+' RUSH, '+pass+' PASS':plays+' PLAYS'));
    lines.appendChild(node('div','',yards+' YARDS, '+Math.floor(seconds/60)+':'+String(seconds%60).padStart(2,'0')));
    plate.appendChild(lines);panel.appendChild(plate);root.appendChild(panel);return true;
  }
  function dismiss(){
    if(!active||root.classList.contains('leaving'))return;var label=active;root.classList.remove('entering','live');root.classList.add('leaving');report('exit','running');
    later(reduced()?0:label==='receiver-stat'&&window.CBSReceiverStats?CBSReceiverStats.exitMs:label==='team-stat'&&window.CBSTeamStats?CBSTeamStats.exitMs:label==='player-comparison'&&window.CBSLargeStats?CBSLargeStats.exitMs:930,function(){cancel();if(window.CBSMotion)CBSMotion.report(label,'settled','complete',label==='receiver-stat'&&window.CBSReceiverStats?CBSReceiverStats.reference:label==='team-stat'&&window.CBSTeamStats?CBSTeamStats.reference:label==='player-comparison'&&window.CBSLargeStats?CBSLargeStats.reference:reference);});
  }
  function show(event,large){
    var receiver=!large&&event.name==='PLAYER_STAT'&&event.player&&event.player.position==='WR'&&!cardToken(event)&&!!window.CBSReceiverStats;
    if(!allowed(latest)||((!large||receiver)&&(!(latest.context||{}).scorebugVisible||latest.gameStatus==='final'))){report('suppressed','suppressed');return true;}
    if(receiver)large=true;
    var owned=!large&&!!cardToken(event)&&event.nativeCardOwned===true;
    var team=event.name==='TEAM_STAT',label=receiver?'receiver-stat':team?'team-stat':large?'player-comparison':'player-stat';
    var signature=JSON.stringify([event.name,event.title,event.subtitle,event.players,event.rows,event.footnote,event.team,event.player,event.caption,event.teams,event.category,event.drive,cardToken(event),owned]);
    if(active===label&&signature===identity&&Date.now()<deadline)return true;
    if(window.CBSMotion)CBSMotion.cancel('stat-insert');cancel();
    active=label;
    var renderer=receiver?window.CBSReceiverStats:team?window.CBSTeamStats:window.CBSLargeStats;
    var rendered=large?renderer&&renderer.render(root,event,latest):small(event);
    if(!rendered){cancel();if(window.CBSMotion)CBSMotion.report('player-stat','missing-data','unavailable','The event must contain valid player identity and statistics; no values are invented.');return true;}
    identity=signature;nativeToken=large?'':cardToken(event);var seconds=numeric(event.displaySeconds!=null?event.displaySeconds:event.duration,2,20,true);
    var enterMs=reduced()?0:large?renderer.enterMs:440;
    var lifetime=large?(seconds==null?14.75:seconds)*1000:enterMs+(seconds==null?6:seconds)*1000;
    root.hidden=false;root.className=receiver?'cbs-stat-receiver entering':team?'cbs-stat-team entering':large?'cbs-stat-comparison entering':'cbs-stat-insert entering';root.setAttribute('aria-hidden','false');
    canvas.classList.toggle('cbs-large-stats-active',!!large);deadline=owned?Infinity:Date.now()+lifetime;
    canvas.classList.toggle('cbs-team-opening',!!team&&enterMs>0);
    report('entrance','running',large?renderer.reference:reference);
    var owner=generation,token=nativeToken;
    later(enterMs,function(){canvas.classList.remove('cbs-team-opening');root.classList.remove('entering');root.classList.add('live');report('hold','running',large?renderer.reference:reference);if(!large)acknowledge(owner,token);});
    // Native-owned cards follow the real popup's close event. A local hold
    // timer could reveal the original card again before its timeout finishes.
    if(!owned)later(lifetime,dismiss);return true;
  }
  function updateState(s){
    var next=s.away&&s.home?String(s.matchId||'')+'|'+String(s.away.abbreviation)+'|'+String(s.home.abbreviation):'';
    if(match&&next&&next!==match)cancel();if(next)match=next;latest=s;
    if(!allowed(s)||(active==='player-stat'||active==='receiver-stat')&&(!s.context||s.context.scorebugVisible!==true||s.gameStatus==='final'))cancel();
  }
  function handleEvent(e){
    if(!e)return false;
    if(e.name==='PLAYER_STAT_HIDE'){if(cardToken(e)&&cardToken(e)===nativeToken)dismiss();return true;}
    if(e.name==='PLAYER_STAT'||e.name==='PLAYER_FEATURE'||e.name==='DRIVE_SUMMARY')return show(e,false);
    if(e.name==='PLAYER_COMPARISON'||e.name==='TEAM_STAT')return show(e,true);
    if(['PLAY_STARTED','REPLAY_STARTED','REPLAY_ENDED','TOUCHDOWN','GAME_ENDED','GAME_STARTED','QUARTER_ENDED','HALFTIME','CBS_REFERENCE_WIPE','CBS_HALFTIME_REPORT_READY'].indexOf(e.name)!==-1)cancel();
    return false;
  }
  window.CBSStats={updateState:updateState,handleEvent:handleEvent,cancel:cancel,dismiss:dismiss,
    snapshot:function(){return {name:active,visible:!root.hidden,phase:root.classList.contains('leaving')?'exit':root.classList.contains('entering')?'entrance':active?'hold':'idle'};}};
})();
