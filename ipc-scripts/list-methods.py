import wayfire_socket as ws
import os
import json

addr = os.getenv('WAYFIRE_SOCKET')
sock = ws.WayfireSocket(addr)

query = ws.get_msg_template('wayfire/configuration')
response = sock.send_json(query)
print("Wayfire version:")
print(json.dumps(response, indent=4))

query = ws.get_msg_template('list-methods')
response = sock.send_json(query)
print("Supported methods:")
print(json.dumps(response['methods'], indent=4))
