import json
from time import sleep
import sys
import re
import os

myfile = os.path.join('example-ublox-cellular-interface','mbed_app.json')
#with open(myfile, 'r') as f:
#  data = json.load(f)

data={}
  
if 'config' not in data:
  data['config'] = {}
  data['target_overrides'] = {}
  data['config']['echo-udp-port'] ={}  
  data['config']['echo-server'] ={}  
  data['config']['echo-tcp-port']={}  
  data['config']['debug-on'] = {}
  data['config']['run-sim-pin-change-tests'] = {}
  data['config']['default-pin'] = {}
  data['config']['apn'] = {}
  data['config']['username'] = {}
  data['config']['password'] = {}
  data['config']['alt-pin'] = {}
  data['config']['incorrect-pin'] = {}
  data['config']['ntp-server'] = {}
  data['config']['ntp-port'] = {}
  data['config']['local-port'] = {}
  data['config']['udp-max-packet-size'] = {}
  data['config']['udp-max-frag-packet-size'] = {}  
  
  #data['config']['echo-server']['value'] = '\"echo.u-blox.com\"'
  data['config']['echo-server']['value'] = '\"ciot.it-sgn.u-blox.com\"'
  data['config']['echo-udp-port']['value'] = 7
  data['config']['echo-tcp-port']['value'] = 7
  data['config']['echo-udp-port']['value'] = 5050
  data['config']['echo-tcp-port']['value'] = 5055
  data['config']['debug-on']['value'] = True
  data['config']['run-sim-pin-change-tests'] = 0
  data['config']['default-pin'] = '\"1234\"'
  data['config']['apn'] = 0
  data['config']['username'] = 0
  data['config']['password'] = 0
  data['config']['alt-pin'] = '\"9876\"'
  data['config']['incorrect-pin'] = '\"1530\"'
  data['config']['ntp-server'] = '\"2.pool.ntp.org\"'
  data['config']['ntp-port'] = 123
  data['config']['local-port'] = 16
  data['config']['udp-max-packet-size'] = 508
  data['config']['udp-max-frag-packet-size'] = 1500
  
  data['target_overrides']['*'] = {}
  data['target_overrides']['*']['lwip.ppp-enabled'] = True
  data['target_overrides']['*']['platform.stdio-convert-newlines'] = True
  
  
  print('data', data)
else:
  data['config']['echo-server']['value'] = '\"ciot.it-sgn.u-blox.com\"'
  data['config']['echo-udp-port']['value'] = 5050
  data['config']['echo-tcp-port']['value'] = 5055
  data['config']['debug-on']['value'] = True

  print('echo-server',data['config']['echo-server']['value'])
  print('echo-udp-port',data['config']['echo-udp-port']['value'])
  print('echo-tcp-port',data['config']['echo-tcp-port']['value'])
  print('debug-on', data['config']['debug-on']['value'])

with open(myfile, 'w') as f:
  json.dump(data, f)
