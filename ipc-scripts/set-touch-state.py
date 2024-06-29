from wayfire.ipc import WayfireSocket
import sys

# This is a small example of how to use the Wayfire socket to set the touchscreen state to on or off.
# Can for example be used as a custom command in Xournalpp.
# Usage: set-touch-state.py <enabled|disabled>

if len(sys.argv) != 2 or sys.argv[1] not in ['enabled', 'disabled']:
    print("Invalid usage, exactly one argument required, either 'enabled' or 'disabled'!")
    exit(-1)

state: bool = sys.argv[1] == 'enabled'
sock = WayfireSocket()

devices = sock.list_input_devices()
for dev in devices:
    if dev['type'] == 'touch':
        print(sock.configure_input_device(dev['id'], state))
