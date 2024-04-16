import wayfire_socket as ws
import os
import json
import argparse

addr = os.getenv('WAYFIRE_SOCKET')
sock = ws.WayfireSocket(addr)

parser = argparse.ArgumentParser()
subparsers = parser.add_subparsers(dest='action')

add = subparsers.add_parser('add')
add.add_argument('width', nargs='?', type=int, default=1920)
add.add_argument('height', nargs='?', type=int, default=1080)

remove = subparsers.add_parser('remove')
remove.add_argument('output')

args = parser.parse_args()
if args.action == "add":
    output = sock.create_headless_output(int(args.width), int(args.height))
    if output and 'output' in output:
        print("Created headless output:\n" + json.dumps(output['output'], indent=4))
    else:
        print("Failed to create headless output: " + json.dumps(output, indent=4))

elif args.action == "remove":
    if args.output.isdigit():
        print(sock.destroy_headless_output(output_id=int(args.output)))
    else:
        print(sock.destroy_headless_output(output_name=args.output))

else:
    print("Invalid usage, either add <width> <height> or remove <output-id|output-name>")
