from pathlib import Path
import sys, io, json
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from ncaa_assemble import TextureBank
from PIL import Image, ImageDraw
root=Path('H:/NCAA07_Extractor_Work/uniform_raw')
banks=[]
for p in sorted(root.glob('*.3ds')):
    b=TextureBank(p)
    if 'HELMET' in b.names:
        banks.append(p)
print('uniform banks',len(banks))
dest=Path('H:/NCAA07_Extractor_Work/leather_search');dest.mkdir(exist_ok=True)
for page in range((len(banks)+119)//120):
    sheet=Image.new('RGB',(1600,1200),'#666666');d=ImageDraw.Draw(sheet)
    for k,p in enumerate(banks[page*120:(page+1)*120]):
        b=TextureBank(p);x=k%10*160;y=k//10*100
        for j,name in enumerate(('HELMET','TORSOSLEEVE')):
            try:
                im=Image.open(io.BytesIO(b.png(name))).convert('RGB');im.thumbnail((78,76));sheet.paste(im,(x+j*80,y+20))
            except Exception as e: print(p.name,name,str(e))
        d.text((x,y),p.stem,fill='white')
    sheet.save(dest/f'uniforms_{page}.png')
print(dest)
