#!/usr/bin/python3

"""Example script that moves all the windows for a given app ID to the specified workspace.

    Usage: move-to-workspace <app-id> <workspace_x> <workspace_y>"
"""

import os
import sys

from wayfire_socket import *


def move_to_workspace(s: WayfireSocket, view, ws_x: int, ws_y: int):
    output = s.query_output(view["output-id"])
    xsize = output["workarea"]["width"]
    ysize = output["workarea"]["height"]
    cur_ws_x = output["workspace"]["x"]
    cur_ws_y = output["workspace"]["y"]

    geometry = view["geometry"]
    x = (geometry["x"] % xsize) + xsize * (ws_x - cur_ws_x)
    y = (geometry["y"] % ysize) + ysize * (ws_y - cur_ws_y)
    w = geometry["width"]
    h = geometry["height"]

    s.configure_view(view["id"], x, y, w, h)


def move_all_to_workspace(app_id, x, y):
    s = WayfireSocket(os.getenv("WAYFIRE_SOCKET"))
    for view in s.list_views():
        if view["app-id"] != app_id:
            continue
        print("Moving %s(%d) to workspace (%d, %d)" %
              (app_id, view["id"], x, y))
        move_to_workspace(s, view, x, y)
    s.close()


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(sys.modules[__name__].__doc__)
    move_all_to_workspace(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]))
