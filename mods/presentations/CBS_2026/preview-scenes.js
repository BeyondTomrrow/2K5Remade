/* Explicit sample-data fixtures only. The running game never enters here. */
(function(){
  'use strict';
  var params=new URLSearchParams(location.search),scene=params.get('preview');if(!scene)return;
  if(params.get('motion')==='full')document.documentElement.setAttribute('data-preview-motion','full');
  if(scene==='manual'){document.body.style.background='#304736';return;}
  var s={valid:true,gameStatus:'in_progress',matchId:'CBS-PREVIEW',quarter:1,gameClock:177,playClock:25,phase:4,down:1,distance:10,possession:null,downDistanceText:'1ST & 10',
    away:{abbreviation:'DAL',city:'Dallas',name:'Cowboys',score:21,timeouts:3,record:'3-1'},home:{abbreviation:'NE',city:'New England',name:'Patriots',score:14,timeouts:2,record:'2-2'},context:{scorebugVisible:true,paused:false,playSelection:false}};
  document.body.style.background=params.get('background')==='transparent'?'transparent':'#304736';
  function state(){NFL2K5.dispatch({type:'state',state:JSON.parse(JSON.stringify(s))});}
  function event(name,extra){NFL2K5.dispatch({type:'event',event:Object.assign({name:name,team:'away'},extra||{})});}
  var caption=null;
  if(scene==='motion-reel'||scene==='stats-reel'||scene==='stats-extended-reel'||scene==='period-sequence'||scene==='challenge'||scene==='challenge-ruling'||scene==='coin-toss'||scene==='player-fact'||scene==='penalty-notice'||scene==='injury-notice'){
    document.body.style.background='linear-gradient(105deg,#132b21bb,#34534355),repeating-linear-gradient(0deg,transparent 0 90px,#c9ddd612 90px 92px),repeating-linear-gradient(90deg,#243c2f 0 192px,#2b4436 192px 384px)';
    caption=document.createElement('div');caption.id='preview-caption';caption.style.cssText='position:absolute;left:90px;top:100px;color:#fff;font:600 28px/1.5 Arial,sans-serif;letter-spacing:.06em;max-width:1540px;pointer-events:none';document.getElementById('canvas').appendChild(caption);
    var note=document.createElement('div');note.textContent='CBS 2026 HTML PACKAGE / SAMPLE DATA / STANDALONE RENDER';note.style.cssText='position:absolute;left:90px;top:65px;color:#c5d9cdaa;font:14px Arial,sans-serif;letter-spacing:.18em;pointer-events:none';document.getElementById('canvas').appendChild(note);
  }
  function label(title,detail){if(!caption)return;caption.textContent='';var h=document.createElement('div');h.textContent=title;caption.appendChild(h);var p=document.createElement('div');p.textContent=detail||'';p.style.cssText='margin-top:10px;color:#c3d3c9;font:18px/1.6 Arial,sans-serif;letter-spacing:0';caption.appendChild(p);}
  if(scene==='reference'||scene==='flag'){s.away={abbreviation:'ARI',name:'Cardinals',score:14,timeouts:3,record:'1-2'};s.home={abbreviation:'NYG',name:'Giants',score:3,timeouts:3,record:'2-1'};}
  if(scene==='kickoff'){s.phase=2;s.downDistanceText='KICKOFF';s.playClock=-1;s.gameClock=900;s.away={abbreviation:'PIT',name:'Steelers',score:0,timeouts:3};s.home={abbreviation:'NYJ',name:'Jets',score:0,timeouts:3};}
  if(scene==='low-clock')s.playClock=4;
  if(scene==='final'){s.gameStatus='final';s.quarter=4;s.gameClock=0;s.playClock=-1;}
  if(scene==='period-end'){s.quarter=1;s.gameClock=0;s.playClock=-1;s.context.periodEnding=true;s.away={abbreviation:'KC',name:'Chiefs',score:0,timeouts:3,record:'5-3'};s.home={abbreviation:'BUF',name:'Bills',score:7,timeouts:3,record:'5-2'};}
  if(scene==='period-sequence'){s.quarter=1;s.gameClock=2;s.playClock=10;s.away={abbreviation:'KC',name:'Chiefs',score:0,timeouts:3,record:'5-3'};s.home={abbreviation:'BUF',name:'Bills',score:7,timeouts:3,record:'5-2'};}
  state();
  if(scene==='coin-toss'||scene==='player-fact'){
    s.gameStatus='pregame';s.phase=0;s.context.scorebugVisible=false;s.context.pregameStage=scene==='coin-toss'?'coin_toss':'warmup';state();
    label('CBS / '+(scene==='coin-toss'?'COIN TOSS':'PLAYER INFORMATION'),'SAMPLE DATA / CBS design-system adaptation / live native text and controls use these slots in-game.');
    if(scene==='coin-toss')event('COIN_TOSS',{source:'sample',preview:true,nativeVisible:false,replacementToken:1,slot:0,team:'away',title:'COIN TOSS',text:'Dallas will call the coin toss.',choices:[{text:'Heads',selected:true,button:'A'},{text:'Tails',selected:false}]});
    else event('PLAYER_FACT',{source:'sample',preview:true,nativeVisible:false,replacementToken:1,slot:1,team:'away',phase:0,caption:'PLAYER INFORMATION',category:'PASSING',text:'',columns:[{label:'COMP',value:'12'},{label:'ATT',value:'18'},{label:'YDS',value:'164'}],player:{fullName:'Quincy Carter',name:'Q. Carter',number:17,position:'QB'}});
  }
  if(scene==='penalty-notice'||scene==='injury-notice'){
    s.context.scorebugVisible=false;state();
    label('CBS / '+(scene==='penalty-notice'?'PENALTY DECISION':'INJURY NOTICE'),'SAMPLE DATA / every original label and consequence remains native / graphics do not invoke the game\'s controls.');
    event('BROADCAST_NOTICE',{source:'sample',preview:true,nativeVisible:false,replacementToken:1,slot:0,phase:4,team:null,title:scene==='penalty-notice'?'PENALTY':'INJURY',text:scene==='penalty-notice'?'Offensive Holding':'Dallas QB #17',interactive:scene==='penalty-notice',choices:scene==='penalty-notice'?[{text:'Accept',selected:true,button:'A',cells:['1st & 20','DAL 25']},{text:'Decline',selected:false,cells:['2nd & 7','DAL 38']}]:[{text:'Quincy Carter',selected:true,cells:[]}]});
  }
  if(scene==='challenge'||scene==='challenge-ruling'){
    s.context.scorebugVisible=false;state();
    label('CBS / CHALLENGE & REPLAY REVIEW','SAMPLE DATA / CBS design-system adaptation / native game message and reason slots / not a measured CBS challenge reference.');
    event('REVIEW_SHOW',{source:'native',nativeVisible:true,nativeReviewToken:'1',heading:'Challenge',
      message:scene==='challenge'?'The booth is reviewing the play.':'The ruling on the field is overturned.',
      reason:scene==='challenge'?'Spot Of Ball':'',reviewStage:scene==='challenge'?'review':'ruling',preview:true});
  }
  if(scene==='period-sequence'){
    label('CBS / ACTUAL PERIOD-ENDING STATE','SAMPLE DATA / ordinary KC–BUF 2025 Q1 reference / scores and timeouts remain live fields.');
    setTimeout(function(){s.gameClock=0;s.playClock=-1;state();label('0:00 / WAIT FOR THE TITLE','A zero clock alone does not mean the last play, pending PAT or untimed down is finished.');},2500);
    setTimeout(function(){s.context.periodEnding=true;state();event('QUARTER_ENDED',{quarter:1});label('PERIOD PRESENTATION / END 1st QTR','The actual game flow now owns the period-ending label; this is not a generic full-screen bumper.');},4500);
    setTimeout(function(){s.context.periodEnding=false;s.quarter=2;s.gameClock=900;s.playClock=40;state();label('NEXT QUARTER / LIVE CLOCK RETURNS','New title state releases the END label; the graphic does not run on an independent hold timer.');},8000);
  }
  if(/^stats-/.test(scene)){
    window.NFL2K5PreviewFixturesReady=new Promise(function(resolve,reject){
      var loader=document.createElement('script');loader.src='stats-fixtures.js';
      loader.onerror=function(){reject(new Error('CBS preview statistics fixtures failed to load'));};
      loader.onload=function(){
        function playerFixture(k,title){label(title,'Reference-based CBS insert / editable sample statistics / actual local portrait where available.');var away=k===3||scene==='stats-qb-away',payload=CBSStatFixtures.player(k,away?'DAL':'NE',away?'away':'home');NFL2K5.dispatch({type:'event',event:payload});}
        function teamFixture(category,title){label(title,'SAMPLE DATA / independent current-game team fields / unavailable values are never replaced with invented totals.');NFL2K5.dispatch({type:'event',event:CBSStatFixtures.team(category)});}
        function driveFixture(split,title){label(title,split?'SAMPLE DATA / optional rush/pass split shown for renderer review; this split is not supplied by the current native summary.':'SAMPLE DATA / native-compatible total plays, yards and possession time only / honest 6 PLAYS fallback.');NFL2K5.dispatch({type:'event',event:CBSStatFixtures.drive('away',split)});}
        if(scene==='stats-extended-reel'){
          label('CBS / EXTENDED PLAYER, TEAM AND DRIVE GRAPHICS','All values are SAMPLE DATA. This standalone HTML review does not launch the game or claim a native event was received.');
          [[2500,0,'01 / QUARTERBACK'],[10000,1,'02 / RUNNING BACK'],[17500,2,'03 / WIDE RECEIVER'],[25000,'TE','04 / TIGHT END'],[32500,3,'05 / DEFENSE']].forEach(function(cue){setTimeout(function(){playerFixture(cue[1],cue[2]);},cue[0]);});
          [[40000,'yardage','06 / TEAM YARDAGE'],[46000,'defense','07 / TEAM DEFENSE'],[52000,'possession','08 / TEAM POSSESSION']].forEach(function(cue){setTimeout(function(){teamFixture(cue[1],cue[2]);},cue[0]);});
          setTimeout(function(){driveFixture(true,'09 / DRIVE SUMMARY / OPTIONAL SAMPLE SPLIT');},58000);
          setTimeout(function(){driveFixture(false,'10 / DRIVE SUMMARY / NATIVE-COMPATIBLE FIELDS');},65500);
          setTimeout(function(){label('SOURCE AND DATA BOUNDARIES','SAMPLE DATA throughout. TE remains a distinct receiving position. Native drive totals do not imply verified rush/pass counts. Full-screen player comparison remains available in the separate stats-reel.');},73500);
        }else if(scene==='stats-reel'){
          label('CBS / PLAYER INSERTS AND FULL-SCREEN STAT GRAPHICS','Actual package components with sample 2004 roster identities. No game was launched.');
          [[2500,0,'01 / QUARTERBACK'],[10000,1,'02 / RUNNING BACK'],[17500,2,'03 / RECEIVER'],[25000,3,'04 / DEFENSE'],[32500,4,'05 / KICKER']].forEach(function(cue){setTimeout(function(){playerFixture(cue[1],cue[2]);},cue[0]);});
          setTimeout(function(){label('06 / PLAYER FEATURE','Alternate multiline insert measured from the same broadcast.');var payload=CBSStatFixtures.player(0,'NE','home');payload.name='PLAYER_FEATURE';NFL2K5.dispatch({type:'event',event:payload});},40000);
          setTimeout(function(){label('07 / FULL-SCREEN COMPARISON','Measured split-team opener, oval frame, comparison table and top-to-bottom exit. Full-length portrait slots are optional.');var payload=CBSStatFixtures.comparison(s);payload.duration=14.75;NFL2K5.dispatch({type:'event',event:payload});},48500);
          setTimeout(function(){label('LIVE DATA / CURRENT LIMITS','PLAYER_STAT consumes native player data. Automatic full-screen comparison events and exact source fonts/materials remain unverified.');},65500);
        }else if(scene==='stats-comparison'){
          setTimeout(function(){NFL2K5.dispatch({type:'event',event:CBSStatFixtures.comparison(s)});},2500);
        }else if(/^stats-team-/.test(scene)){
          var category=scene.slice('stats-team-'.length);
          setTimeout(function(){teamFixture(category,'CBS TEAM STAT COMPARISON / SAMPLE DATA');},2500);
        }else if(scene==='stats-drive'||scene==='stats-drive-native'){
          setTimeout(function(){driveFixture(scene==='stats-drive','CBS DRIVE SUMMARY / SAMPLE DATA');},2500);
        }else{
          var kinds={'stats-qb':0,'stats-qb-away':0,'stats-rb':1,'stats-receiver':2,'stats-tight-end':'TE','stats-defense':3,'stats-kicker':4};
          if(kinds[scene]!=null)setTimeout(function(){playerFixture(kinds[scene],'CBS PLAYER STAT INSERT');},2500);
        }
        resolve();
      };
      document.head.appendChild(loader);
    });
  }
  if(scene==='flag')setTimeout(function(){event('PENALTY');},1900);
  if(scene==='timeout')setTimeout(function(){event('TIMEOUT');},1900);
  if(/^touchdown/.test(scene)){
    setTimeout(function(){s.away.score=27;state();if(scene!=='touchdown-late')event('TOUCHDOWN',{points:6});},2500);
    if(scene==='touchdown-late')setTimeout(function(){event('TOUCHDOWN',{points:6});},3100);
    if(scene==='touchdown-cancel')setTimeout(function(){s.context.paused=true;s.context.scorebugVisible=false;state();},3500);
  }
  if(scene==='score-update')setTimeout(function(){s.away.score=22;state();event('SCORE_CHANGED',{points:1,score:22});},2500);
  if(scene==='reference-wipe')setTimeout(function(){event('CBS_REFERENCE_WIPE');},2500);
  if(scene==='replay-in')setTimeout(function(){event('REPLAY_STARTED');},2500);
  if(scene==='replay-out')setTimeout(function(){event('REPLAY_ENDED');},2500);
  if(scene==='showcase'){
    setTimeout(function(){s.phase=2;s.downDistanceText='KICKOFF';s.playClock=-1;s.gameClock=900;state();},2400);
    setTimeout(function(){s.phase=4;s.downDistanceText='2ND & 7';s.gameClock=536;s.playClock=18;s.possession='away';state();},5000);
    setTimeout(function(){event('PENALTY');},7800);
    setTimeout(function(){s.context.playSelection=true;s.context.scorebugVisible=false;state();},10200);
    setTimeout(function(){s.context.playSelection=false;s.context.scorebugVisible=true;s.playClock=4;state();},11700);
    setTimeout(function(){s.away.score=27;s.playClock=-1;state();event('TOUCHDOWN',{points:6});},14500);
    setTimeout(function(){s.gameStatus='final';s.quarter=4;s.gameClock=0;s.playClock=-1;state();event('GAME_ENDED');},24500);
  }
  if(scene==='motion-reel'){
    label('01 / EYEBAR ENTRANCE','Center eye, team-logo travel, CBS SPORTS wordmark and live plate reveal.');
    setTimeout(function(){label('02 / FLAG REVEAL','The yellow band expands from the center; game data owns the flag state.');s.context.flag=true;state();},4000);
    setTimeout(function(){s.context.flag=false;state();},6500);
    setTimeout(function(){label('03 / POINTS UPDATE','Only the scoring team changes: previous total → actual points → new total.');s.away.score=22;state();event('SCORE_CHANGED',{points:1,score:22});},8000);
    setTimeout(function(){label('04 / TOUCHDOWN','Team-logo turn, outlined title, contraction, Eyebar return and +6. No player identity is fabricated.');s.away.score=28;s.phase=2;s.downDistanceText='KICKOFF';s.playClock=-1;state();event('TOUCHDOWN',{points:6});},11000);
    setTimeout(function(){label('05 / MENU SUPPRESSION','A pause cancels presentation work and clears the overlay.');s.context.paused=true;s.context.scorebugVisible=false;state();},18500);
    setTimeout(function(){label('06 / RETURN TO LIVE','The latest teams, score and clock return through the measured entrance.');s.context.paused=false;s.context.scorebugVisible=true;s.phase=4;s.downDistanceText='2ND & 7';s.playClock=4;state();},20000);
    setTimeout(function(){label('07 / FINAL','CBS SPORTS above FINAL; clock, quarter and timeout ticks leave the plate.');s.gameStatus='final';s.quarter=4;s.gameClock=0;s.playClock=-1;state();event('GAME_ENDED');},23000);
    setTimeout(function(){label('08 / CBS HALFTIME REPORT EXIT','Observed red/blue eye segment from the 2026 AFC halftime report; reconstructed as editable layers.');event('CBS_REFERENCE_WIPE');},26500);
    setTimeout(function(){label('09 / CBS REPLAY IN','The NFL shield grows into the replay cut, measured from the CBS full broadcast.');s.context.scorebugVisible=false;state();event('REPLAY_STARTED');},30500);
    setTimeout(function(){label('10 / CBS REPLAY RETURN','CBS SPORTS contracts into its centered wordmark while the eye outlines expand and clear.');event('REPLAY_ENDED');},33500);
    setTimeout(function(){s.gameStatus='in_progress';s.quarter=4;s.gameClock=177;s.playClock=25;s.context.scorebugVisible=true;state();},34550);
    setTimeout(function(){label('CURRENT COVERAGE','Quarter-break / complete halftime sequences, exact source font and original 3D material matching remain unfinished.');},37000);
  }
})();
