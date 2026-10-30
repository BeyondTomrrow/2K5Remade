/* Browser-review fixtures only. No game API calls, timers or state mutation.
 * Identity examples come from the project's original 2004 roster previews;
 * every statistic below is illustrative, not a historical or current result.
 * Unknown identities are explicitly named Sample Quarterback/Receiver/etc.
 * Portraits use existing local preview assets; none are downloaded here.
 * Include this helper in preview pages only, never the live package entry.
 */
(function () {
  'use strict';
  var positions=['QB','RB','WR','DEF','K'];
  var aliases={qb:0,quarterback:0,rb:1,fb:1,runningback:1,wr:2,te:2,receiver:2,def:3,defense:3,k:4,kicker:4};
  var labels=['Quarterback','Running Back','Receiver','Defender','Kicker'];
  var known={
    NE:{
      0:{fullName:'Tom Brady',name:'T. Brady',number:12,portrait:'../NBC_SNF/assets/preview/brady-2004.png'},
      1:{fullName:'Corey Dillon',name:'C. Dillon',number:28,portrait:'../NBC_SNF/assets/preview/dillon-2004.png'},
      2:{fullName:'Troy Brown',name:'T. Brown',number:80,portrait:'../NBC_SNF/assets/preview/brown-2004.png'}
    },
    DAL:{0:{fullName:'Quincy Carter',name:'Q. Carter',number:17}}
  };
  function kindOf(value){
    if(typeof value==='number')return Number.isInteger(value)&&value>=0&&value<positions.length?value:null;
    if(typeof value!=='string')return null;
    var text=value.trim().toLowerCase();
    if(/^[0-4]$/.test(text))return Number(text);
    return Object.prototype.hasOwnProperty.call(aliases,text)?aliases[text]:null;
  }
  function player(kind,teamCode,side){
    var family=kindOf(kind);if(family==null)return null;
    var tightEnd=typeof kind==='string'&&kind.trim().toLowerCase()==='te';
    var code=String(teamCode||'').trim().toUpperCase();
    /* A TE uses the receiving schema, but must not inherit the WR identity. */
    var identity=!tightEnd&&known[code]&&known[code][family]||null;
    var sampleName='Sample '+(tightEnd?'Tight End':labels[family]);
    /* Native serialization supplies every family's numeric fields. Keep
       that shape here so UI family selection must use kind or position. */
    var data={
      name:identity?identity.name:sampleName,
      fullName:identity?identity.fullName:sampleName,
      position:tightEnd?'TE':positions[family],number:identity?identity.number:null,
      college:'',portrait:identity&&identity.portrait||'',kind:family,
      completions:0,attempts:0,passingYards:0,passingTouchdowns:0,interceptions:0,
      carries:0,rushingYards:0,rushingTouchdowns:0,
      receptions:0,receivingYards:0,receivingTouchdowns:0,
      tackles:0,sacks:0,defensiveInterceptions:0,
      fieldGoalsMade:0,fieldGoalsAttempted:0,longestFieldGoal:0
    };
    if(family===0)Object.assign(data,{completions:14,attempts:22,passingYards:153,passingTouchdowns:1,interceptions:0});
    if(family===1)Object.assign(data,{carries:12,rushingYards:67,rushingTouchdowns:1});
    if(family===2)Object.assign(data,{receptions:4,receivingYards:61,receivingTouchdowns:1});
    if(tightEnd)Object.assign(data,{receptions:3,receivingYards:38,receivingTouchdowns:0});
    if(family===3)Object.assign(data,{tackles:6,sacks:1,defensiveInterceptions:0});
    if(family===4)Object.assign(data,{fieldGoalsMade:2,fieldGoalsAttempted:2,longestFieldGoal:43});
    return {name:'PLAYER_STAT',team:side==='away'||side==='home'?side:null,
      source:'preview',fixture:true,displaySeconds:6,caption:'THIS GAME',player:data};
  }
  function team(category){
    if(['yardage','defense','possession'].indexOf(category)===-1)return null;
    /* Deliberately illustrative values, independent of the selected identities.
       Never infer team totals from the individual player fixtures. */
    return {name:'TEAM_STAT',category:category,scope:'this_game',source:'preview',fixture:true,duration:5.5,
      teams:{
        away:{carries:14,rushingYards:64,passingYards:41,totalYards:105,tackles:16,sacks:1.5,defensiveInterceptions:1,firstDowns:6,timeOfPossession:524},
        home:{carries:4,rushingYards:8,passingYards:78,totalYards:86,tackles:19,sacks:0,defensiveInterceptions:0,firstDowns:4,timeOfPossession:376}
      }};
  }
  function drive(side,withSplit){
    var data={plays:6,yards:47,timeOfPossession:197,source:'game'};
    if(withSplit===true){data.rushPlays=5;data.passPlays=1;}
    /* The native summary currently verifies total plays/yards/time only.
       The optional split is a separate preview fixture, not a live claim. */
    return {name:'DRIVE_SUMMARY',team:side==='home'?'home':'away',source:'preview',fixture:true,
      displaySeconds:6,caption:'THIS DRIVE',drive:data};
  }
  function comparison(state){
    if(!state||!state.away||!state.home)return null;
    var away=player(0,state.away.abbreviation,'away'),home=player(0,state.home.abbreviation,'home');
    Object.assign(home.player,{completions:12,attempts:20,passingYards:136,passingTouchdowns:1,interceptions:1});
    /* These local preview images are headshots, not the full-length cutouts
       in the CBS comparison reference. Do not stretch a head into that slot. */
    away.player.portrait='';home.player.portrait='';
    return {name:'PLAYER_COMPARISON',team:null,source:'preview',fixture:true,
      title:'QUARTERBACK COMPARISON',subtitle:'THIS GAME',duration:14.75,
      players:[{team:'away',player:away.player},{team:'home',player:home.player}],
      rows:[{label:'COMP / ATT',values:['14/22','12/20']},
        {label:'PASS YARDS',values:[153,136]},
        {label:'TD / INT',values:['1 / 0','1 / 1']}]};
  }
  window.CBSStatFixtures=Object.freeze({player:player,team:team,drive:drive,comparison:comparison});
})();
