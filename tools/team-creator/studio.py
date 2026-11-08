"""Local Team Studio: real mesh preview, editable textures and isolated packs."""
from __future__ import annotations
import argparse
import base64
import copy
import csv
import hashlib
import io
import json
import mimetypes
import re
import secrets
import shutil
import sys
import threading
import time
import traceback
import uuid
import webbrowser
import zipfile
from functools import lru_cache
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
from PIL import Image
import game_assets as G
import logos
import rosters
import studio_assets as A
import build_team

PROJECTS = HERE/'projects'
WEB = HERE/'studio-ui'
TOKEN = secrets.token_urlsafe(32)
JOBS = {}; BUILD_LOCK = threading.Lock(); MODEL_LOCK = threading.Lock()
Image.MAX_IMAGE_PIXELS = 16_777_216

def read(path): return json.loads(Path(path).read_text(encoding='utf-8'))
def slug(value):
    value = re.sub('[^A-Za-z0-9_-]', '', value)[:48]
    if not value or value in ('.','..'): raise ValueError('Give this project a name')
    return value
def project_path(key):
    if slug(key) != key: raise ValueError('Invalid project ID')
    return PROJECTS/key
def atomic_json(path, value):
    path.parent.mkdir(parents=True,exist_ok=True)
    temporary = path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
    temporary.write_text(json.dumps(value,indent=2),encoding='utf-8'); temporary.replace(path)
def load_project(key): return read(project_path(key)/'project.json')
def asset_path(key, relative):
    base = project_path(key).resolve(); target = (base/relative).resolve()
    if not target.is_relative_to(base) or not target.is_file(): raise ValueError('Project asset is missing')
    return target
@lru_cache(maxsize=1)
def archive(): return G.Archive(ROOT/'original/disc')
@lru_cache(maxsize=24)
def uniform_rows(name):
    if not re.fullmatch(r'[0-9A-Z]{2}[ha][a-z0-9]{1,2}\.iff',name,re.I): raise ValueError('Invalid uniform filename')
    data = archive().read(name)
    if data is None: raise ValueError('Uniform not found on this disc')
    return A.textures(data)
def color(value):
    if isinstance(value,str):
        if not re.fullmatch('#[0-9a-fA-F]{6}',value): raise ValueError('Invalid color')
        return [int(value[i:i+2],16) for i in (1,3,5)]
    if not isinstance(value,list) or len(value)!=3 or any(type(v)!=int or not 0<=v<=255 for v in value): raise ValueError('Invalid color')
    return value
def validate(spec):
    spec = copy.deepcopy(spec)
    for key in ('city','nickname'):
        if not isinstance(spec.get(key),str) or not spec[key].strip() or len(spec[key])>15:
            raise ValueError(f'{key.title()} must contain 1–15 characters (the game limit)')
    if not re.fullmatch('[A-Z0-9]{2,4}',spec.get('abbreviation','')): raise ValueError('Use a 2–4 character abbreviation')
    if spec.get('base') not in {t[0] for t in read(HERE/'teams_2k5.json')}: raise ValueError('Unknown base team')
    spec['folder'] = slug(spec.get('folder') or spec['city']+spec['nickname'])
    spec['colors'] = [color(c) for c in spec['colors']]
    spec['base_colors'] = [color(c) for c in spec['base_colors']]
    players = spec.get('roster',[])
    if not 1<=len(players)<=53: raise ValueError('The roster must contain 1–53 players; use 53 for a full game-day roster')
    for p in players:
        for field,low,high in (('pos',0,16),('number',0,99),('height',60,90),('weight',140,400),('skin',0,21),('face',0,15),('years_pro',0,31),('hand',0,1)):
            if type(p.get(field))!=int or not low<=p[field]<=high: raise ValueError(f'Invalid player {field}: {p.get("last", "")}')
        if any(not isinstance(p.get(k),str) or len(p[k])>15 for k in ('first','last')): raise ValueError('Player names must fit 15 characters')
        if len(p.get('ratings',[]))!=28 or any(type(v)!=int or not 0<=v<=99 for v in p['ratings']): raise ValueError('Each player needs 28 ratings from 0–99')
    return spec

def bootstrap():
    teams = [t for t in read(HERE/'teams_2k5.json') if t[0].isdigit() and int(t[0])<38 and t[1] not in ('NFL','AFC','NFC','USER1','USER2')]
    colors = {}
    for t in teams:
        try:
            c=read(ROOT/'mods/teams'/t[1]/'team.json')['colors']; colors[t[0]]=[color(c['primary']),color(c['secondary'])]
        except (OSError,ValueError,KeyError): colors[t[0]]=[[170,0,0],[255,255,255]]
    venues=[]
    for path in sorted((ROOT/'mods/eras/stadiums').glob('*/stadium.json')):
        try: venues.append(dict(id=path.parent.name,**read(path)))
        except (OSError,ValueError): pass
    installed=[]
    for path in sorted((ROOT/'mods/teams').glob('*/creator.json')):
        try:
            s=read(path); installed.append(dict(id=path.parent.name,name=f'{s["city"]} {s["nickname"]}'))
        except (OSError,ValueError,KeyError): pass
    return dict(teams=teams,colors=colors,stadiums=venues,installed=installed,positions=rosters.POSITIONS,
        ratings=rosters.RATINGS,discReady=(ROOT/'original/disc/vc_53450030/0').is_file(),
        projects=[dict(id=p.parent.name,name=read(p).get('nickname',p.parent.name)) for p in sorted(PROJECTS.glob('*/project.json'))])

@lru_cache(maxsize=1)
def model_data():
    with MODEL_LOCK:
        import model_preview as M
        model=M.PlayerModel(archive().dir)
        center=(model.lo+model.hi)/2; height=float(model.hi[1]-model.lo[1])
        meshes=[]
        for s in model.submeshes:
            if s.tris.max()>=len(s.P) or s.tris.min()<0: raise ValueError('Original mesh has an out-of-range triangle')
            meshes.append(dict(name=s.material,texture=s.texkind,positions=((s.P-center)/height*2).round(7).reshape(-1).tolist(),
                uv=s.uv.round(7).reshape(-1).tolist(),indices=s.tris.reshape(-1).tolist()))
        return dict(source='Original disc player hi_body + hi_head; bind pose',meshes=meshes)

def preview(spec, side, filename=None):
    import model_preview as M
    filename=filename or f'{spec["base"]}{"a" if side=="away" else "h"}0.iff'
    rows=uniform_rows(filename)
    images={r['name']:Image.open(io.BytesIO(base64.b64decode(r['image'].split(',')[1]))).convert('RGBA') for r in rows if 'image' in r}
    uniform=spec.get('uniforms',{}).get(side)
    if uniform: uniform=A.normalized_uniform(uniform)
    if uniform:
        if 'splayer' in images: images['splayer']=logos.paint_splayer(images['splayer'],uniform,spec['base_colors'],side=='away')
        for name in ('helmet00','helmet02'):
            if name in images:
                images[name]=logos.paint_helmet_shell(images[name],uniform['helmet_color'])
                if uniform.get('helmet_stripe',{}).get('on',True): images[name]=logos.draw_helmet_stripe(images[name],uniform['helmet_stripe'])
        for name in list(images):
            if name in logos.NUMBER_NAMES: images[name]=logos.paint_number(images[name],uniform)
        # Detailed uniform meshes sample separate jersey/pants/sleeve TSET
        # textures, not the low-detail splayer atlas. Preserve each source's
        # shading while replacing its authored primary/secondary palette.
        for name,primary,secondary in [('jersey00',uniform['jersey_color'],uniform['jersey_trim']),
                ('pants00',uniform['pants_color'],uniform['pant_stripe']['color']),
                ('sleeve00',uniform['jersey_color'],uniform['jersey_trim'])]:
            if name in images:
                images[name]=A.detailed_image(name,images[name],uniform,spec['base_colors'],side=='away')
    if spec.get('logo'):
        logo=Image.open(asset_path(spec['project_id'],spec['logo'])).convert('RGBA')
        for name in ('helmet00','helmet02'):
            if name in images: images[name]=logos.helmet(images[name],logo,spec.get('nickname','TEAM'),(uniform or {}).get('helmet_color',spec['colors'][0]))
    for key,path in spec.get('texture_overrides',{}).items():
        if key.startswith(filename+'|'):
            row=next(r for r in rows if r['id']==key.split('|')[1]);images[row['name']]=Image.open(asset_path(spec['project_id'],path)).convert('RGBA')
    result={key:A.image_url(images[name]) for key,name in [('splayer','splayer'),('helmet00','helmet00'),('number','48'),
        ('jersey00','jersey00'),('pants00','pants00'),('sleeve00','sleeve00'),('socks00','socks00'),('shoes01','shoes01')] if name in images}
    result['facemask_color']=(uniform or {}).get('facemask_color',[180,180,180])
    return dict(textures=result,assetImages={name:A.image_url(images[name]) for name in
        ('jersey00','pants00','sleeve00','splayer','helmet00','helmet02','socks00','shoes01') if name in images},
        filename=filename,source='Editable design preview on the original bind-pose mesh; verify a built pack in-game')

def start_build(project):
    key=project['project_id'];spec=validate(project)
    if len(spec['roster'])!=53: raise ValueError('Build needs a full 53-player roster')
    if not BUILD_LOCK.acquire(blocking=False): raise ValueError('A team is already building; wait for it to finish')
    job=uuid.uuid4().hex;JOBS[job]=dict(status='building',log=[],result=None)
    def worker():
        stage=PROJECTS/'.builds'/job
        try:
            stage.mkdir(parents=True)
            for name in ('logo','helmet_logo'):
                if spec.get(name):spec[name]=str(asset_path(key,spec[name]))
            spec['texture_overrides']={k:str(asset_path(key,p)) for k,p in spec.get('texture_overrides',{}).items()}
            for p in spec['roster']:
                if p.get('photo_image'):p['photo_image']=str(asset_path(key,p['photo_image']))
            target=ROOT/'mods/teams'/spec['folder']
            if (target/'team.json').exists():spec['code']=read(target/'team.json')['code']
            result=build_team.build(spec,ROOT,lambda m:JOBS[job]['log'].append(str(m)),output_dir=stage/'team')
            # Make texture overrides portable too, after the builder has consumed them.
            recipe=read(result/'creator.json');portable={}
            for k,path in spec.get('texture_overrides',{}).items():
                destination=result/'textures'/(hashlib.sha256(k.encode()).hexdigest()[:16]+'.png')
                destination.parent.mkdir(exist_ok=True);shutil.copy2(path,destination);portable[k]=destination.relative_to(result).as_posix()
            recipe['texture_overrides']=portable;atomic_json(result/'creator.json',recipe)
            target.parent.mkdir(parents=True,exist_ok=True);backup=None
            if target.exists():
                backup=ROOT/'outputs/team-studio/backups'/(target.name+'-'+time.strftime('%Y%m%d-%H%M%S')+'-'+job[:6])
                backup.parent.mkdir(parents=True,exist_ok=True);target.rename(backup)
            try:result.rename(target)
            except Exception:
                if backup and not target.exists():backup.rename(target)
                raise
            JOBS[job].update(status='done',result=str(target),backup=str(backup) if backup else None)
        except Exception as exc:
            JOBS[job].update(status='failed',error=str(exc));JOBS[job]['log'].append(traceback.format_exc())
        finally:BUILD_LOCK.release()
    threading.Thread(target=worker,daemon=True).start()
    return dict(job=job)

class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def send(self,data,mime='application/json',status=200,filename=None):
        if not isinstance(data,bytes):data=json.dumps(data).encode()
        self.send_response(status);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(data)))
        self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff')
        if filename:self.send_header('Content-Disposition',f'attachment; filename="{filename}"')
        self.end_headers();self.wfile.write(data)
    def do_GET(self):
        try:
            if self.headers.get('Host')!=f'127.0.0.1:{self.server.server_port}':
                return self.send(dict(error='Use the local Team Studio address'),status=403)
            url=urlparse(self.path);q={k:v[0] for k,v in parse_qs(url.query).items()}
            if url.path=='/api/bootstrap':return self.send(bootstrap())
            if url.path=='/api/model':return self.send(model_data())
            if url.path=='/api/files':return self.send(dict(files=archive().team_files(q['base'])))
            if url.path=='/api/textures':return self.send(dict(textures=uniform_rows(q['file'])))
            if url.path=='/api/project':return self.send(load_project(q['id']))
            if url.path=='/api/job':return self.send(JOBS[q['id']])
            if url.path=='/api/export-uniforms':return self.send(A.export_zip(archive(),q['base']),'application/zip',filename='uniform-textures.zip')
            if url.path=='/api/export-project':
                folder=project_path(q['id']);buffer=io.BytesIO()
                with zipfile.ZipFile(buffer,'w',zipfile.ZIP_DEFLATED) as z:
                    for p in folder.rglob('*'):
                        if p.is_file():z.write(p,p.relative_to(folder))
                return self.send(buffer.getvalue(),'application/zip',filename=q['id']+'-project.zip')
            if url.path=='/api/asset':return self.send(asset_path(q['id'],q['path']).read_bytes(),'image/png')
            file=WEB/('index.html' if url.path=='/' else url.path.lstrip('/'))
            if not file.resolve().is_relative_to(WEB.resolve()) or not file.is_file():return self.send(dict(error='Not found'),status=404)
            data=file.read_bytes()
            if file.name=='index.html':data=data.replace(b'__TOKEN__',TOKEN.encode())
            self.send(data,mimetypes.guess_type(file.name)[0] or 'application/octet-stream')
        except Exception as exc:self.send(dict(error=str(exc)),status=400)
    def do_POST(self):
        try:
            origin=self.headers.get('Origin');expected=f'http://127.0.0.1:{self.server.server_port}'
            if self.headers.get('X-Studio-Token')!=TOKEN or (origin and origin!=expected):return self.send(dict(error='Open Team Studio locally first'),status=403)
            length=int(self.headers.get('Content-Length','0'))
            if not 0<length<=64*1024*1024:raise ValueError('Upload must be smaller than 64 MB')
            data=json.loads(self.rfile.read(length));route=urlparse(self.path).path
            if route=='/api/save':
                key=slug(data.get('project_id') or uuid.uuid4().hex[:12]);data['project_id']=key
                atomic_json(project_path(key)/'project.json',data);return self.send(dict(id=key))
            if route=='/api/preview':return self.send(preview(data['spec'],data['side'],data.get('file')))
            if route=='/api/generate':return self.send(dict(roster=rosters.pick_53(rosters.generate(int(data.get('strength',75))))))
            if route=='/api/upload':
                key=data['id'];folder=project_path(key)/'assets';folder.mkdir(parents=True,exist_ok=True)
                raw=base64.b64decode(data['data'].split(',')[-1],validate=True);suffix=Path(data['name']).suffix.lower()
                if data.get('kind')=='image':
                    with Image.open(io.BytesIO(raw)) as im:
                        if im.width*im.height>16_777_216:raise ValueError('Image exceeds 16 megapixels')
                        image=im.convert('RGBA');out=io.BytesIO();image.save(out,format='PNG');raw=out.getvalue();suffix='.png'
                elif suffix not in ('.csv','.json','.ros','.db','.dat','.mcn','.roster',''):raise ValueError('Unsupported roster format')
                name=uuid.uuid4().hex+suffix;target=folder/name;target.write_bytes(raw)
                return self.send(dict(path='assets/'+name))
            if route=='/api/import-roster':
                path=asset_path(data['id'],data['path'])
                if path.suffix=='.csv':
                    players=[]
                    for row in csv.DictReader(path.read_text(encoding='utf-8-sig').splitlines()):
                        players.append(dict(first=row['first'],last=row['last'],
                            **{k:int(row.get(k) or default) for k,default in [('pos',0),('number',0),('height',72),('weight',220),('years_pro',0),('skin',3),('face',0),('hand',1)]},
                            ratings=[int(row.get(k) or 50) for k in rosters.RATINGS],ovr=75))
                    return self.send(dict(roster=players))
                if path.suffix=='.json':
                    value=read(path);return self.send(dict(roster=value if isinstance(value,list) else value['roster']))
                source=rosters.EARoster(str(path))
                if 'team' not in data:return self.send(dict(teams=source.team_list(),format=source.kind))
                return self.send(dict(roster=rosters.pick_53(source.team_players(int(data['team']))),info=source.team_info(int(data['team']))))
            if route=='/api/open-installed':
                folder=ROOT/'mods/teams'/slug(data['id']);spec=read(folder/'creator.json')
                key=uuid.uuid4().hex[:12];spec['project_id']=key;out=project_path(key);(out/'assets').mkdir(parents=True)
                def capture(value):
                    p=Path(value);p=p if p.is_absolute() else folder/p
                    if not p.is_file():return ''
                    target=out/'assets'/(uuid.uuid4().hex+p.suffix);shutil.copy2(p,target);return target.relative_to(out).as_posix()
                for k in ('logo','helmet_logo'):
                    if spec.get(k):spec[k]=capture(spec[k])
                spec['texture_overrides']={k:capture(v) for k,v in spec.get('texture_overrides',{}).items()}
                for p in spec.get('roster',[]):
                    if p.get('photo_image'):p['photo_image']=capture(p['photo_image'])
                atomic_json(out/'project.json',spec);return self.send(spec)
            if route=='/api/build':return self.send(start_build(data))
            if route=='/api/clone-stadium':
                source=ROOT/'mods/eras/stadiums'/slug(data['source'])
                if not (source/'stadium.json').is_file():raise ValueError('Select an installed stadium pack to clone')
                name=str(data.get('name','')).strip()
                if not name or len(name)>48:raise ValueError('Use a stadium name of 1–48 characters')
                capacity=int(data['capacity'])
                if not 1<=capacity<=200000:raise ValueError('Capacity must be 1–200,000')
                folder=slug(name);target=source.parent/folder
                if target.resolve()==source.resolve():raise ValueError('Use a different name to preserve the original venue')
                stage=PROJECTS/'.venues'/uuid.uuid4().hex
                shutil.copytree(source,stage)
                metadata=read(stage/'stadium.json')
                metadata.update(name=name.upper(),display=name,location=str(data.get('location',''))[:64],
                    capacity=capacity,grass=int(bool(data.get('grass'))),indoor=int(bool(data.get('indoor'))))
                atomic_json(stage/'stadium.json',metadata)
                backup=None
                if target.exists():
                    backup=ROOT/'outputs/team-studio/backups'/(folder+'-venue-'+uuid.uuid4().hex[:8])
                    backup.parent.mkdir(parents=True,exist_ok=True);target.rename(backup)
                try:stage.rename(target)
                except Exception:
                    if backup and not target.exists():backup.rename(target)
                    raise
                return self.send(dict(name=metadata['name'],folder=str(target)))
            raise ValueError('Unknown action')
        except Exception as exc:self.send(dict(error=str(exc)),status=400)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--no-browser',action='store_true');parser.add_argument('--port',type=int,default=0)
    args=parser.parse_args();PROJECTS.mkdir(exist_ok=True)
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    url=f'http://127.0.0.1:{server.server_port}/'
    atomic_json(ROOT/'outputs/team-studio/session.json',dict(url=url,pid=__import__('os').getpid()))
    print('Team Studio: '+url,flush=True)
    if not args.no_browser:webbrowser.open(url)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:server.server_close()
if __name__=='__main__':main()
