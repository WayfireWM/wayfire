from wayfire.ipc import WayfireSocket
import argparse
import tabulate

# Usage:
# input.py list-inputs
# input.py enable <id>
# input.py disable <id>

# argparse is used to parse the command line arguments
parser = argparse.ArgumentParser()
parser.add_argument('action', choices=['list-inputs', 'enable', 'disable'])
parser.add_argument('device', nargs='?', default=None)
args = parser.parse_args()

if args.action == 'list-inputs' and args.device is not None:
    print('Invalid usage, no arguments allowed when listing inputs!')
    exit(-1)

if args.device is None and args.action != 'list-inputs':
    print('Invalid usage, an input id >= 0 is required!')
    exit(-1)

sock = WayfireSocket()
devices = sock.list_input_devices()

def find_device_id(name_or_id_or_type):
    for dev in devices:
        if dev['name'] == name_or_id_or_type or str(dev['id']) == name_or_id_or_type or dev['type'] == name_or_id_or_type:
            return dev['id']
    return None

if args.action == 'list-inputs':

    headers = ["Name", "ID", "Type", "Vendor/Product", "Enabled"]
    data = []
    for dev in devices:
        data.append([
            dev['name'],
            str(dev['id']),
            dev['type'],
            str(dev['vendor']) + ', ' + str(dev['product']),
            dev['enabled']
        ])

    print(tabulate.tabulate(data, headers))

elif args.action == 'enable':
    print(sock.configure_input_device(find_device_id(args.device), True))
elif args.action == 'disable':
    print(sock.configure_input_device(find_device_id(args.device), False))
