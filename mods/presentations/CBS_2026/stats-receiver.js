/* Standalone CBS receiving lower-third. Geometry comes from the actual
 * CeeDee Lamb table in official NFL upload PLR04ljZ5DI, approximately 6090s,
 * saved locally as extended-20261018/wr-lamb-table-full.png. All identities,
 * portraits and values are supplied by the event; none are built into art.
 * The shared stats controller owns suppression, timing and cancellation.
 */
(function () {
  'use strict';
  var aliases={STL:'LAR',SD:'LAC',OAK:'LV',JAC:'JAX',WSH:'WAS'};
  var colors={ARI:'#970d2b',ATL:'#a71930',BAL:'#241773',BUF:'#00338d',CAR:'#0085ca',CHI:'#0b162a',CIN:'#fb4f14',CLE:'#311d00',DAL:'#002c5f',DEN:'#fb4f14',DET:'#0076b6',GB:'#174535',HOU:'#03202f',IND:'#002c5f',JAX:'#006778',KC:'#b90c22',LV:'#25272a',LAC:'#0080c6',LAR:'#003594',MIA:'#008e97',MIN:'#4f2683',NE:'#002244',NO:'#39342d',NYG:'#123e97',NYJ:'#006c54',PHI:'#004c54',PIT:'#242426',SF:'#aa0000',SEA:'#002244',TB:'#9c1222',TEN:'#0c2340',WAS:'#5a1414'};
  var reference='Official NFL PLR04ljZ5DI, CBS Chiefs/Cowboys Thanksgiving 2025, CeeDee Lamb receiving table ~6090s; source x153-490/y309-343 at 640x360';
  function text(value){return typeof value==='string'?value.trim():'';}
  function node(tag,cls,value,parent){var item=document.createElement(tag);if(cls)item.className=cls;if(value!=null)item.textContent=String(value);if(parent)parent.appendChild(item);return item;}
  function numeric(value,min,max){
    if(typeof value!=='number'&&typeof value!=='string'||typeof value==='string'&&!value.trim())return null;
    var result=Number(value);return Number.isInteger(result)&&result>=min&&result<=max?result:null;
  }
  function asset(value){value=text(value);return value&&!/[\u0000-\u001f]/.test(value)&&!/^\s*(?:javascript|vbscript):/i.test(value)?value:'';}
  function code(team){var value=text(team&&team.abbreviation).toUpperCase();return aliases[value]||value;}
  function color(team){var supplied=text(team&&team.primaryColor);return /^#[\da-f]{6}$/i.test(supplied)?supplied:colors[code(team)]||'#303842';}
  function image(parent,cls,src){
    if(!src)return null;var item=node('img',cls,null,parent);item.alt='';
    item.onload=function(){item.classList.add('cbs-receiver-image-ready');};
    item.onerror=function(){item.hidden=true;};item.setAttribute('src',src);return item;
  }
  function render(host,event,state){
    if(!host||!event||event.name!=='PLAYER_STAT'||!event.player||text(event.player.position).toUpperCase()!=='WR'||
      (event.team!=='away'&&event.team!=='home')||!state||!state[event.team])return false;
    var player=event.player,team=state[event.team],name=text(player.fullName)||text(player.name);
    if(!name)return false;
    var td=player.receivingTouchdowns!=null?player.receivingTouchdowns:player.receivingTD;
    var fields=[['TARGETS',numeric(player.targets,0,1000)],['REC',numeric(player.receptions,0,1000)],['YARDS',numeric(player.receivingYards,-1000,100000)],['TD',numeric(td,0,1000)]];
    if(!fields.some(function(field){return field[1]!=null;}))return false;
    var parts=name.split(/\s+/),first=parts.length>1?parts.shift():'',last=parts.join(' ')||name;
    var jersey=numeric(player.number,0,99),abbreviation=code(team);
    host.replaceChildren();
    var table=node('section','cbs-receiver-table',null,host);
    table.style.setProperty('--cbs-receiver-team',color(team));
    table.setAttribute('aria-label',name+' receiving statistics, this game');
    var art=node('div','cbs-receiver-art',null,table),local=/^[A-Z0-9]{2,4}$/.test(abbreviation)?'assets/logos/'+abbreviation+'.png':'';
    var logo=image(art,'cbs-receiver-logo',asset(team.logo)||local);
    if(logo)logo.onerror=function(){if(local&&logo.getAttribute('src')!==local){logo.setAttribute('src',local);return;}logo.hidden=true;};
    /* Never substitute a generated portrait or reuse another player's photo. */
    image(art,'cbs-receiver-portrait',asset(player.portrait));
    var number=node('div','cbs-receiver-position',null,table);
    node('div','','WR',number);node('div','',jersey==null?'\u2014':jersey,number);
    var identity=node('div','cbs-receiver-name',null,table);
    node('div','cbs-receiver-first',first,identity);node('div','cbs-receiver-last',last.toUpperCase(),identity);
    var values=node('div','cbs-receiver-columns',null,table);
    fields.forEach(function(field){
      var column=node('div','cbs-receiver-column',null,values);
      node('div','cbs-receiver-label',field[0],column);
      var value=node('div','cbs-receiver-value',field[1]==null?'\u2014':field[1],column);
      if(field[1]==null)value.setAttribute('aria-label','Unavailable');
      if(field[1]!=null&&String(field[1]).length>4)value.classList.add('cbs-receiver-value-long');
    });
    return true;
  }
  /* Measured 30fps source crossfade 6087.43-6087.63s. No separate exit
     motion was observed before the camera cut; do not invent one. */
  window.CBSReceiverStats={render:render,enterMs:200,exitMs:0,reference:reference};
})();
