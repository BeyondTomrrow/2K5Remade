import re,sys,collections
lines=open(sys.argv[1]).read().split('\n'); target=sys.argv[2]; tid=sys.argv[3]
start=next(i for i,l in enumerate(lines) if l.startswith('Call graph:'))
end=next(i for i,l in enumerate(lines) if i>start and l.startswith('Total number in stack'))
rx=re.compile(r'^([ +!:|]*)(\d+) (.*)$'); stack=[]; inth=False; agg=collections.Counter()
for l in lines[start+1:end]:
    m=rx.match(l)
    if not m: continue
    d,c,n=len(m.group(1)),int(m.group(2)),m.group(3).split('  (in')[0].strip()
    if n.startswith('Thread_'): inth = tid in n; stack=[]; continue
    if not inth: continue
    while stack and stack[-1][0]>=d: stack.pop()
    stack.append((d,n))
    if n.startswith(target): agg[' <- '.join(x[1][:40] for x in reversed(stack[-7:-1]))]+=c
for k,v in agg.most_common(8): print(v,k)
