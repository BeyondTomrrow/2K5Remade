import json,re,sys,os
d=sys.argv[1]; out=[]
for y in range(1966,2026):
    try: w=json.load(open(f'{d}/{y}.json',encoding='utf-8'))['parse']['wikitext']['*']
    except Exception as e: out.append(f'#### {y} ERR {e}'); continue
    m=re.search(r'\n(==+)\s*(Rule changes|Rules changes|Major rule changes)[^\n]*\1\s*\n(.*?)(?=\n==[^=])',w,re.S|re.I)
    sec=m.group(3) if m else '(no rule section)'
    sec=re.sub(r'<ref[^>]*/>','',sec); sec=re.sub(r'<ref.*?</ref>','',sec,flags=re.S)
    sec=re.sub(r'\[\[(?:[^|\]]*\|)?([^\]]*)\]\]',r'\1',sec)
    out.append(f'#### {y}\n{sec.strip()}')
open(sys.argv[2],'w',encoding='utf-8').write('\n\n'.join(out))
