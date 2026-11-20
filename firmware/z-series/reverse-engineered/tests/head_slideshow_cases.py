import itertools
WORDS=[('started',0x2c1,'o.heater.started',1,1),('elapsed',0x368,'o.runtime.elapsed_seconds',8345,4),('left',0x37c,'o.runtime.instruction_left_seconds',234,4),('total_left',0x3a0,'o.runtime.total_left_seconds',4500,4),('pid',0x361,'o.runtime.pid_active',1,1),('flag_b5',0x3a9,'o.runtime.flag_b5',0,1),('reference_date',0x478,'o.runtime.still.reference_date',1000,4),('boil_date',0x4a0,'o.runtime.still.boil_date',2000,4),('t1_enabled',0x4cc,'o.runtime.t1_drop_watch.enabled',1,1),('t1_triggered',0x4cd,'o.runtime.t1_drop_watch.triggered',0,1),('network',0x7e0,'o.network',2,4),('wifi',0x808,'o.wifi.id',4,4),('offline',0xe,'o.core.offline',0,1),('service',0xc,'o.core.service_mode',0,1),('program',0x1c,'o.core.program_id',2,4),('pump',0x980,'o.still_samples.current.pump_running',1,1),('t1_count',0xc50,'o.still_samples.t1.count',5,4),('t2_count',0xd80,'o.still_samples.t2.count',5,4),('pressure_count',0xeb0,'o.still_samples.pressure.count',5,4)]
DOUBLES=[('target',0x2b0,'o.heater.target_celsius',81.5),('hex',0x548,'o.heater_sensors.temperatures[0].celsius',91.25),('wort',0x558,'o.heater_sensors.temperatures[1].celsius',78.5),('drain',0x568,'o.heater_sensors.temperatures[2].celsius',23.4),('ambient',0x578,'o.heater_sensors.temperatures[3].celsius',19.75),('wort_rate',0x588,'o.heater_sensors.wort_rate',1.125),('hex_rate',0x590,'o.heater_sensors.hex_rate',-2.875),('pressure_pa',0x778,'o.auxiliary.pressure_pa',98765),('boiling',0x7a0,'o.heater_sensors.estimated_boil_celsius',98.1),('t1',0x958,'o.still_samples.current.temperature[0]',78.75),('t2',0x960,'o.still_samples.current.temperature[1]',83.15),('t3',0x968,'o.still_samples.current.temperature[2]',86.25),('pressure',0x978,'o.still_samples.current.pressure',-2.125),('pressure_speed',0x9f8,'o.still_samples.current.speed[4]',-2.5),('t1_speed',0xa88,'o.still_samples.seconds10.speed[0]',0.4),('t2_speed',0xa90,'o.still_samples.seconds10.speed[1]',-0.6),('t3_speed',0xa98,'o.still_samples.seconds10.speed[2]',0.85),('reference_wort',0x480,'o.runtime.still.reference_wort',90.5),('heat_loss',0x490,'o.runtime.still.heat_loss',4.75),('boil_t3',0x498,'o.runtime.still.boil_t3',85.15),('boil_pressure',0x4a8,'o.runtime.still.boil_scalar_978',-1.5),('maximum',0x4d0,'o.runtime.t1_drop_watch.maximum',90.25),('t1_variance',0xc68,'o.still_samples.t1.squared_deviations',1),('t2_variance',0xd98,'o.still_samples.t2.squared_deviations',4),('pressure_variance',0xec8,'o.still_samples.pressure.squared_deviations',0.01)]
FLOATS=[('reference_pressure',0x784,'o.reference_pressure_pa',101325.0)]
def cases():
 rows=[]
 def add(**updates):
  c=dict(operation=0,imperial=False,language=0,type=1,counter=8345,name='A pleasant recipe',step='Mash',substep='Circulating',mode=7,detail=False,static=False,select=0,clock=777,tick=0,tick2=0,change=0);c.update({f[0]:f[3]for f in WORDS+DOUBLES+FLOATS});c.update(updates);rows.append(c)
 for t,imperial,language in itertools.product(range(1,19),(False,True),range(3)):add(type=t,imperial=imperial,language=language)
 for t in (0,19,20):add(type=t)
 for value in (0,1,59,60,3599,3600,35999,36000,359999,360000,0xffffffff):add(type=2,counter=value)
 for t in (4,5,6):
  for started,target in ((0,80),(1,0),(1,0.09),(1,0.1)):add(type=t,started=started,target=target)
 for name in ('','012345678901234567890','012345678901234567890123456789','éçüäß a lengthier name here'):add(type=1,name=name)
 for step,substep,counter in (('','',0),('Mash','',4500),('Mash','Pause',4499),('Mash','',4501),('Mash','',4502),('A very long step name','Next',3599999)):add(type=7,step=step,substep=substep,counter=counter)
 for t in (12,13,15,17):
  for v in (-1,0,0.1,0.2,0.6,0.7,0.8,1,5):add(type=t,t1_speed=v,t2_speed=-v,t3_speed=v,pressure_speed=v,t1_variance=4*v*v,t2_variance=v*v,pressure_variance=v*v/100)
 for enabled,triggered in itertools.product((0,1),(0,1)):add(type=16,t1_enabled=enabled,t1_triggered=triggered)
 for reference,boil,imperial in itertools.product((0,1),(0,1),(False,True)):add(type=14,reference_date=reference,boil_date=boil,imperial=imperial)
 for mode in range(20):add(operation=1,mode=mode)
 for mode,network,detail,static in itertools.product((4,7,11,13),(0,2),(False,True),(False,True)):add(operation=1,mode=mode,network=network,detail=detail,static=static)
 for pump,program,offline,language in itertools.product((0,1),(2,7),(0,1),range(3)):add(operation=1,mode=11,pump=pump,program=program,offline=offline,network=0,detail=True,language=language)
 for mode,selected in itertools.product((4,7,11),(1,2,3,11,12,0xffffffff)):add(operation=1,mode=mode,select=selected)
 for selected,now in itertools.product((0,1,2),(1276,1277,1776,1777,4776,4777,4778)):add(operation=1,mode=4,select=selected,tick=now)
 for change in range(1,8):add(operation=1,mode=11,change=change,tick=900)
 for mode in (4,7,11):add(operation=1,mode=mode,clock=0xfffffffc,tick=1,tick2=5000)
 for left in (0,1,2):add(operation=1,mode=7,left=left,tick=1777,tick2=2777)
 for operation in (2,3,4):add(operation=operation)
 for language in (1,2):add(operation=3,language=language)
 for large in (99,100,1000):add(type=15,t1_variance=4*large*large)
 return rows
