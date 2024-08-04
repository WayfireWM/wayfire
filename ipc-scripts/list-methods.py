from wayfire.ipc import WayfireSocket
import json

sock = WayfireSocket()

print("Wayfire version:")
print(json.dumps(sock.get_configuration(), indent=4))

print("Supported methods:")
print(json.dumps(sock.list_methods(), indent=4))
