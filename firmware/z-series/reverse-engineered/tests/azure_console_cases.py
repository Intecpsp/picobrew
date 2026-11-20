"""Explicit synthetic MQTT/clock/crypto conditions for original Azure diagnostics."""
import copy
def cases():
    base=dict(id='live-listening',words=[0,100,7,2,1,1,1,1,0,2000,50,0,0xfffffff9,0,0,0,60000,1800000,41,9876])
    out=[base]
    def add(name,changes):
        item=copy.deepcopy(base);item['id']=name
        for index,value in changes.items():item['words'][index]=value
        out.append(item)
    for state in [0,1,3,4,9]:add('live-state-%d'%state,{3:state})
    add('live-disabled',{4:0});add('live-disconnected',{5:0});add('factory-unavailable',{6:0})
    add('mqtt-connect-fails',{7:0});add('mqtt-ignore-disconnect',{8:1})
    for stack in [0,700,701]:add('stack-%d'%stack,{9:stack})
    add('counter-wrap',{2:0xffffffff,9:700});add('clock-wrap',{1:0xffffff00});add('clock-zero',{1:0})
    for duration in [0,1,100,1000]:add('retry-duration-%d'%duration,{7:0,16:duration})
    for call in [1,2,0xffffffff]:add('live-hmac-fails-%d'%call,{11:call})
    add('live-hmac-corrupted',{13:1})
    for host in [1,4]:add('live-host-%d'%host,{14:host})
    add('loaded-deadline-near-wrap',{1:0x100000000-30400,3:0,10:0})
    add('zero-loaded-deadline',{1:0x100000000-30000,3:0,10:0})
    add('password-vectors',{0:1})
    for host in [1,2,3,4]:add('password-host-%d'%host,{0:1,14:host})
    for call in [1,2,0xffffffff]:add('password-hmac-fails-%d'%call,{0:1,11:call})
    add('password-wrong-key',{0:1,15:1});add('password-corrupted-hmac',{0:1,13:1})
    add('password-counter-wrap',{0:1,2:0xffffffff,15:1})
    return out
