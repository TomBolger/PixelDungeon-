"""blame.py RESULT.txt: summarise BLAME lines (replays of the moments before each death with other settings)."""
import re,collections,sys
L=[l for l in open(sys.argv[1]) if l.startswith('BLAME')]
sty=collections.defaultdict(lambda:[0,0]); kinds=collections.Counter(); gain=collections.defaultdict(lambda:[0,0,0]); base=[0,0]
for l in L:
    m=re.search(r'default (\d+)/(\d+)',l); d,R=int(m.group(1)),int(m.group(2)); base[0]+=d; base[1]+=R
    st=re.search(r'styles:(.*)',l)
    if st:
        for n,v in re.findall(r' (\w+) (\d+)',st.group(1)): sty[n][0]+=int(v); sty[n][1]+=R
    best=re.findall(r'\[([^\]]+?) (\d+)/(\d+)\]',l)
    if best:
        name,v,_=best[0]; v=int(v)
        if v-d>=3:
            k=re.sub(r' \d+,\d+','',name); k=re.sub(r'(drink|read|throw|zap|eat) .*',r'\1',k) if not k.startswith('style') else k
            kinds[k]+=1
print('checkpoints',len(L),'default survival',base[0],'/',base[1])
print('styles (survived/total):',{k:f'{v[0]}/{v[1]}' for k,v in sty.items()})
print('best alternative beats default by 3+/R:',kinds.most_common(20))
