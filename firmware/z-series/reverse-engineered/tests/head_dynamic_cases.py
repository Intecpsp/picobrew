import itertools
WORDS=[('session',0x14,'o.core.session_type',6,4),('started',0x2c1,'o.heater.started',1,1),('msp_status',0x2e5,'o.msp.status_byte_3',3,1),('valve',0x2e3,'o.msp.valve_position',4,1),('moving',0x2ec,'o.msp.moving_valve',0,1),('pumps',0x2e1,'o.msp.pumps',0x70,1),('fan',0x2e2,'o.msp.fan',8,1),('errors',0x2e8,'o.msp.errors',0,4),('filter',0x526,'o.safety.filter_missing',0,1),('runtime',0x2f4,'o.runtime.id',3,4),('elapsed',0x368,'o.runtime.elapsed_seconds',8345,4),('left',0x37c,'o.runtime.instruction_left_seconds',234,4),('count',0x374,'o.runtime.instruction_count',23,4),('opcode',0x36c,'o.runtime.instruction.opcode',4,4),('argument',0x370,'o.runtime.instruction.argument',600,4),('still_id',0x944,'o.still.id',19,4),('connectivity',0x107c,'o.still_connectivity_error',0,1)]
DOUBLES=[('target',0x2b0,'o.heater.target_celsius',81.5),('output',0x2c8,'o.heater.output_ticks',45.5),('hex',0x548,'o.heater_sensors.temperatures[0].celsius',91.25),('wort',0x558,'o.heater_sensors.temperatures[1].celsius',78.5),('drain',0x568,'o.heater_sensors.temperatures[2].celsius',23.4),('ambient',0x578,'o.heater_sensors.temperatures[3].celsius',19.75),('x',0x788,'o.auxiliary.acceleration[0]',1),('y',0x790,'o.auxiliary.acceleration[1]',0.03),('z',0x798,'o.auxiliary.acceleration[2]',0.04)]
def cases():
 out=[]
 def add(**updates):
  c=dict(context_font=4,context_spacing=0,operation=0,imperial=False,language=0,reason=1,rect=[0,0,127,63],name='Recipe name',step='Mash',substep='Circulating',error='Still sample error')
  c.update({f[0]:f[3]for f in WORDS+DOUBLES});c.update(updates);out.append(c)
 for imperial,pumps,moving in itertools.product([False,True],[0,16,64,112],[False,True]):add(imperial=imperial,pumps=pumps,moving=moving,errors=257 if moving else 0,filter=moving,started=not moving)
 for op in range(58):
  argument={2:80*256,6:4,7:7,25:93*256,27:5,28:0x50,29:1,30:81,31:81,35:10*256,36:90*256}.get(op,600)
  add(operation=4,opcode=op,argument=argument)
 for session,step,opcode in itertools.product([6,7],['','Mash'],[2,4,13,30]):add(operation=1,session=session,step=step,opcode=opcode,argument={2:80*256,30:81}.get(opcode,600),rect=[0,14,127,63])
 for reason,language,imperial in itertools.product(range(20),[0,2],[False,True]):add(operation=2,reason=reason,language=language,imperial=imperial)
 for reason in [1,2,4,6,12,15]:
  add(operation=2,reason=reason,rect=[0,14,127,48],name='<None>',step='',substep='',connectivity=True,errors=0xffffffff)
 for x,y in itertools.product([-1,-0.1,0,0.1,1],[-0.2,0,0.2]):add(operation=3,y=x,z=y)
 for font in range(5):add(operation=3,context_font=font,context_spacing=1)
 return out
