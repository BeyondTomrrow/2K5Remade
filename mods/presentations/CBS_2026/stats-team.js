/* CBS compact team comparison. Geometry measured from the actual YARDS
 * insert in PLR04ljZ5DI, 1188-1215s; the stable 640x360 reference is saved
 * at outputs/cbs-2026/reference/extended-20261018/team-comparison-full.png.
 * Values belong to the event. Missing data never becomes a zero or a total
 * inferred by this renderer. Lifecycle belongs to the stats controller.
 */
(function () {
  'use strict';
  var aliases={STL:'LAR',SD:'LAC',OAK:'LV',JAC:'JAX',WSH:'WAS'};
  var colors={ARI:'#970d2b',ATL:'#a71930',BAL:'#241773',BUF:'#00338d',CAR:'#0085ca',CHI:'#0b162a',CIN:'#fb4f14',CLE:'#311d00',DAL:'#002c5f',DEN:'#fb4f14',DET:'#0076b6',GB:'#174535',HOU:'#03202f',IND:'#002c5f',JAX:'#006778',KC:'#b90c22',LV:'#25272a',LAC:'#0080c6',LAR:'#003594',MIA:'#008e97',MIN:'#4f2683',NE:'#002244',NO:'#39342d',NYG:'#123e97',NYJ:'#006c54',PHI:'#004c54',PIT:'#242426',SF:'#aa0000',SEA:'#002244',TB:'#9c1222',TEN:'#0c2340',WAS:'#5a1414'};
  var categories={
    yardage:{title:'YARDS',fields:[['RUSH','rushingYards',-100000,1000000],['PASS','passingYards',-100000,1000000],['TOTAL','totalYards',-100000,1000000]]},
    defense:{title:'DEFENSE',fields:[['SACKS','sacks',0,1000,'half'],['TACKLES','tackles',0,100000],['INT','defensiveInterceptions',0,1000]]},
    possession:{title:'POSSESSION',fields:[['FIRST DOWNS','firstDowns',0,100000],['TOP','timeOfPossession',0,1000000,'clock']]}
  };
  var reference='CBS Chiefs/Cowboys Thanksgiving 2025, official NFL PLR04ljZ5DI, compact YARDS comparison 1198.80-1203.13s; measured 1230ms entrance, camera-cut exit';
  var openerSerial=0;
  function text(value){return typeof value==='string'?value.trim():'';}
  function code(team){var value=text(team&&team.abbreviation).toUpperCase();return aliases[value]||value;}
  function node(tag,cls,value,parent){var result=document.createElement(tag);if(cls)result.className=cls;if(value!=null)result.textContent=String(value);if(parent)parent.appendChild(result);return result;}
  function numeric(value,field){
    if(typeof value!=='number'&&typeof value!=='string'||typeof value==='string'&&!value.trim())return null;
    var result=Number(value);
    if(!Number.isFinite(result)||result<field[2]||result>field[3])return null;
    if(field[4]==='clock')return Math.floor(result);
    return Number.isInteger(result*(field[4]==='half'?2:1))?result:null;
  }
  function display(value,field){
    if(value==null)return '\u2014';
    if(field[4]==='clock')return Math.floor(value/60)+':'+('0'+value%60).slice(-2);
    return String(value);
  }
  function asset(value){value=text(value);return value&&!/[\u0000-\u001f]/.test(value)&&!/^\s*(?:javascript|vbscript):/i.test(value)?value:'';}
  function color(team){var supplied=text(team&&team.primaryColor);return /^#[\da-f]{6}$/i.test(supplied)?supplied:colors[code(team)]||'#303842';}
  function logo(parent,team,onSource){
    var abbreviation=code(team),local=/^[A-Z0-9]{2,4}$/.test(abbreviation)?'assets/logos/'+abbreviation+'.png':'';
    var supplied=asset(team.logo),src=supplied||local;
    if(!src)return;
    var image=node('img','cbs-team-logo',null,parent);image.alt='';
    image.onerror=function(){
      if(local&&image.getAttribute('src')!==local){image.setAttribute('src',local);if(onSource)onSource(local);return;}
      image.onerror=null;image.hidden=true;if(onSource)onSource('');
    };
    image.setAttribute('src',src);
    if(onSource)onSource(src);
  }
  function opener(host,state){
    var band=node('div','cbs-team-opener',null,host);
    band.setAttribute('aria-hidden','true');
    ['away','home'].forEach(function(side){
      var plane=node('div','cbs-team-opener-plane cbs-team-opener-'+side,null,band);
      var svg=document.createElementNS('http://www.w3.org/2000/svg','svg');
      svg.setAttribute('viewBox','0 0 420 420');svg.setAttribute('class','cbs-team-opener-contour');
      var id='cbs-team-contour-'+(++openerSerial);
      /* Derive both silhouette and internal color-boundary contours from
         this matchup's actual logo. No hand-drawn generic substitute and
         no raster reference footage is loaded by this component. */
      svg.innerHTML='<defs><filter id="'+id+'" x="-8%" y="-8%" width="116%" height="116%" color-interpolation-filters="sRGB">'+
        '<feColorMatrix in="SourceGraphic" type="saturate" values="0" result="mono"/>'+
        '<feConvolveMatrix in="mono" order="3" kernelMatrix="-1 -1 -1 -1 8 -1 -1 -1 -1" preserveAlpha="true" result="details"/>'+
        '<feColorMatrix in="details" type="matrix" values="0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 .2126 .7152 .0722 0 0" result="detailInk"/>'+
        '<feComposite in="detailInk" in2="SourceAlpha" operator="in" result="boundedDetails"/>'+
        '<feMorphology in="SourceAlpha" operator="dilate" radius="1" result="outer"/>'+
        '<feMorphology in="SourceAlpha" operator="erode" radius="1" result="inner"/>'+
        '<feComposite in="outer" in2="inner" operator="out" result="edge"/>'+
        '<feFlood flood-color="#e4f1ff" flood-opacity=".86" result="ink"/>'+
        '<feComposite in="ink" in2="edge" operator="in" result="outline"/>'+
        '<feMerge><feMergeNode in="boundedDetails"/><feMergeNode in="outline"/></feMerge></filter></defs>';
      var outline=document.createElementNS('http://www.w3.org/2000/svg','image');
      outline.setAttribute('width','420');outline.setAttribute('height','420');
      outline.setAttribute('preserveAspectRatio','xMidYMid meet');outline.setAttribute('filter','url(#'+id+')');
      svg.appendChild(outline);plane.appendChild(svg);
      logo(plane,state[side],function(src){
        if(src){outline.setAttribute('href',src);svg.style.display='';}
        else{outline.removeAttribute('href');svg.style.display='none';}
      });
    });
  }
  function render(host,event,state){
    if(!host||!event||event.name!=='TEAM_STAT'||event.scope!=='this_game'||
      (event.source!=='game'&&event.source!=='preview')||!state||!state.away||!state.home||!event.teams)return false;
    var category=Object.prototype.hasOwnProperty.call(categories,event.category)?categories[event.category]:null;
    if(!category)return false;
    var available=0;
    var rows=['away','home'].map(function(side){
      var sample=event.teams[side];if(!sample||typeof sample!=='object'||sample.valid===false)sample={};
      var values=category.fields.map(function(field){var value=numeric(sample[field[1]],field);if(value!=null)available++;return value;});
      return {side:side,team:state[side],values:values};
    });
    if(!available)return false;
    host.replaceChildren();
    opener(host,state);
    var table=node('section','cbs-team-table cbs-team-'+event.category,null,host);
    table.setAttribute('role','table');table.setAttribute('aria-label',category.title+' comparison, this game');
    table.style.setProperty('--cbs-team-columns',category.fields.length);
    node('div','cbs-team-title',category.title,table);
    var header=node('div','cbs-team-head',null,table);header.setAttribute('role','row');
    node('div','cbs-team-label-blank','',header).setAttribute('role','columnheader');
    category.fields.forEach(function(field){node('div','cbs-team-column-label',field[0],header).setAttribute('role','columnheader');});
    rows.forEach(function(row){
      var line=node('div','cbs-team-row cbs-team-'+row.side,null,table);line.setAttribute('role','row');
      line.style.setProperty('--cbs-team-color',color(row.team));
      var identity=node('div','cbs-team-identity',null,line);identity.setAttribute('role','rowheader');
      logo(identity,row.team);
      node('span','cbs-team-city',(text(row.team.city)||code(row.team)||text(row.team.name)).toUpperCase(),identity);
      row.values.forEach(function(value,index){
        var cell=node('div','cbs-team-value',display(value,category.fields[index]),line);cell.setAttribute('role','cell');
        if(value==null){cell.classList.add('cbs-team-unavailable');cell.setAttribute('aria-label','Unavailable');}
      });
    });
    return true;
  }
  /* The controller owns the first200ms Eyebar contraction. Traveling logo
     planes/contours run270-630ms; the measured560ms table reveal starts670ms.
     The source exits by a camera cut, so no reverse animation is invented. */
  window.CBSTeamStats={render:render,enterMs:1230,exitMs:0,reference:reference,
    timing:{eyebarExitMs:200,logoStartMs:270,tableStartMs:670,tableRevealMs:560}};
})();
