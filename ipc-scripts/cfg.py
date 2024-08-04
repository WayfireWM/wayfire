from wayfire.ipc import WayfireSocket
import sys

sock = WayfireSocket()

if sys.argv[1] == "get":
    opt = sock.get_option_value(sys.argv[2])
    if 'value' in opt:
        print(f"Current value='{opt['value']}', default='{opt['default']}'")
    else:
        print("Option not found: " + sys.argv[2])

elif sys.argv[1] == "set":
    print(sock.set_option_values({sys.argv[2] : sys.argv[3]}))
else:
    print("Invalid usage, either get <option> or set <option> <value>")
