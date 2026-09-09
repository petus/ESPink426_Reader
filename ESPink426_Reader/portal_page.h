// ============================================================================
//  portal_page.h  -  HTML stranka nahravaciho portalu
// ----------------------------------------------------------------------------
//  Sobestacna: zadne CDN, zadne externi skripty ani fonty. Deska nema
//  pristup k internetu a v rezimu AP ho nema ani telefon.
//
//  Kodovani: cely soubor i stranka jsou UTF-8. Server posila hlavicku
//  "text/html; charset=utf-8" a stranka ma <meta charset="utf-8">, aby
//  ceska diakritika prosla i pres captive portal prohlizece.
//
//  Responzivni: jeden sloupec, ovladaci prvky dost velke na dotyk,
//  breakpoint na 560 px pro pohodlnejsi rozlozeni na desktopu.
// ============================================================================
#pragma once
#include <Arduino.h>

static const char RDR_PORTAL_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html lang="cs">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Čtečka — nahrávání knih</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
:root{--bg:#11141a;--card:#191e26;--line:#2a313c;--txt:#e7eaf0;--dim:#98a2b3;--acc:#5b8def}
body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:var(--bg);
     color:var(--txt);line-height:1.5;font-size:16px;padding:14px;padding-bottom:40px}
.wrap{max-width:640px;margin:0 auto}
h1{font-size:21px;margin-bottom:2px}
h2{font-size:13px;margin:22px 0 8px;color:var(--dim);text-transform:uppercase;letter-spacing:.07em}
.sub{color:var(--dim);font-size:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
.drop{border-color:#3b444f;border-radius:12px;padding:26px 14px;
      text-align:center;color:var(--dim);cursor:pointer;transition:.15s}
.drop.hot,.drop:hover{border-color:var(--acc);color:var(--txt);background:#1d2530}
.drop b{display:block;color:var(--txt);font-size:16px;margin-bottom:3px}
input[type=file]{display:none}
.bar{height:6px;background:var(--line);border-radius:3px;overflow:hidden;margin-top:12px;display:none}
.bar i{display:block;height:100%;width:0;background:var(--acc);transition:width .2s}
ul{list-style:none}
li{display:flex;align-items:center;gap:10px;padding:11px 0;border-bottom:1px solid #232a34;flex-wrap:wrap}
li:last-child{border-bottom:0}
.bk{flex:1 1 60%;min-width:0}
.bk .t{word-break:break-word}
.bk .m{color:var(--dim);font-size:13px;margin-top:2px}
.acts{display:flex;gap:6px;flex:0 0 auto}
button{font:inherit;border:0;border-radius:8px;padding:10px 14px;cursor:pointer;min-height:40px}
.pri{background:var(--acc);color:#fff;width:100%;font-weight:600}
.sm{background:var(--line);color:var(--txt);padding:7px 11px;font-size:14px;min-height:34px}
.sm.dl{color:#ff9b9b}
.gh{background:var(--line);color:var(--txt);width:100%;margin-top:6px}
label{display:block;font-size:13px;color:var(--dim);margin:12px 0 5px}
input[type=text],input[type=password],select,input[type=number]{
  width:100%;padding:11px;border-radius:8px;border:1px solid var(--line);
  background:var(--bg);color:var(--txt);font:inherit}
.seg{display:flex;gap:6px}
.seg button{flex:1;background:var(--line);color:var(--txt)}
.seg button.on{background:var(--acc);color:#fff;font-weight:600}
.msg{padding:11px 13px;border-radius:9px;margin-bottom:12px;display:none;font-size:15px}
.ok{background:#123020;border:1px solid #1f5c38;color:#93e5b4}
.er{background:#331618;border:1px solid #5c2327;color:#ffabab}
.foot{display:flex;justify-content:space-between;gap:10px;color:var(--dim);
      font-size:13px;margin-top:16px;flex-wrap:wrap}
.empty{color:#6b7684;font-style:italic;padding:6px 0}
.saved{background:#12251a;border:1px solid #245c3a;color:#93e5b4;
       border-radius:8px;padding:8px 11px;font-size:14px;margin-bottom:4px}
.saved.none{background:#231f16;border-color:#5c4a23;color:#e5cf93}
.nets{margin-top:8px}
.nets button{display:flex;justify-content:space-between;align-items:center;
  width:100%;background:var(--line);color:var(--txt);margin-top:5px;text-align:left}
.nets .sig{color:var(--dim);font-size:13px;margin-left:10px}
@media(min-width:560px){body{padding:24px}h1{font-size:24px}}
</style>
</head>
<body><div class="wrap">
  <h1>Čtečka eknih</h1>
  <div class="sub" id="sub">Načítám…</div>

  <div class="msg" id="msg"></div>

  <h2>Nahrát knihy</h2>
  <div class="card">
    <div class="drop" id="drop"><b>Přetáhněte sem soubory .book nebo .epub</b>nebo klepněte pro výběr</div>
    <input type="file" id="file" multiple accept=".book,.epub">
    <div class="bar" id="bar"><i id="fill"></i></div>
  </div>

  <h2>Knihy na kartě</h2>
  <div class="card"><ul id="list"><li class="empty">Načítám…</li></ul></div>

  <h2>Zobrazení</h2>
  <div class="card">
    <label>Velikost písma</label>
    <div class="seg" id="fseg">
      <button data-f="0">Malé</button>
      <button data-f="1">Střední</button>
      <button data-f="2">Velké</button>
    </div>
    <label for="slp">Uspat po nečinnosti (minut)</label>
    <input type="number" id="slp" min="1" max="60" step="1">
    <button class="pri" style="margin-top:14px" onclick="saveSet()">Uložit zobrazení</button>
  </div>

  <h2>Domácí WiFi</h2>
  <div class="card">
    <div class="sub" style="margin-bottom:6px">Uložíte-li síť, čtečka se k ní
      příště připojí sama místo vytváření hotspotu. Prázdná pole = smazat.</div>
    <div id="saved" class="saved">Uložená síť: žádná</div>
    <label for="ssid">Název sítě (SSID)</label>
    <input type="text" id="ssid" autocomplete="off" spellcheck="false">
    <button class="gh" style="margin-top:8px" onclick="scan()" id="scanb">
      Vyhledat dostupné sítě</button>
    <div id="nets"></div>
    <label for="pw">Heslo</label>
    <input type="password" id="pw" autocomplete="off">
    <button class="pri" style="margin-top:14px" onclick="saveWifi()">Uložit WiFi</button>
  </div>

  <div class="foot"><span id="space">—</span><span id="tmr">—</span></div>
  <button class="gh" onclick="finish()">Ukončit a vrátit se ke čtení</button>
</div>

<script>
var busy=false, font=1, filled=false, savedSsid='';

function say(t,bad){var m=document.getElementById('msg');
  m.textContent=t;m.className='msg '+(bad?'er':'ok');m.style.display='block';
  if(!bad)setTimeout(function(){m.style.display='none'},4500);}
function hum(kb){if(kb<1024)return kb+' kB';return (kb/1024).toFixed(1)+' MB';}
function mmss(s){s=Math.max(0,s);var m=Math.floor(s/60),r=s%60;return m+':'+(r<10?'0':'')+r;}

function refresh(){
  fetch('/api/status').then(function(r){return r.json()}).then(function(d){
    document.getElementById('sub').textContent=d.mode+' · '+d.ip+
      (d.host?' · '+d.host:'')+' · baterie '+d.batt+' %';
    document.getElementById('space').textContent='Volné místo: '+hum(d.free)+' z '+hum(d.total);
    document.getElementById('tmr').textContent='Vypne se za '+mmss(d.idle);
    savedSsid=d.ssid||''; showSaved();
    // Pole SSID a minuty se vyplni JEN POPRVE. Pri kazdem obnoveni by
    // prepsaly to, co uzivatel prave píše, a vypadalo by to, ze se
    // zadana sit sama maze.
    if(!filled){ document.getElementById('ssid').value=savedSsid;
                 document.getElementById('slp').value=d.sleep; filled=true; }
    font=d.font; setSeg();
    var ul=document.getElementById('list'); ul.innerHTML='';
    if(!d.books.length){ul.innerHTML='<li class="empty">Zatím žádné knihy.</li>';return;}
    d.books.forEach(function(b){
      var li=document.createElement('li');
      var info=document.createElement('div'); info.className='bk';
      var t=document.createElement('div'); t.className='t'; t.textContent=b.title;
      var m=document.createElement('div'); m.className='m';
      m.textContent=(b.conv?'EPUB · ke zpracování'
                    :(b.done?'Přečteno':(b.pct>0?'Rozečteno '+b.pct+' %':'Nepřečteno')))+
                    ' · '+hum(b.size);
      info.appendChild(t); info.appendChild(m);
      var acts=document.createElement('div'); acts.className='acts';
      var mk=document.createElement('button'); mk.className='sm';
      mk.textContent=b.done?'Nepřečteno':'Přečteno';
      mk.onclick=function(){mark(b.file,b.done?0:1)};
      var dl=document.createElement('button'); dl.className='sm dl'; dl.textContent='Smazat';
      dl.onclick=function(){del(b.file,b.title)};
      acts.appendChild(mk); acts.appendChild(dl);
      li.appendChild(info); li.appendChild(acts); ul.appendChild(li);
    });
  }).catch(function(){});
}

function showSaved(){var e=document.getElementById('saved');
  if(savedSsid){e.textContent='Uložená síť: '+savedSsid;e.className='saved';}
  else{e.textContent='Uložená síť: žádná — čtečka vytvoří vlastní hotspot';
       e.className='saved none';}}

function scan(){var b=document.getElementById('scanb');
  b.disabled=true; b.textContent='Hledám sítě…';
  fetch('/api/scan').then(function(r){return r.json()}).then(function(d){
    b.disabled=false; b.textContent='Vyhledat dostupné sítě';
    var box=document.getElementById('nets'); box.className='nets'; box.innerHTML='';
    if(!d.nets.length){box.innerHTML='<div class="empty">Žádné sítě nenalezeny.</div>';return;}
    d.nets.forEach(function(n){
      var x=document.createElement('button');
      var nm=document.createElement('span'); nm.textContent=n.s+(n.e?' 🔒':'');
      var sg=document.createElement('span'); sg.className='sig'; sg.textContent=n.q+' %';
      x.appendChild(nm); x.appendChild(sg);
      x.onclick=function(){document.getElementById('ssid').value=n.s;
        document.getElementById('pw').focus();};
      box.appendChild(x);});
  }).catch(function(){b.disabled=false;b.textContent='Vyhledat dostupné sítě';
    say('Vyhledávání sítí se nezdařilo.',true);});}

function setSeg(){var b=document.querySelectorAll('#fseg button');
  for(var i=0;i<b.length;i++) b[i].className = (+b[i].dataset.f===font)?'on':'';}
document.querySelectorAll('#fseg button').forEach(function(b){
  b.onclick=function(){font=+b.dataset.f;setSeg();};});

// DULEZITE: data se posilaji jako application/x-www-form-urlencoded.
// WebServer na ESP32 umi arg() cist POUZE z urlencoded tela; multipart
// (tedy FormData) zpracovava jen pro nahravani souboru, takze pri jeho
// pouziti se hodnoty tise ztratily a nic se neulozilo.
function post(url,obj){
  var b=new URLSearchParams();
  for(var k in obj) b.append(k,obj[k]);
  return fetch(url,{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},
    body:b.toString()}).then(function(r){
    return r.text().then(function(t){if(!r.ok)throw new Error(t);return t;});});}

function del(file,title){ if(busy)return;
  if(!confirm('Opravdu smazat „'+title+'“?\nSmaže se i uložená pozice čtení.'))return;
  post('/api/delete',{file:file}).then(function(t){say(t,false);refresh();})
    .catch(function(e){say(e.message||'Smazání se nezdařilo.',true)});}

function mark(file,done){ if(busy)return;
  post('/api/read',{file:file,done:done}).then(function(t){say(t,false);refresh();})
    .catch(function(e){say(e.message||'Nezdařilo se.',true)});}

function saveSet(){
  post('/api/settings',{font:font,sleep:document.getElementById('slp').value}).then(function(t){say(t,false)})
    .catch(function(e){say(e.message||'Uložení se nezdařilo.',true)});}

function saveWifi(){var s=document.getElementById('ssid').value.trim();
  var p=document.getElementById('pw').value;
  if(s.length&&p.length&&p.length<8){say('Heslo WiFi musí mít alespoň 8 znaků.',true);return;}
  post('/api/wifi',{ssid:s,pass:p}).then(function(t){
      say(t,false); savedSsid=s; showSaved();})
    .catch(function(e){say(e.message||'Uložení se nezdařilo.',true)});}

function finish(){ if(busy){say('Počkejte na dokončení nahrávání.',true);return;}
  if(!confirm('Ukončit režim nahrávání a vrátit se ke čtení?'))return;
  fetch('/api/finish',{method:'POST'});
  document.body.innerHTML='<div class="wrap"><h1>Hotovo</h1><div class="sub">'+
    'Čtečka se vrátila do seznamu knih. WiFi se vypnula, '+
    'tuto stránku můžete zavřít.</div></div>';}

var drop=document.getElementById('drop'), inp=document.getElementById('file');
drop.onclick=function(){if(!busy)inp.click()};
['dragenter','dragover'].forEach(function(e){drop.addEventListener(e,function(v){
  v.preventDefault();drop.classList.add('hot')})});
['dragleave','drop'].forEach(function(e){drop.addEventListener(e,function(v){
  v.preventDefault();drop.classList.remove('hot')})});
drop.addEventListener('drop',function(v){send(v.dataTransfer.files)});
inp.onchange=function(){send(inp.files)};

function send(files){ if(busy||!files.length)return;
  var list=[];
  for(var i=0;i<files.length;i++){
    if(!/\.(book|epub)$/i.test(files[i].name)){
      say('„'+files[i].name+'“ není soubor .book ani .epub.',true);
      return;}
    list.push(files[i]);}
  busy=true; document.getElementById('bar').style.display='block'; one(list,0);}

function one(list,i){
  if(i>=list.length){ busy=false;
    document.getElementById('bar').style.display='none';
    document.getElementById('fill').style.width='0';
    say('Nahráno: '+list.length+' '+(list.length===1?'soubor':'souborů')+'.',false);
    refresh(); return;}

  // Nejdriv oznamime velikost a POCKAME na potvrzeni - teprve pak posilame
  // soubor. Kdyby oba pozadavky bezely soucasne, server by mohl zacit
  // prijimat data driv, nez se dozvi, kolik jich ma cekat.
  post('/api/expect',{size:list[i].size})
    .catch(function(){})          // kdyz se nepodari, jede se dal bez kontroly
    .then(function(){ sendOne(list,i); });
}

function sendOne(list,i){
  var fd=new FormData(); fd.append('book',list[i],list[i].name);
  var x=new XMLHttpRequest(); x.open('POST','/api/upload');
  x.upload.onprogress=function(e){ if(e.lengthComputable)
    document.getElementById('fill').style.width=
      (((i+e.loaded/e.total)/list.length)*100)+'%';};
  x.onload=function(){ if(x.status===200) one(list,i+1);
    else{busy=false;document.getElementById('bar').style.display='none';
      say(x.responseText||'Nahrávání selhalo.',true);refresh();}};
  x.onerror=function(){busy=false;document.getElementById('bar').style.display='none';
    say('Spojení se čtečkou se přerušilo.',true);};
  x.send(fd);}

refresh(); setInterval(function(){if(!busy)refresh()},5000);
</script>
</body></html>
)HTMLPAGE";
