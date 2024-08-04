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

from wayfire.ipc import WayfireSocket

sock = WayfireSocket()
sock.watch(['view-mapped'])

while True:
    msg = sock.read_next_event()
    # The view-mapped event is emitted when a new window has been opened.
    if "event" in msg:
        view = msg["view"]
        if view["app-id"] == "gedit":
            output_data = sock.query_output(view["output"])
            print(output_data)
            workarea = output_data["workarea"]

            # We want gedit to take a certain position (200,200) and a quarter of the output
            x = 200
            y = 200
            w = workarea['width'] // 2
            h = workarea['height'] // 2

            sock.configure_view(view["id"], x, y, w, h)
            # sock.assign_slot(view["id"], "slot_br")
            sock.set_view_always_on_top(view["id"], True)
            sock.set_view_alpha(view["id"], 0.5)
