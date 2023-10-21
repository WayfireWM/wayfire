#!/usr/bin/python3

# This script demonstrates how to use custom bindings to achieve more complex command sequences in Wayfire
# The binding realised with this script is double pressing and releasing the left control button, which causes Expo to be activated.

import os
import time
from wayfire_socket import *

addr = os.getenv('WAYFIRE_SOCKET')

sock = WayfireSocket(addr)

response = sock.register_binding('<alt>', 'press', True)
binding_id = response['binding-id']
last_release_time = 0
MAX_DELAY = 0.5

while True:
    msg = sock.read_message()
    if "event" in msg and msg["event"] == "command-binding":
        assert msg['binding-id'] == binding_id

        now = time.time()
        if now - last_release_time <= MAX_DELAY:
            msg = get_msg_template("expo/toggle")
            sock.send_json(msg)

            last_release_time = now - 2 * MAX_DELAY # Prevent triple press
        else:
            last_release_time = now
