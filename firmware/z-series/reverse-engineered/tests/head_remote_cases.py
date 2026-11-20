"""Original remote-session selection, quiet period and coffee payload cases."""
import itertools,struct
def number(value,unit):return list(struct.unpack('<II',struct.pack('<d',value)))+[unit]
def cases():
 out=[]
 def add(**updates):
  c=dict(command=46,program=1,session=6,recipe=765,name='Remote recipe',clock=20000,activity=0,quiet=10000,parameters=[23,6,8]+number(420,6)+number(2,1)+number(7,7)+number(83.5,4)+[1]);c.update(updates);out.append(c)
 for program,activity in itertools.product(range(30),[10000,10001,0]):add(program=program,activity=activity)
 for session,name in itertools.product(range(10),['','Remote café','A'*64]):add(program=24,session=session,name=name)
 for program,water,temperature in itertools.product(range(20,24),[0,1,2],[4,5,6]):
  parameters=[program,6,8]+number(420,8)+number(2,water)+number(7,8)+number(83.5,temperature)+[1];add(program=program,parameters=parameters)
 for command in [0,1,55]:add(command=command,program=0)
 for clock,activity,quiet in [(0,0xffffffff,1),(0,0xffffffff,2),(100,0xfffffff0,100),(100,0xfffffff0,117),(123,123,0)]:add(clock=clock,activity=activity,quiet=quiet)
 return out
