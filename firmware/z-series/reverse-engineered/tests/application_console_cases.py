"""Deterministic callback inputs; no live device, network, or credential use."""

def command_cases():
    rows=[]
    def add(kind, shell, token, address, arg='', **extra):
        row=dict(id=f'{kind}-{len(rows)}',kind=kind,direct_shell=shell,direct_token=token,
                 direct_argument=arg,arg=arg,function=f'0x{address:08x}',**extra)
        rows.append(row)
    numeric=['','0','1','99','100','101','-1','-2147483648','2147483647','256','257','  +52tail','invalid']
    for automatic in (False,True):
        for arg in numeric:add('duty','Heater','setduty',0x400de1b0,arg,automatic=automatic)
    for token,address in [('scan',0x400d5c20),('ap',0x400d5c10),('idle',0x400d5c30)]:add('wifi','WiFi',token,address)
    for token,address in [('on',0x401bf96c),('off',0x400d6a10),('sf in',0x400d6a80),('sf out',0x400d6a90)]:add('safety','Safeties',token,address)
    commands=[('resume',0x400f4a84),('skip',0x400f4a94),('drain',0x400f4aa4),('s rinse',0x400f4b4c),('s clean',0x400f4b5c),('s drain',0x400f4b6c),('s rack',0x400f4b7c),('s circ',0x400f4b8c),('s sous',0x400f4b9c),('s test1',0x400f4bac),('s test2',0x400f4bbc),('s test3',0x400f4bcc),('s BREW!',0x400f4bdc)]
    for occupied in (False,True):
        for token,address in commands:add('runtime','Runtime',token,address,occupied=occupied)
        for token,address in [('sett_c ',0x400f4adc),('sett_f ',0x400f4b00)]:
            for arg in numeric:add('runtime','Runtime',token,address,arg,occupied=occupied)
        for token,address in [('scan',0x400f4f08),('start',0x400f4f34),('stop',0x400f4f8c)]:add('still','PicoStill',token,address,occupied=occupied)
        for arg in numeric:add('still','PicoStill','connect ',0x400f4f18,arg,occupied=occupied)
    for token,address in [('start',0x401bdcdc),('stop',0x401bdd00)]:add('adc','MAX11613',token,address)
    add('session','ZSessionLogger','end',0x401bdc88)
    for enabled in (False,True):
        for token,address in [('t1 start',0x400dafbc),('t1 stop',0x400daf4c)]:add('t1','Runtime',token,address,t1_enabled=enabled)
    for token,address in [('start',0x400dc0bc),('stop',0x400dc0cc),('rel',0x400dc48c),('pos',0x400dc4a0)]:add('msp','Servos',token,address)
    for token,address in [(f'{i+1} ',0x400dc3ac+i*28) for i in range(8)]+[('open ',0x400dc4b0),('close ',0x400dc4cc),('all ',0x400dc4e8)]:
        for arg in numeric:add('msp','Servos',token,address,arg)
    return rows

if __name__=='__main__':
    import json,sys
    json.dump(command_cases(),sys.stdout,indent=2);print()
