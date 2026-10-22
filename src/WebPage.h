// SoftAP pages of the config box (design by Tobias, 2026-07-12).
//
// The UI is a small menu of four pages served on the SoftAP web server:
//   /        Firmware-Update of the box itself   (WEB_SEC_UPDATE)
//   /wlan    WLAN credentials for internet updates (WEB_SEC_WLAN)
//   /images  Device image upload into the store    (WEB_SEC_IMAGES)
//   /log     Ring-buffer log viewer                (WEB_SEC_LOG)
// Every page = WEB_PAGE_TOP (+ %NAV% menu, %DEVICE_ID%) + one section
// + WEB_PAGE_BOTTOM. The root page is persistent (WebUpdateService
// keeps a pointer); the others are built on demand in UiController.
// Forms post to the existing routes /update, /wifi, /store, /log/clear.

#pragma once

static const char WEB_PAGE_TOP[] = R"rawpage(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Config-Box %DEVICE_ID%</title>
<style>
body{margin:0;background:#0c0e0f;font-family:Helvetica,Arial,sans-serif;color:#e8eaea;font-size:13px}
a{color:#4db3a2;text-decoration:none}
.wrap{max-width:440px;margin:0 auto;padding:16px}
.card{background:#131516;border:1px solid #23282a;border-radius:8px;overflow:hidden}
.bar{height:5px;background:linear-gradient(90deg,#79C8B4,#03817D)}
.hdr{display:flex;align-items:center;gap:12px;padding:18px 28px;border-bottom:1px solid #23282a}
.wm{font-size:15px;font-weight:700;letter-spacing:3px}
.wm b{color:#fff}
.wm i{font-style:normal;color:#939598}
.wm em{font-style:normal;color:#4db3a2}
.sub{font-size:11px;color:#8a9092;letter-spacing:1px;margin-top:2px}
.nav{display:flex;border-bottom:1px solid #23282a;padding:0 16px;flex-wrap:wrap}
.nav a{padding:10px 14px;font-size:13px;color:#8a9092}
.nav a.on{color:#fff;font-weight:700;border-bottom:2px solid #03817D;margin-bottom:-1px}
.bd{padding:6px 28px 28px}
.sec{border-top:2px solid #333c3f;margin-top:28px;padding-top:22px}
.sec:first-child{border:0;margin-top:0;padding-top:16px}
h2{font-size:11px;font-weight:700;letter-spacing:2px;color:#4db3a2;margin:0 0 10px}
p{color:#8a9092;margin:0 0 10px;line-height:1.5}
.hint{font-size:12px;color:#6e7578;line-height:1.5;margin-top:10px}
.hint b,p b{color:#a9b0b2}
.col{display:flex;flex-direction:column;gap:10px}
.file{display:flex;align-items:center;gap:12px;background:#16191b;border:1px dashed #414a4d;border-radius:6px;padding:10px;cursor:pointer}
.file span{background:#24292b;color:#d7dbdb;border:1px solid #3a4144;border-radius:4px;padding:6px 14px;font-weight:700;white-space:nowrap}
.file i{font-style:normal;color:#6e7578;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.file input{display:none}
.rbt{margin-left:auto;color:#8a9092;border:1px solid #3a4144;border-radius:4px;padding:3px 9px;font-size:14px;align-self:flex-start}
.file.drag{border-color:#79C8B4;background:#1b2422}
input[type=text],input[type=password]{background:#131516;border:1px solid #3a4144;border-radius:6px;color:#e8eaea;padding:9px 12px;font-size:13px}
button,a.btn{align-self:flex-start;display:inline-block;background:transparent;color:#79C8B4;border:1px solid #0d8c80;border-radius:4px;padding:8px 18px;font-size:13px;font-weight:700;cursor:pointer}
.btnrow{display:flex;gap:10px}
.log{background:#0a0c0d;border:1px solid #23282a;border-radius:6px;padding:12px;margin:0 0 12px;font-family:Menlo,Consolas,monospace;font-size:11px;line-height:1.45;color:#c7cdcd;white-space:pre-wrap;word-break:break-all;max-height:420px;overflow:auto}
.ft{font-size:11px;color:#565c5e;text-align:center;letter-spacing:1px;border-top:1px solid #24282a;margin-top:18px;padding-top:14px}
.ok{color:#79C8B4;font-weight:700}
.nets{display:flex;flex-direction:column;gap:6px;margin:4px 0 2px}
.net{display:flex;justify-content:space-between;align-items:center;gap:10px;background:#16191b;border:1px solid #23282a;border-radius:6px;padding:8px 12px;color:#e8eaea}
.net b{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.net i{font-style:normal;color:#6e7578;font-size:11px;white-space:nowrap}
.nmsg{font-size:12px;color:#6e7578}
button.busy{opacity:.5;cursor:default}
a,button{touch-action:manipulation;-webkit-tap-highlight-color:rgba(121,200,180,.25)}
@media (hover:hover){a:hover{color:#79C8B4}.nav a:hover{color:#e8eaea}.rbt:hover{color:#fff;border-color:#0d8c80}button:hover,a.btn:hover{background:#0d8c80;color:#fff}.net:hover{border-color:#0d8c80;color:#fff}}
</style>
</head>
<body>
<div class="wrap">
<div class="card">
<div class="bar"></div>
<div class="hdr" style="border:0">
<svg width="46" height="24" viewBox="0 0 64 32" fill="none"><defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#79C8B4"/><stop offset="1" stop-color="#03817D"/></linearGradient></defs><polygon points="32,16 25,3.9 11,3.9 4,16 11,28.1 25,28.1" stroke="url(#g)" stroke-width="4"/><polygon points="60,16 53,3.9 39,3.9 32,16 39,28.1 53,28.1" stroke="url(#g)" stroke-width="4"/></svg>
<div>
<div class="wm"><i>INFINI</i><b>TAG</b> <em>NOW</em></div>
<div class="sub">CONFIG-BOX &middot; %DEVICE_ID%</div>
</div>
<a class="rbt" href="/reboot" onclick="return confirm('Update-Modus beenden und neu starten?')" title="Neustart">&#8635;</a>
</div>
<div class="nav">%NAV%</div>
<div class="bd">
)rawpage";

static const char WEB_SEC_UPDATE[] = R"rawpage(
<form class="sec" method="POST" action="/update" enctype="multipart/form-data">
<h2>FIRMWARE-UPDATE</h2>
<p>Laufende Version: <span class="ok">%VERSION%</span> &middot; <a href="https://github.com/Infinitag/infinitag-now-config/releases" target="_blank">Neueste Version auf GitHub&nbsp;&#8599;</a></p>
<div class="col">
<label class="file"><span>Datei ausw&auml;hlen</span><i>Keine Datei ausgew&auml;hlt</i><input type="file" name="fw" accept=".bin"></label>
<button type="submit">Update starten</button>
</div>
<div class="hint">Erwartete Datei: <b>infinitag-config-vX.Y.Z.bin</b> &mdash; andere Dateinamen werden abgelehnt. Nur die Box selbst wird geflasht; nach dem Upload startet sie neu.</div>
</form>
)rawpage";

static const char WEB_SEC_WLAN[] = R"rawpage(
<form class="sec" method="POST" action="/wifi">
<h2>WLAN F&Uuml;R INTERNET-UPDATES</h2>
<p>Aktuell: <b>%WIFI_STATUS%</b></p>
<div class="col">
<button type="button" id="sc">WLANs suchen</button>
<div id="nets" class="nets"></div>
<input type="text" name="ssid" placeholder="SSID" value="%WIFI_SSID%">
<input type="password" name="pass" placeholder="Passwort">
<button type="submit">WLAN speichern</button>
</div>
<div class="hint">Genutzt von Tools &gt; <b>Internet-Update</b>: Die Box verbindet sich nur dort mit dem WLAN und bleibt sonst auf ESP-NOW. W&auml;hrend der Suche kann die Verbindung zur Box kurz wackeln.</div>
</form>
<script>
(function(){
var b=document.getElementById('sc'),n=document.getElementById('nets');
var si=document.querySelector('input[name=ssid]'),pi=document.querySelector('input[name=pass]');
function msg(t){n.textContent='';var d=document.createElement('div');d.className='nmsg';d.textContent=t;n.appendChild(d);}
var busy=false;
function done(){busy=false;b.classList.remove('busy');b.textContent='WLANs suchen';}
function row(e){var a=document.createElement('a');a.href='#';a.className='net';
var s=document.createElement('b');s.textContent=e.ssid;a.appendChild(s);
var i=document.createElement('i');i.textContent=(e.enc?'gesichert \u00b7 ':'offen \u00b7 ')+e.rssi+' dBm';a.appendChild(i);
a.addEventListener('click',function(ev){ev.preventDefault();si.value=e.ssid;pi.value='';});
return a;}
function poll(){fetch('/scan',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
if(d.busy){setTimeout(poll,1000);return;}
n.textContent='';if(!d.length){msg('Keine WLANs gefunden.');}
d.forEach(function(e){n.appendChild(row(e));});done();
}).catch(function(){msg('Fehler bei der Suche \u2013 bitte erneut versuchen.');done();});}
b.addEventListener('click',function(){if(busy)return;busy=true;b.classList.add('busy');b.blur();b.textContent='Suche...';msg('Suche l\u00e4uft...');
fetch('/scan?start=1',{cache:'no-store'}).then(function(){setTimeout(poll,1500);}).catch(function(){setTimeout(poll,1500);});});
})();
</script>
)rawpage";

static const char WEB_SEC_IMAGES[] = R"rawpage(
<form class="sec" method="POST" action="/store" enctype="multipart/form-data">
<h2>GER&Auml;TE-IMAGES</h2>
<p>Firmware f&uuml;r Station/Target auf der Box speichern &mdash; wird per Funk verteilt, die Box flasht sich damit <b>nicht</b> selbst.</p>
<p>Gespeichert: <b>%IMG_STATUS%</b><br>Dateisystem: <b>%FS_STATUS%</b></p>
<pre class="log">%IMG_LIST%</pre>
<div class="col">
<label class="file"><span>Datei ausw&auml;hlen</span><i>Keine Datei ausgew&auml;hlt</i><input type="file" name="img" accept=".bin"></label>
<button type="submit">Image speichern</button>
</div>
<div class="hint">Der Speicher fasst <b>ein</b> Image; ein neuer Upload ersetzt das vorhandene.</div>
</form>
<div class="sec">
<h2>SPEICHER BEREINIGEN</h2>
<p>Formatiert das Dateisystem der Box (entfernt auch unsichtbare Datei-Leichen). WLAN-Daten und Versions-Memo bleiben erhalten (NVS).</p>
<form method="POST" action="/images/format" onsubmit="return confirm('Dateisystem wirklich formatieren?');"><button type="submit">Formatieren</button></form>
</div>
)rawpage";

static const char WEB_SEC_LOG[] = R"rawpage(
<div class="sec">
<h2>STATUS</h2>
%STATUS%
</div>
<div class="sec">
<h2>LOG</h2>
<pre class="log" id="lg">%LOG%</pre>
<div class="btnrow">
<a class="btn" href="/log">Aktualisieren</a>
<a class="btn" href="#" id="cpy">Kopieren</a>
<form method="POST" action="/log/clear"><button type="submit">Leeren</button></form>
</div>
<div class="hint">Ringpuffer (6&nbsp;KB, &auml;lteste Zeilen fallen raus). &Uuml;berlebt Neustarts per Software &mdash; nicht das Ausschalten.</div>
</div>
<script>
var l=document.getElementById('lg');l.scrollTop=l.scrollHeight;
document.getElementById('cpy').addEventListener('click',function(e){
e.preventDefault();var b=this;
var t=document.createElement('textarea');
t.value=l.textContent;
document.body.appendChild(t);
t.select();t.setSelectionRange(0,t.value.length);
try{document.execCommand('copy');b.textContent='Kopiert!';}catch(err){b.textContent='Fehler';}
document.body.removeChild(t);
setTimeout(function(){b.textContent='Kopieren';},1500);
});
</script>
)rawpage";

static const char WEB_PAGE_BOTTOM[] = R"rawpage(
<div class="ft">INFINITAG NOW &middot; CONFIG-BOX &middot; <a href="https://github.com/Infinitag" target="_blank">GITHUB&nbsp;&#8599;</a></div>
</div>
</div>
</div>
<script>
document.querySelectorAll('label.file').forEach(function(l){
var f=l.querySelector('input');
function nm(){l.querySelector('i').textContent=f.files.length?f.files[0].name:'Keine Datei ausgewählt';}
f.addEventListener('change',nm);
var z=l.closest('form')||l;
['dragover','dragenter'].forEach(function(ev){z.addEventListener(ev,function(e){e.preventDefault();l.classList.add('drag');});});
z.addEventListener('dragleave',function(){l.classList.remove('drag');});
z.addEventListener('drop',function(e){e.preventDefault();l.classList.remove('drag');if(e.dataTransfer.files.length){f.files=e.dataTransfer.files;nm();}});
});
</script>
</body>
</html>
)rawpage";
