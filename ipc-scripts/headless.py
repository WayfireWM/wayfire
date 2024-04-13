import wayfire_socket as ws
import os
import sys
import json

addr = os.getenv('WAYFIRE_SOCKET')
sock = ws.WayfireSocket(addr)

if sys.argv[1] == "add":
    output = sock.create_headless_output(int(sys.argv[2]), int(sys.argv[3]))
    if output and 'output' in output:
        print("Created headless output:\n" + json.dumps(output['output'], indent=4))
    else:
        print("Failed to create headless output: " + json.dumps(output, indent=4))

elif sys.argv[1] == "remove":
    if sys.argv[2].isdigit():
        print(sock.destroy_headless_output(output_id=int(sys.argv[2])))
    else:
        print(sock.destroy_headless_output(output_name=sys.argv[2]))

else:
    print("Invalid usage, either add <width> <height> or remove <output-id|output-name>")
