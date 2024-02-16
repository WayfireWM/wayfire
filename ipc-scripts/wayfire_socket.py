import socket
import json as js

def get_msg_template(method: str):
    # Create generic message template
    message = {}
    message["method"] = method
    message["data"] = {}
    return message

def geometry_to_json(x: int, y: int, w: int, h: int):
    geometry = {}
    geometry["x"] = x
    geometry["y"] = y
    geometry["width"] = w
    geometry["height"] = h
    return geometry

class WayfireSocket:
    def __init__(self, socket_name):
        self.client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.client.connect(socket_name)

    def read_exact(self, n):
        response = bytes()
        while n > 0:
            read_this_time = self.client.recv(n)
            if not read_this_time:
                raise Exception("Failed to read anything from the socket!")
            n -= len(read_this_time)
            response += read_this_time

        return response

    def read_message(self):
        rlen = int.from_bytes(self.read_exact(4), byteorder="little")
        response_message = self.read_exact(rlen)
        response = js.loads(response_message)
        if "error" in response:
            raise Exception(response["error"])
        return response

    def send_json(self, msg):
        data = js.dumps(msg).encode('utf8')
        header = len(data).to_bytes(4, byteorder="little")
        self.client.send(header)
        self.client.send(data)
        return self.read_message()

    def close(self):
      self.client.close()

    def watch(self, events = None):
        message = get_msg_template("window-rules/events/watch")
        if events:
            message["data"]["events"] = events
        return self.send_json(message)

    def query_output(self, output_id: int):
        message = get_msg_template("window-rules/output-info")
        message["data"]["id"] = output_id
        return self.send_json(message)

    def list_views(self):
        return self.send_json(get_msg_template("window-rules/list-views"))

    def configure_view(self, view_id: int, x: int, y: int, w: int, h: int):
        message = get_msg_template("window-rules/configure-view")
        message["data"]["id"] = view_id
        message["data"]["geometry"] = geometry_to_json(x, y, w, h)
        return self.send_json(message)

    def assign_slot(self, view_id: int, slot: str):
        message = get_msg_template("grid/" + slot)
        message["data"]["view_id"] = view_id
        return self.send_json(message)

    def set_focus(self, view_id: int):
        message = get_msg_template("window-rules/focus-view")
        message["data"]["id"] = view_id
        return self.send_json(message)

    def set_always_on_top(self, view_id: int, always_on_top: bool):
        message = get_msg_template("wm-actions/set-always-on-top")
        message["data"]["view_id"] = view_id
        message["data"]["state"] = always_on_top
        return self.send_json(message)

    def set_view_alpha(self, view_id: int, alpha: float):
        message = get_msg_template("wf/alpha/set-view-alpha")
        message["data"] = {}
        message["data"]["view-id"] = view_id
        message["data"]["alpha"] = alpha
        return self.send_json(message)

    def list_input_devices(self):
        message = get_msg_template("input/list-devices")
        return self.send_json(message)

    def configure_input_device(self, id, enabled: bool):
        message = get_msg_template("input/configure-device")
        message["data"]["id"] = id
        message["data"]["enabled"] = enabled
        return self.send_json(message)
