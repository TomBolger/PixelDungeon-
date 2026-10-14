"""upg.py RESULT.txt: summarise UPGCF lines (each upgrade scroll played into armour and into the weapon, to the end)."""
import re,sys,collections
L=[l for l in open(sys.argv[1]) if l.startswith('UPGCF')]
agg=collections.defaultdict(lambda:[0,0]); byctx=collections.defaultdict(lambda:collections.defaultdict(lambda:[0,0]))
wins=0; best=collections.Counter(); n=0
ARM={25:'cloth',26:'leather',27:'mail',28:'scale',29:'plate'}
for l in L:
    d=dict((k,float(v)) for k,v in re.findall(r'\[(\w+) ([\d.]+)',l))
    if 'armour' not in d or 'weapon' not in d: continue
    n+=1; m=re.search(r'arm (\d+)([+-]\d+)(\??) wpn (\d+)',l); a=int(m.group(1)); unk=m.group(3)=='?'
    dflt=re.search(r'default (-?\d+)',l).group(1)
    dval=d.get('other', d['armour'] if dflt=='-2' else d['weapon'])
    for k,v in (('default',dval),('armour',d['armour']),('weapon',d['weapon'])): agg[k][0]+=v; agg[k][1]+=1
    ctx=(ARM.get(a,str(a))+('?' if unk else ''), re.search(r' n(\d)',l).group(1))
    for k,v in (('default',dval),('armour',d['armour']),('weapon',d['weapon'])): byctx[ctx][k][0]+=v; byctx[ctx][k][1]+=1
    best[max(('default',dval),('armour',d['armour']),('weapon',d['weapon']),key=lambda x:x[1])[0]]+=1
print('decisions',n)
print({k:round(v[0]/v[1],2) for k,v in agg.items()})
print('best option counts',best)
for c in sorted(byctx): print(c, {k:f'{v[0]/v[1]:.2f} (n{v[1]})' for k,v in byctx[c].items()})
