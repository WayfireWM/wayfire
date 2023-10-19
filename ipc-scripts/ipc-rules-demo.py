#!/usr/bin/python3
#
# This is a simple script which demonstrates how to use the Wayfire IPC socket with the wayfire_socket.py helper.
# To use this, make sure that the ipc plugin is first in the plugin list, so that the WAYFIRE_SOCKET environment
# variable propagates to all processes, including autostarted processes.
# To use this script, the ipc-rules plugin should also be enabled, as it provides some of the basic events and commands
# required for IPC interaction.
#
# Lastly, this script can be run from a terminal for testing purposes, or started as an autostart entry.
# It is safe to kill/restart the process at any point in time.

import os
from wayfire_socket import *

addr = os.getenv('WAYFIRE_SOCKET')

# Important: we connect to Wayfire's IPC two times. The one socket is used for reading events (view-mapped, view-focused, etc).
# The other is used for sending commands and querying Wayfire.
# We could use the same socket, but this would complicate reading responses, as events and query responses would be mixed with just one socket.
events_sock = WayfireSocket(addr)
commands_sock = WayfireSocket(addr)
events_sock.watch()

while True:
    msg = events_sock.read_message()
    # The view-mapped event is emitted when a new window has been opened.
    if "event" in msg and msg["event"] == "view-mapped":
        view = msg["view"]
        if view["app-id"] == "gedit":
            output_data = commands_sock.query_output(view["output"])
            print(output_data)
            workarea = output_data["workarea"]

            # We want gedit to take a certain position (200,200) and a quarter of the output
            x = 200
            y = 200
            w = workarea['width'] // 2
            h = workarea['height'] // 2

            commands_sock.configure_view(view["id"], x, y, w, h)
            # sock.assign_slot(view["id"], "slot_br")
            commands_sock.set_always_on_top(view["id"], True)
            commands_sock.set_view_alpha(view["id"], 0.5)
