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
  data['config']['debug-on'] = {}
  data['config']['default-pin'] = {}
  data['config']['apn'] = {}
  data['config']['username'] = {}
  data['config']['password'] = {}
  data['config']['ntp-server'] = {}
  data['config']['ntp-port'] = {}
  
  data['config']['debug-on']['value'] = True
  data['config']['default-pin'] = '\"1234\"'
  data['config']['apn'] = 0
  data['config']['username'] = 0
  data['config']['password'] = 0
  data['config']['ntp-server'] = '\"2.pool.ntp.org\"'
  data['config']['ntp-port'] = 123
  
  data['target_overrides']['*'] = {}
  data['target_overrides']['*']['lwip.ppp-enabled'] = True
  data['target_overrides']['*']['platform.stdio-convert-newlines'] = True
  
  
  print('data', data)
else:
  data['config']['debug-on']['value'] = True

  print('debug-on', data['config']['debug-on']['value'])

with open(myfile, 'w') as f:
  json.dump(data, f)
