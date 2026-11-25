import re,sys,collections
lines=open(sys.argv[1]).read().split('\n')
start=next(i for i,l in enumerate(lines) if l.startswith('Call graph:'))
end=next(i for i,l in enumerate(lines) if i>start and l.startswith('Total number in stack'))
rx=re.compile(r'^([ +!:|]*)(\d+) (.*)$')
nodes=[]
for l in lines[start+1:end]:
    m=rx.match(l)
    if not m: continue
    nodes.append((len(m.group(1)),int(m.group(2)),m.group(3).strip()))
WAIT=('__psynch_cvwait','mach_msg2_trap','semaphore_wait','__semwait','nanosleep','__workq_kernreturn','__select','kevent','__ulock_wait','__psynch_mutexwait','start_wqthread','swtch_pri','__wait4','poll')
busy=collections.Counter(); leaf_by=collections.defaultdict(collections.Counter); thread=None
for i,(d,c,name) in enumerate(nodes):
    if 'Thread_' in name and (i==0 or True) and name.split()[0].startswith('Thread_'): thread=name[:60]; tdepth=d; continue
    nxt=nodes[i+1][0] if i+1<len(nodes) else -1
    if nxt<=d:
        fn=name.split('  (in')[0]
        if not any(w in fn for w in WAIT):
            busy[thread]+=c; leaf_by[thread][fn[:90]]+=c
for t,c in busy.most_common(6):
    print(c, t)
    for fn,n in leaf_by[t].most_common(6): print('     ',n,fn)
