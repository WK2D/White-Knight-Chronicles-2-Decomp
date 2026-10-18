import argparse,json,statistics,collections,bisect
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('trace',type=Path);args=ap.parse_args()
labels={1:'FIFO drain',2:'present',3:'frame-clock wait',4:'SPU entry run',5:'SPU channel wait',6:'syscall >=20us',7:'HLE >=20us',8:'GPU fence wait',9:'readback',10:'GPU flush',11:'GCM semaphore wait'}
records=[];total=collections.Counter();counts=collections.Counter();kinds=collections.Counter();spu=collections.Counter();sys=collections.Counter()
with args.trace.open(encoding='utf-8') as f:
 header=next(f).strip()
 for line in f:
  k,tid,a,b,t0,t1=line.split();r=(int(k),int(tid),int(a),int(b),float(t0),float(t1));records.append(r)
  dt=r[5]-r[4];total[r[1],r[0]]+=dt;counts[r[1],r[0]]+=1;kinds[r[0]]+=dt
  if r[0]==4:spu[r[1],r[2]]+=dt
  if r[0] in (6,7):sys[r[0],r[1],r[2],r[3]]+=dt
print(header)
print('records',len(records))
for (tid,k),dt in total.most_common(26):print('thread',tid,labels[k],round(dt/1000,2),'ms',counts[tid,k],'spans')
print('SPU entry time by thread/image:',[(tid,img,round(dt/1000,1)) for (tid,img),dt in spu.most_common(12)])
print('Top blocked syscall/HLE sites:',[(labels[k],tid,hex(a),hex(b),round(dt/1000,1)) for (k,tid,a,b),dt in sys.most_common(15)])
presents=sorted(r for r in records if r[0]==2)
ends=sorted(r[5] for r in presents)
def union(items):
 items=sorted(items);total=0;lo=hi=None
 for a,b in items:
  if lo is None:lo,hi=a,b
  elif a<=hi:hi=max(hi,b)
  else:total+=hi-lo;lo,hi=a,b
 return total+(hi-lo if lo is not None else 0)
frames=[]
if len(ends)>2:
 index={}
 for r in records:
  if r[0] in (1,2,3,4,5,8,10,11):
   start=max(0,bisect.bisect_right(ends,r[4])-1);stop=min(len(ends)-1,bisect.bisect_left(ends,r[5])+1)
   for i in range(start,stop):
    if ends[i]<r[5] and ends[i+1]>r[4]:index.setdefault(i,[]).append(r)
 for i,(a,b) in enumerate(zip(ends,ends[1:])):
  rs=index.get(i,[]);d={'start_us':a,'end_us':b,'duration_ms':(b-a)/1000}
  for k in (1,2,3,4,5,8,10,11):d[labels[k]+'_ms']=union((max(a,r[4]),min(b,r[5])) for r in rs if r[0]==k)/1000
  for image in sorted(set(r[2] for r in rs if r[0]==4)):
   d['SPU image '+str(image)+'_ms']=union((max(a,r[4]),min(b,r[5])) for r in rs if r[0]==4 and r[2]==image)/1000
  frames.append(d)
 keys=sorted(set(k for d in frames for k in d)-{'start_us','end_us'})
 print('Median frame breakdown (overlap is intentional):')
 for k in keys:print(k,round(statistics.median(d.get(k,0) for d in frames),3))
 print('Representative frames:',json.dumps(frames[len(frames)//2:len(frames)//2+3],indent=2))
args.trace.with_suffix('.report.json').write_text(json.dumps({'header':header,'records':len(records),'labels':labels,'frames':frames,'thread_kind_totals_ms':{str(t)+':'+labels[k]:dt/1000 for (t,k),dt in total.items()}},indent=2),encoding='utf-8')
