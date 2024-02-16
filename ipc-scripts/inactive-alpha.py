#!/usr/bin/python3
#
# This script demonstrates how Wayfire's IPC can be used to set the opacity of inactive views.

import os
from wayfire_socket import *

addr = os.getenv('WAYFIRE_SOCKET')
sock = WayfireSocket(addr)
sock.watch(['view-focused'])

last_focused_toplevel = -1
while True:
    msg = sock.read_message()
    # The view-mapped event is emitted when a new window has been opened.
    if "event" in msg:
        print(msg["event"])
        view = msg["view"]
        new_focus = view["id"] if view and view["type"] == "toplevel" else -1
        if last_focused_toplevel != new_focus:
            if last_focused_toplevel != -1 and new_focus != -1:
                try:
                    sock.set_view_alpha(last_focused_toplevel, 0.8)
                except:
                    print("Last focused toplevel was closed?")

            if new_focus != -1:
                sock.set_view_alpha(new_focus, 1.0)

            last_focused_toplevel = new_focus
