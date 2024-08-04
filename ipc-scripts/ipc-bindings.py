#!/usr/bin/python3

# This script demonstrates how to use custom bindings to achieve more complex command sequences in Wayfire
# The binding realised with this script is double pressing and releasing the left control button, which causes Expo to be activated.

import time
from wayfire.ipc import WayfireSocket

sock = WayfireSocket()

response = sock.register_binding('KEY_D', 'press', True)
print(response)
binding_id = response['binding-id']
last_release_time = 0
MAX_DELAY = 0.5

while True:
    msg = sock.read_next_event()
    print(msg)
    if "event" in msg and msg["event"] == "command-binding":
        assert msg['binding-id'] == binding_id

        now = time.time()
        if now - last_release_time <= MAX_DELAY:
            print("toggle")
            sock.toggle_expo()
            last_release_time = now - 2 * MAX_DELAY # Prevent triple press
        else:
            print("reset")
            last_release_time = now
