import itertools
WORDS=[('still_id',0x3c0,'o.runtime.still.id',7,4),('t1_enabled',0x4cc,'o.runtime.t1_drop_watch.enabled',1,1),('t1_triggered',0x4cd,'o.runtime.t1_drop_watch.triggered',0,1)]
DOUBLES=[('t1',0x958,'o.still_samples.current.temperature[0]',78.75),('t2',0x960,'o.still_samples.current.temperature[1]',83.15),('maximum',0x4d0,'o.runtime.t1_drop_watch.maximum',90.25)]
def cases():
 rows=[]
 def add(**updates):
  c=dict(operation=0,imperial=False,language=0,extra=1,rect=[0,14,127,63]);c.update({f[0]:f[3]for f in WORDS+DOUBLES});c.update(updates);rows.append(c)
 for imperial,extra,triggered in itertools.product((False,True),(0,1),(0,1)):add(imperial=imperial,extra=extra,t1_triggered=triggered)
 for imperial,language,still in itertools.product((False,True),range(3),range(9)):add(operation=1,imperial=imperial,language=language,still_id=still)
 for rect in ([0,0,127,63],[4,8,123,51],[0,30,127,63]):
  add(rect=rect);add(operation=1,rect=rect,still_id=7);add(operation=1,rect=rect,still_id=8)
 return rows
