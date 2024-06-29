#!/usr/bin/python3

# A simple script which demonstrates how simple-tile's IPC scripting capabilities can be used to achieve automatic tiling policies.
# This script in particular listens for the view-mapped event and places new views in a master-stack layout: one view remains on the left,
# and all other views are piled on top of each other vertically in the right column.
from wayfire.ipc import WayfireSocket

sock = WayfireSocket()
sock.watch(['view-mapped'])

def create_list_views(layout):
    if 'view-id' in layout:
        return [(layout['view-id'], layout['geometry']['width'], layout['geometry']['height'])]

    split = 'horizontal-split' if 'horizontal-split' in layout else 'vertical-split'
    list = []
    for child in layout[split]:
        list += create_list_views(child)
    return list

while True:
    msg = sock.read_next_event()
    # The view-mapped event is emitted when a new window has been opened.
    if "event" in msg:
        view = msg["view"]
        if view["type"] == "toplevel" and view["parent"] == -1:
            # Tile the new view appropriately

            # First, figure out current wset and workspace and existing layout
            output = sock.query_output(view["output-id"])
            wset = output['wset-index']
            wsx = output['workspace']['x']
            wsy = output['workspace']['y']
            layout = sock.get_tiling_layout(wset, wsx, wsy)
            all_views = create_list_views(layout)

            desired_layout = {}
            if not all_views or (len(all_views) == 1 and all_views[0][0] == view["id"]):
                # Degenerate case 1: we have just our view
                desired_layout = { 'vertical-split': [ {'view-id': view["id"], 'weight': 1} ]}
                sock.set_tiling_layout(wset, wsx, wsy, desired_layout)
                continue

            main_view = all_views[0][0]
            weight_main = all_views[0][1]
            stack_views_old = [v for v in all_views[1:] if v[0] != view["id"]]
            weight_others = max([v[1] for v in stack_views_old], default=output['workarea']['width'] - weight_main)

            if main_view == view["id"]:
                print("New view is the master, how did this happen???")
                continue

            if not stack_views_old:
                # Degenerate case 2: the new view is the first on the stack, set 2:1 ratio and place the new view on the right
                desired_layout = { 'vertical-split': [ {'view-id': main_view, 'weight': 2}, {'view-id': view["id"], 'weight': 1} ]}
                sock.set_tiling_layout(wset, wsx, wsy, desired_layout)
                continue

            stack = [{'view-id': v[0], 'weight': v[2]} for v in stack_views_old]
            stack += [{'view-id': view["id"], 'weight': sum([v[2] for v in stack_views_old]) / len(stack_views_old)}]

            desired_layout = {
                    'vertical-split': [
                        {'weight': weight_main, 'view-id': main_view},
                        {'weight': weight_others, 'horizontal-split': stack}
                    ]
            }

            sock.set_tiling_layout(wset, wsx, wsy, desired_layout)
