(function () {
  'use strict';
  var params = new URLSearchParams(location.search), scene = params.get('preview');
  if (!scene) return;
  if (params.get('motion') === 'full') document.documentElement.setAttribute('data-preview-motion', 'full');
  var bug = document.getElementById('bug');
  document.body.style.background = params.get('background') === 'transparent' ? 'transparent' : '#23382b';
  var sample = { valid:true, gameStatus:'in_progress', quarter:1, gameClock:564, playClock:25, possession:'home', downDistanceText:'1ST & 10',
    context:{scorebugVisible:true,playSelection:false,paused:false},
    away:{abbreviation:'DAL',city:'Dallas',name:'Cowboys',record:'0-0',score:7,timeouts:3},
    home:{abbreviation:'TB',city:'Tampa Bay',name:'Buccaneers',record:'0-0',score:14,timeouts:2} };
  var brady = { name:'T. BRADY', fullName:'Tom Brady', position:'QB', kind:0, number:12, portrait:'assets/preview/brady-2004.png',
    completions:32,attempts:50,passingYards:379,passingTouchdowns:4,interceptions:2 };
  function state() { NFL2K5.dispatch({type:'state',state:sample}); }
  function event(name,more) { NFL2K5.dispatch({type:'event',event:Object.assign({name:name,team:'home'},more || {})}); }
  function later(ms,fn) { setTimeout(fn,ms); }
  function score(word,points) {
    sample.home.score += points; state(); event('SCORE_CHANGED',{points:points,score:sample.home.score});
    event(word,{drive:{plays:11,yards:75,timeOfPossession:373,source:'preview'}});
  }
  function qb() { event('PLAYER_STAT',{player:brady,caption:'2021 WEEK 1 VS DAL'}); }
  function receiver() {
    sample.home.abbreviation='NE';sample.home.city='New England';sample.home.name='Patriots';state();
    event('PLAYER_STAT',{player:{name:'T. BROWN',fullName:'Troy Brown',position:'WR',kind:2,number:80,portrait:'assets/preview/brown-2004.png',receptions:4,receivingYards:93,receivingTouchdowns:1}});
  }
  function feature() {
    event('PLAYER_FEATURE',{title:'BRADY PASSING',subtitle:'EXAMPLE OF TWO SEASON DATA',columns:['SEASON 1','SEASON 2'],
      rows:[{label:'PASSING YARDS',values:['4,000','4,500']},{label:'TOUCHDOWNS',values:['30','36']},{label:'INTERCEPTIONS',values:['12','8']},{label:'COMPLETIONS',values:['320','360']},{label:'QB RATING',values:['97.0','104.0']}],player:brady});
  }
  function introSetup() {
    /* Historical names/photos and illustrative rating inputs are fixture
       data only; a running game supplies its own roster and overall values. */
    sample.away={abbreviation:'CIN',city:'Cincinnati',name:'Bengals',record:'0-0',score:0,timeouts:3,
      featuredPlayers:[{fullName:'Corey Dillon',position:'RB',number:28,overall:90,portrait:'assets/preview/dillon-2004.png'}]};
    sample.home={abbreviation:'NE',city:'New England',name:'Patriots',record:'0-0',score:0,timeouts:3,
      featuredPlayers:[{fullName:'Tom Brady',position:'QB',number:12,overall:95,portrait:'assets/preview/brady-2004.png'}]};
    sample.gameStatus='pregame';sample.phase=0;sample.quarter=0;sample.periodLength=900;sample.gameClock=900;sample.possession=null;
    sample.context={scorebugVisible:false,playSelection:false,paused:false,pregameStage:'crowd'};
    sample.venue='Gillette Stadium - Foxborough, MA';
  }
  function intro() {
    introSetup();state();event('BROADCAST_INTRO');
    later(800,function(){sample.context.pregameStage='warmup';state();});
    later(7500,function(){sample.context.pregameStage='coin_toss';state();});
    later(8800,function(){sample.phase=2;sample.quarter=1;sample.gameStatus='in_progress';sample.context.pregameStage='kickoff';sample.context.scorebugVisible=true;sample.downDistanceText='KICKOFF';state();event('GAME_STARTED');});
  }
  function pregameBase(stageName) {
    sample.away={abbreviation:'KC',city:'Kansas City',name:'Chiefs',record:'0-0',score:0,timeouts:3};
    sample.home={abbreviation:'ATL',city:'Atlanta',name:'Falcons',record:'0-0',score:0,timeouts:3};
    sample.gameStatus='pregame';sample.phase=0;sample.quarter=0;sample.periodLength=900;sample.gameClock=900;sample.possession=null;
    sample.context={scorebugVisible:false,playSelection:false,paused:false,pregameStage:stageName};state();
  }
  function careerFact() {
    pregameBase('warmup');
    event('PLAYER_FACT',{team:'home',caption:'CAREER',text:'Has a career total of 27 interceptions.',duration:4.35,
      player:{fullName:'Jessie Bates',position:'SS',number:3}});
  }
  function coinToss() {
    pregameBase('coin_toss');event('COIN_TOSS',{team:'away',source:'native',text:'Chiefs choose heads.'});
    later(2000,function(){event('COIN_TOSS',{team:'away',source:'native',text:'Chiefs will defend the north goal.'});});
    later(5600,function(){event('COIN_TOSS_HIDE');});
    later(6100,function(){sample.phase=2;sample.quarter=1;sample.gameStatus='in_progress';sample.context.pregameStage='kickoff';sample.context.scorebugVisible=true;sample.downDistanceText='KICKOFF';state();});
  }
  function profile(receiverProfile) {
    sample.home={abbreviation:'NE',city:'New England',name:'Patriots',record:'0-0',score:14,timeouts:2};state();
    var p=receiverProfile?{name:'T. BROWN',fullName:'Troy Brown',position:'WR',number:80,portrait:'assets/preview/brown-2004.png',height:'5 FT 10',weight:'196 LB',college:'Marshall',experience:'12TH YEAR',fields:[{label:'RECEPTIONS',value:4},{label:'RECEIVING YARDS',value:93},{label:'TOUCHDOWNS',value:1},{label:'YAC',value:66}]}:
      Object.assign({},brady,{height:'6 FT 4',weight:'225 LB',age:27,college:'Michigan',experience:'5TH YEAR',fields:[{label:'COMPLETIONS',value:32},{label:'ATTEMPTS',value:50},{label:'PASSING YARDS',value:379},{label:'TOUCHDOWNS',value:4},{label:'INTERCEPTIONS',value:2}]});
    event('PLAYER_PROFILE',{caption:'EXAMPLE PROFILE / ORIGINAL GAME PORTRAIT',player:p});
  }
  function lineup() {
    sample.home={abbreviation:'NE',city:'New England',name:'Patriots',record:'0-0',score:14,timeouts:2};state();
    event('LINEUP',{players:[{position:'QB',number:12,name:'Tom Brady',college:'Michigan',portrait:'assets/preview/brady-2004.png'},
      {position:'HB',number:28,name:'Corey Dillon',college:'Washington',portrait:'assets/preview/dillon-2004.png'},
      {position:'WR',number:80,name:'Troy Brown',college:'Marshall',portrait:'assets/preview/brown-2004.png'}]});
  }
  function comparison() {
    sample.home.abbreviation='NE';sample.home.city='New England';sample.home.name='Patriots';state();
    event('PLAYER_COMPARISON',{title:'TWO PLAYMAKERS',subtitle:'EXAMPLE DATA - ORIGINAL GAME PORTRAITS',duration:7,
      players:[{fullName:'Tom Brady',position:'QB',number:12,portrait:'assets/preview/brady-2004.png',teamData:{abbreviation:'NE',primaryColor:'#002244'}},
      {fullName:'Troy Brown',position:'WR',number:80,portrait:'assets/preview/brown-2004.png',teamData:{abbreviation:'NE',primaryColor:'#c60c30'}}],
      rows:[{label:'AGE',values:[27,33]},{label:'HEIGHT',values:['6 FT 4','5 FT 10']},{label:'WEIGHT',values:['225 LB','196 LB']},{label:'YEARS PRO',values:[5,12]}]});
  }
  function teamTable() {
    sample.home.abbreviation='NE';sample.home.city='New England';sample.home.name='Patriots';state();
    event('TEAM_STATS',{teamData:{abbreviation:'NE',primaryColor:'#002244'},title:'PASS DISTRIBUTION',duration:7,columns:['REC','YDS','TD'],
      rows:[{label:'Troy Brown',values:[4,93,1]},{label:'David Givens',values:[3,46,0]},{label:'Daniel Graham',values:[2,19,0]}]});
  }
  if (scene === 'reference') {
    document.body.style.background='#212121'; bug.style.setProperty('--bug-scale','1');bug.style.setProperty('--bug-x','413px');bug.style.setProperty('--bug-y','90px');
    // Inspection overrides are confined to the reference fixture. They
    // never select a replacement font for a running game.
    var inspectFont=params.get('inspectFont'), inspectWeight=params.get('inspectWeight');
    if(['Arial','Arial Black','Segoe UI','Bahnschrift'].indexOf(inspectFont)>=0 && /^(400|600|700|900)$/.test(inspectWeight||'')) {
      var inspectRoles={clock:'.clock',score:'.score',record:'.record',ordinal:'.quarter .ord',quarter:'.quarter span'};
      var roles=inspectRoles[params.get('inspectRole')]||'.abbr,.record,.score,.clock,.quarter span';
      bug.querySelectorAll(roles).forEach(function(el){el.style.fontFamily='"'+inspectFont+'"';el.style.fontWeight=inspectWeight;});
    }
    sample.away={abbreviation:'LAR',record:'10-6',score:42,timeouts:3};sample.home={abbreviation:'CIN',record:'9-7',score:27,timeouts:2};
    sample.quarter=2;sample.gameClock=900;sample.possession=null;sample.playClock=-1;
  }
  if (scene === 'away-ball') { sample.possession='away';sample.downDistanceText='4TH & INCHES'; }
  if (scene === 'intro') introSetup();
  if (scene === 'career-fact' || scene === 'coin-toss') pregameBase('crowd');
  state();
  later(600,function () {
    switch(scene) {
      case 'qb-stat': qb(); break;
      case 'player-context': event('PLAYER_STAT',{player:brady,caption:'EXAMPLE DATA',topic:'PASSING VS BLITZ',note:'EXAMPLE CONTEXT NOTE — SUPPLIED BY THE EVENT'}); break;
      case 'receiver-stat': receiver(); break;
      case 'drive-stat': event('PLAYER_STAT',{placement:'scorebug',caption:'THIS DRIVE',player:Object.assign({},brady,{completions:4,attempts:5,passingYards:61,passingTouchdowns:1})}); break;
      case 'player-comparison': comparison(); break;
      case 'team-stats': teamTable(); break;
      case 'player-feature': feature(); break;
      case 'intro': intro(); break;
      case 'career-fact': careerFact(); break;
      case 'coin-toss': coinToss(); break;
      case 'player-profile': profile(false); break;
      case 'profile-receiver': profile(true); break;
      case 'watermark': break;
      case 'lineup': case 'lineup-motion': lineup(); break;
      case 'touchdown': score('TOUCHDOWN',6); break;
      case 'field-goal': score('FIELD_GOAL',3); break;
      case 'safety': score('SAFETY',2); break;
      case 'extra-point': score('EXTRA_POINT',1); break;
      case 'flag': event('PENALTY'); break;
      case 'timeout': event('TIMEOUT'); break;
      case 'quarter': event('QUARTER_ENDED',{quarter:1}); break;
      case 'halftime': event('HALFTIME'); break;
      case 'final': event('GAME_ENDED'); break;
      case 'replay-wipe': event('REPLAY_STARTED'); break;
      case 'showcase':
        qb(); later(6500,receiver); later(13000,feature); later(21000,lineup);
        later(30000,function(){score('TOUCHDOWN',6);});later(41200,function(){event('PENALTY');});
        later(50000,function(){event('TIMEOUT');});later(55000,function(){event('QUARTER_ENDED',{quarter:1});});
        later(61000,function(){event('REPLAY_STARTED');});later(63500,function(){event('GAME_ENDED');}); break;
    }
  });
})();
