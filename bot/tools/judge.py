import glob,re,numpy as np
from sklearn.linear_model import LogisticRegression
rows=[]
for fi,f in enumerate(glob.glob(__import__('sys').argv[1] if len(__import__('sys').argv) > 1 else 'results/*/*.txt')):
    fl={}; 
    for l in open(f):
        if l.startswith('FLOOR '):
            t=l.split(); d=dict(zip(t[1::2],t[2::2]))  # crude
            p=l.split(); g=p[2]
            v={}
            v['d']=int(p[1][1:]); v['lvl']=int(p[4]); v['hp']=int(p[6]); v['ht']=int(p[7]); v['hunger']=int(p[9]); v['turn']=int(p[11]); v['food']=int(p[13]); v['heal']=int(p[15]); v['unk']=int(p[17]); v['str']=int(p[19]); v['arm']=int(p[21]); v['wpn']=int(p[23]); v['ak']=int(p[25]); v['wk']=int(p[27]); v['dew']=int(p[33]); v['vial']=int(p[35])
            fl.setdefault(g,[]).append(v)
        elif l.startswith('game '):
            p=l.split(); g='g'+p[1]; dep=int(p[p.index('depth')+1])
            for v in fl.get(g,[]): v['final']=dep; v['file']=fi; rows.append(v)
print(len(rows))
TIER={25:1,26:2,27:3,28:4,29:5}
def feats(v):
    return [v['lvl'], v['hp']/v['ht'], v['ht'], 1 if v['hunger']>=360 else 0, 1 if 260<=v['hunger']<360 else 0, min(v['food'],3), min(v['heal'],3), min(v['unk'],5), v['str'], TIER.get(v['ak'],0), max(v['arm'],-2), max(v['wpn'],-2), v['dew'], 1 if v['dew']>=10 else 0, v['vial'], v['turn']/100]
names=['lvl','hp_frac','HT','starving','hungry','food','heal','unk_pot','str','armor_tier','armor_lvl','wpn_lvl','dew','vial_full','has_vial','turn/100']
for lo,hi,boss in [(1,5,5),(6,10,10)]:
    X=[];y=[]
    for v in rows:
        if lo<=v['d']<=hi and v['final']>=v['d']:
            X.append(feats(v)+[1 if v['d']==k else 0 for k in range(lo,hi+1)]); y.append(1 if v['final']>boss else 0)
    X=np.array(X,float); y=np.array(y)
    m=X.mean(0); s=X.std(0)+1e-9
    clf=LogisticRegression(max_iter=2000,C=1.0).fit((X-m)/s,y)
    print(f"\n== floors {lo}-{hi}: chance past boss {boss}; n={len(y)} base rate {y.mean():.3f}")
    coefs=clf.coef_[0]/s
    for n_,c in zip(names,coefs): print(f"  {n_:12s} {c:+.3f} per unit")
    from sklearn.metrics import roc_auc_score
    print("  AUC", round(roc_auc_score(y,clf.predict_proba((X-m)/s)[:,1]),3))
