"""Build source-backed field layers and one configured, textured player."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
from ncaa_textures import parse_mmap, decode_texture, checked, unswizzle, png_bytes
from ncaa_formats import decode_model, export_glb

class TextureBank:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        self.meta = parse_mmap(self.data, allow_unsupported=True)
        self.names = {f['name']: f['index'] for f in self.meta['files']}

    def palette(self, name):
        f = self.meta['files'][self.names[name]]
        p = self.meta['palettes'][f['palette_index']]
        return checked(self.data, p['data_offset'], p['size'])

    def png(self, name, shared_palette=None):
        index = self.names[name]; f = self.meta['files'][index]
        if f['palette_count'] or self.meta['bitmaps'][f['bitmap_index']]['format'] == 6:
            return decode_texture(self.data, self.meta, index)
        if shared_palette is None:
            raise ValueError(f'{name} requires an explicitly selected shared palette')
        bmp = self.meta['bitmaps'][f['bitmap_index']]
        if bmp['format'] != 11: raise ValueError('Unsupported pixel format')
        w,h = bmp['width'],bmp['height']
        indices = unswizzle(checked(self.data,bmp['data_offset'],bmp['size']),w,h,1)
        bgra = b''.join(shared_palette[i*4:i*4+4] for i in indices)
        rgba = bytearray(len(bgra)); rgba[0::4]=bgra[2::4];rgba[1::4]=bgra[1::4];rgba[2::4]=bgra[0::4];rgba[3::4]=bgra[3::4]
        return png_bytes(w,h,rgba)


def player_parts(players, scratch, output, uniform_index=9):
    from ncaa_rig import export_clean
    players, scratch, output = Path(players), Path(scratch), Path(output)
    output.mkdir(parents=True,exist_ok=True)
    uniform = TextureBank(scratch/'uniform_raw'/f'{uniform_index:08x}.3ds')
    gear = TextureBank(players/'00000001.3ds')
    art = TextureBank(scratch/'player_art_raw/00000000.3ds')
    skin = art.palette('YOUNGFACEWHITE')
    body = {}
    def bind(material, bank, name, palette=None, alpha=False, tint=None):
        body[material] = {'png': bank.png(name,palette), 'alpha':alpha, 'tint':tint}
    for mat, name in [('torsoSleeve','TORSOSLEEVE'),('hipThigh','HIPTHIGH'),
                      ('helmet_outer','HELMET'),('helmet_outer_jawpiece','HELMET'),('facemask','FMASK')]:
        bind(mat,uniform,name)
    for mat in ('tN_Left','shN_Left'): bind(mat,uniform,'NUM1',alpha=True)
    for mat in ('tN_Rt','shN_Rt'): bind(mat,uniform,'NUM2',alpha=True)
    bind('decal_two',uniform,'DECAL',alpha=True)
    for mat in ('wrist_Rt','wrist_Left','elbow_lower_Left','elbow_lower_Rt','elbow_upper_Left','elbow_upper_Rt'):
        # ARM's external runtime palette is unresolved. Use the source's
        # grayscale arm-detail channel with an explicitly editable skin tint.
        bind(mat,gear,'SpecArm2',tint=[.58,.32,.20,1])
    for mat in ('hand_Left','hand_Rt'): bind(mat,gear,'GLOVE')
    for mat in ('ankleShoe_Rt','ankleShoe_Left','cleats_ankleShoe_Rt','cleats_ankleShoe_Left'): bind(mat,gear,'SHOE')
    for mat in ('shin_Lt','shin_Rt'): bind(mat,uniform,'SOCKS')
    bind('helmetInner',gear,'HelmetInner')
    bind('chinstrap',gear,'CHINSTRAP')
    bind('tab',gear,'TABS')
    report = export_clean(players/'00000004.tmd',output/'body_configured.glb',body)
    face = {'face':{'png':art.png('YOUNGFACEWHITE')},
            'eye_Left':{'png':art.png('EYE',skin)},'eye_Right':{'png':art.png('EYE',skin)}}
    head = export_clean(players/'00000003.tmd',output/'face_configured.glb',face,selection=[0,72,144])
    (output/'player_source_manifest.json').write_text(json.dumps({'body':report,'head':head,
        'uniform_archive_index':uniform_index,'skin_art':'YOUNGFACEWHITE palette 0',
        'arm_shader':'Source SpecArm2 detail with editable tint; exact runtime ARM palette remains pending',
        'runtime_palette_selection':'Explicit editable example selection; not inferred from a roster'},indent=2))
    return [output/'body_configured.glb',output/'face_configured.glb']


def field_parts(scratch, output, team_index=3):
    scratch, output = Path(scratch),Path(output);output.mkdir(parents=True,exist_ok=True)
    root = scratch/'field_raw'
    # Team order is verified visually for the first entries (AIRFOR, AKRON,
    # ALBAAM, ALBAMA) and by matching the source logo/end-zone textures.
    items=[('turf',0,0),('numbers',0x37,.010),('lines',0x4a,.006)]
    if team_index is not None:
        items += [('midfield',0x4c+team_index,.014),('endzone',0x445+team_index,.012)]
    result=[]
    for role,index,z in items:
        source=root/f'{index:08x}.tmd';model=decode_model(source.read_bytes(),str(source))
        if role=='turf': model['_preview_material_tint']=[.18,.60,.08,1]
        dest=output/(role+'.glb');report=export_glb(model,dest)
        result.append({'role':role,'file':str(dest.resolve()),'z':z,'source':str(source),'report':report})
    (output/'field_source_manifest.json').write_text(json.dumps(result,indent=2))
    return result


def ensure_assets(game, scratch, player=False, log=print):
    """Read archives into separate caches. Never modify the game data folder."""
    game,scratch=Path(game),Path(scratch)
    archives={'FLDDATA.DAT':'field_raw','STADATA.dat':'stadata_raw'} if not player else {
        'PLYDATA.DAT':'players_raw','UNIFORM.DAT':'uniform_raw','PLADATA.DAT':'player_art_raw'}
    here=Path(__file__).resolve().parent
    tool_folder=here/'tools'
    if not (tool_folder/'quickbms.exe').exists(): tool_folder=Path(sys.executable).parent/'tools'
    required={'field_raw':['00000000.tmd','00000037.tmd','0000004a.tmd','0000004f.tmd','00000448.tmd'],
              'stadata_raw':['0000002e.tmd'],'players_raw':['00000001.3ds','00000003.tmd','00000004.tmd'],
              'uniform_raw':['00000009.3ds'],'player_art_raw':['00000000.3ds']}
    for archive,folder in archives.items():
        dest=scratch/folder
        if all((dest/name).is_file() for name in required[folder]): continue
        dest.mkdir(parents=True,exist_ok=True);log('Extracting '+archive)
        with (dest/'quickbms.log').open('w',encoding='utf-8') as stream:
            result=subprocess.run([str(tool_folder/'quickbms.exe'),'-o',str(tool_folder/'madden_terf.bms'),
                str(game/archive),str(dest)],stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        if result.returncode: raise RuntimeError(f'Could not extract {archive}; see {dest}/quickbms.log')


def run_blender_job(job, work, blender, log=print):
    work=Path(work);job_path=work/'assembly_job.json'
    job_path.write_text(json.dumps(job,indent=2),encoding='utf-8')
    script=Path(__file__).resolve().parent/'ncaa_blender_build.py'
    logfile=work/'blender_build.log';log('Assembling meshes and packed textures in Blender…')
    with logfile.open('w',encoding='utf-8') as stream:
        result=subprocess.run([str(blender),'--background','--factory-startup','--python',str(script),'--',str(job_path)],
            stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,
            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    if result.returncode or not Path(job['blend']).is_file() or not Path(job['glb']).is_file():
        raise RuntimeError('Blender assembly failed: '+str(logfile)+'\n'+logfile.read_text(encoding='utf-8',errors='replace')[-2000:])
    return {'file':job['glb'],'blend':job['blend'],'status':'exported','assembly_log':str(logfile)}


def fresh_output(output, name):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    base=output/name;index=2
    while base.with_suffix('.blend').exists() or base.with_suffix('.glb').exists():
        base=output/(name+'_'+str(index));index+=1
    return base


def build_clean_player(game, scratch, output, blender, log=print):
    ensure_assets(game,scratch,player=True,log=log)
    work=Path(tempfile.mkdtemp(prefix='clean_player_',dir=scratch))
    body,head=player_parts(Path(scratch)/'players_raw',scratch,work)
    target=fresh_output(output,'Alabama_Player_Clean')
    result=run_blender_job({'kind':'player','body':str(body),'head':str(head),
        'blend':str(target.with_suffix('.blend').resolve()),'glb':str(target.with_suffix('.glb').resolve())},work,blender,log)
    result.update(source_bones=75,meshes=3,rig_status='Source hierarchy; partial deformation remains experimental')
    return result


def build_stadium_field(source, game, scratch, output, blender, log=print):
    ensure_assets(game,scratch,log=log)
    work=Path(tempfile.mkdtemp(prefix='stadium_field_',dir=scratch))
    source=Path(source);model=decode_model(source.read_bytes(),str(source));code=model['name'].split('_')[0]
    team={'AIRFOR':0,'AKRONX':1,'ALBAAM':2,'ALBAMA':3}.get(code)
    layers=field_parts(scratch,work,team)
    stadium=work/'structure.glb';export_glb(model,stadium)
    prop=Path(scratch)/'stadata_raw/0000002e.tmd';goal=work/'goalpost.glb'
    export_glb(decode_model(prop.read_bytes(),str(prop)),goal)
    target=fresh_output(output,model['name'].replace('.ea3','')+'_With_Field')
    result=run_blender_job({'kind':'field','stadium':str(stadium),'goalpost':str(goal),'layers':layers,
        'blend':str(target.with_suffix('.blend').resolve()),'glb':str(target.with_suffix('.glb').resolve())},work,blender,log)
    result.update(field_team=code if team is not None else 'generic source field; team-logo mapping pending')
    return result


def uniform_thumbnail(scratch, index, size=96):
    """PNG of a UNIFORM.DAT entry: jersey (TORSOSLEEVE) over helmet (HELMET),
    cached in <scratch>/uniform_thumbs."""
    import io
    from PIL import Image
    scratch = Path(scratch)
    out = scratch / 'uniform_thumbs' / f'{index:08x}.png'
    if out.is_file():
        return out
    bank = TextureBank(scratch / 'uniform_raw' / f'{index:08x}.3ds')
    sheet = Image.new('RGB', (size, size + size // 2), (50, 50, 50))
    for name, box, at in (('TORSOSLEEVE', (size, size), (0, 0)), ('HELMET', (size, size // 2), (0, size))):
        if name in bank.names:
            try:
                sheet.paste(Image.open(io.BytesIO(bank.png(name))).convert('RGB').resize(box), at)
            except Exception:
                pass
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    return out
