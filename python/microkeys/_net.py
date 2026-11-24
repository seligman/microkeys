# Messages between machines: the pairing token, the signed wire format, the
# listener thread, and sending.  net.py is the part scripts use.
#
# Each message is one line of JSON holding the message itself (as a string)
# and its HMAC-SHA256 signature, keyed with the pairing token.  The receiver
# checks the signature, drops messages whose timestamp is far off or whose
# nonce it has already seen, and answers with one line of JSON.  That gives
# integrity, not secrecy; a VPN such as Tailscale adds encryption.

import base64
import hashlib
import hmac
import json
import os
import secrets
import socket
import threading
import time

from ._native import native, LOG_INFO, LOG_ERROR, FIRE_MESSAGE

TOKEN_PREFIX = "mknet1-"
TOKEN_FILE = "net_token.txt"
DEFAULT_PORT = 47913
# How far a message's clock may be from ours, and how long nonces are kept
MAX_SKEW = 300
NONCE_KEEP = 2 * MAX_SKEW
MAX_LINE = 1024 * 1024
CONNECT_TIMEOUT = 3.0
REPLY_TIMEOUT = 10.0
RESOLVE_MS = 1500

_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
# Characters people misread, and what they probably meant
_LOOKALIKES = {"0": "O", "1": "I", "8": "B", "9": "G"}


class NetError(Exception):
    """A message couldn't be delivered: the machine couldn't be reached, or
    it turned the message down."""


# Pairing tokens: "mknet1-", 26 base32 characters (128 random bits), and a
# 4-character checksum, so a typo is caught when it's entered

def _checksum(body):
    bits = int.from_bytes(hashlib.sha256(body.encode("ascii")).digest()[:3], "big") >> 4
    return "".join(_ALPHABET[(bits >> shift) & 31] for shift in (15, 10, 5, 0))


def new_token():
    body = base64.b32encode(secrets.token_bytes(16)).decode("ascii").rstrip("=")
    return TOKEN_PREFIX + body + _checksum(body)


def check_token(text):
    # The token in its standard form, or ValueError saying what's wrong
    token = "".join(str(text).split())
    if not token.lower().startswith(TOKEN_PREFIX):
        raise ValueError(f"A pairing token starts with {TOKEN_PREFIX}")
    rest = token[len(TOKEN_PREFIX):].upper()
    if len(rest) != 30:
        raise ValueError(f"A pairing token has 30 characters after {TOKEN_PREFIX}, and this one has {len(rest)}")
    for ch in rest:
        if ch not in _ALPHABET:
            hint = f" (perhaps {_LOOKALIKES[ch]}?)" if ch in _LOOKALIKES else ""
            raise ValueError(f"{ch!r} can't be in a pairing token{hint}")
    if _checksum(rest[:26]) != rest[26:]:
        raise ValueError("This pairing token has a typo in it: its last four characters don't match the rest")
    return TOKEN_PREFIX + rest


def token_path(data_dir):
    return os.path.join(data_dir, TOKEN_FILE)


def read_token(data_dir):
    # The saved token, or None when networking is off
    try:
        with open(token_path(data_dir), "rt", encoding="utf-8") as f:
            return check_token(f.read())
    except (OSError, ValueError):
        return None


def save_token(data_dir, token):
    fn = token_path(data_dir)
    if token is None:
        if os.path.isfile(fn):
            os.unlink(fn)
        return
    tmp = fn + ".tmp"
    with open(tmp, "wt", encoding="utf-8") as f:
        f.write(token + "\n")
    os.replace(tmp, fn)


# The wire format

def _sign(token, body):
    return hmac.new(token.encode("ascii"), body.encode("utf-8"), hashlib.sha256).hexdigest()


def pack(token, sender, target, name, args, kwargs, now=None):
    # One message, as the line that goes on the wire
    msg = {"v": 1, "from": sender, "to": target, "name": name, "args": list(args), "kwargs": dict(kwargs),
           "time": time.time() if now is None else now, "nonce": secrets.token_hex(16)}
    try:
        body = json.dumps(msg, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
    except (TypeError, ValueError) as e:
        raise TypeError(f"Message arguments have to be things JSON can hold (str, int, float, bool, None, list, dict): {e}") from None
    return (json.dumps({"msg": body, "sig": _sign(token, body)}, separators=(",", ":")) + "\n").encode("utf-8")


class Checker:
    # The receiving side's checks, apart from the socket
    def __init__(self, token):
        self.token = token
        self.seen = {}

    def open(self, line, now=None):
        # The message in a line from the wire, or ValueError saying why not
        now = time.time() if now is None else now
        try:
            outer = json.loads(line)
            body = outer["msg"]
            sig = outer["sig"]
            if not isinstance(body, str) or not isinstance(sig, str):
                raise ValueError
        except (ValueError, KeyError, TypeError):
            raise ValueError("not a MicroKeys message") from None
        if not hmac.compare_digest(_sign(self.token, body), sig):
            raise ValueError("its signature is wrong, so the sender has a different pairing token")
        msg = json.loads(body)
        if msg.get("v") != 1 or not isinstance(msg.get("name"), str) or not isinstance(msg.get("args"), list) or \
                not isinstance(msg.get("kwargs"), dict) or not isinstance(msg.get("time"), (int, float)):
            raise ValueError("it isn't in a form this version understands")
        if abs(now - msg["time"]) > MAX_SKEW:
            raise ValueError("its clock is more than five minutes from this machine's")
        for nonce, when in list(self.seen.items()):
            if now - when > NONCE_KEEP:
                del self.seen[nonce]
        nonce = msg.get("nonce")
        if not isinstance(nonce, str) or nonce in self.seen:
            raise ValueError("it's a copy of a message already received")
        self.seen[nonce] = now
        return msg


# Receiving

_handlers = {}
_listener = None
_name = ""
_sender = None


def set_handlers(handlers):
    # Message name to binding id, from the runtime whenever bindings change
    _handlers.clear()
    _handlers.update(handlers)


def sender():
    return _sender


def name():
    return _name


def _log(text, level=LOG_INFO):
    native.log(level, text)


class Listener:
    # A thread taking messages on a TCP port and handing each one to the
    # script thread as a job.  An empty address means every network.
    def __init__(self, name, address, port, token):
        self.name = name
        self.address = address
        self.port = port
        self.token = token
        self.checker = Checker(token)
        self.server = None
        self.thread = None
        self.stopping = threading.Event()

    def start(self):
        if self.address:
            family = socket.AF_INET6 if ":" in self.address else socket.AF_INET
            self.server = socket.create_server((self.address, self.port), family=family)
        elif socket.has_dualstack_ipv6():
            self.server = socket.create_server(("", self.port), family=socket.AF_INET6, dualstack_ipv6=True)
        else:
            self.server = socket.create_server(("", self.port))
        self.server.settimeout(0.5)
        self.thread = threading.Thread(target=self.run, name="MicroKeys network", daemon=True)
        self.thread.start()

    def stop(self):
        self.stopping.set()
        if self.thread is not None:
            self.thread.join(5)
        if self.server is not None:
            self.server.close()

    def run(self):
        while not self.stopping.is_set():
            try:
                conn, addr = self.server.accept()
            except socket.timeout:
                continue
            except OSError:
                if not self.stopping.is_set():
                    time.sleep(0.5)
                continue
            try:
                with conn:
                    conn.settimeout(REPLY_TIMEOUT)
                    reply = self.handle(_read_line(conn), addr[0])
                    conn.sendall((json.dumps(reply) + "\n").encode("utf-8"))
            except Exception:
                pass

    def handle(self, line, address):
        if line is None:
            return {"ok": False, "error": "the message was cut off"}
        try:
            msg = self.checker.open(line)
        except ValueError as e:
            _log(f"Ignored a message from {address}: {e}", LOG_ERROR)
            return {"ok": False, "error": f"{self.name} turned the message down: {e}"}
        sender = str(msg.get("from") or address)
        name = msg["name"]
        id = _handlers.get(name)
        if id is None:
            _log(f"{sender} sent {name!r}, but nothing here handles it", LOG_ERROR)
            return {"ok": False, "error": f"Nothing on {self.name} handles the message {name!r}"}
        if native.paused():
            return {"ok": False, "error": f"MicroKeys is paused on {self.name}"}
        job = json.dumps({"name": name, "from": sender, "args": msg["args"], "kwargs": msg["kwargs"]})
        if not native.post(FIRE_MESSAGE, id, job):
            return {"ok": False, "error": f"MicroKeys on {self.name} is too busy to take it"}
        return {"ok": True}


def _read_line(conn):
    data = b""
    while b"\n" not in data:
        chunk = conn.recv(65536)
        if not chunk:
            return None
        data += chunk
        if len(data) > MAX_LINE:
            return None
    return data.split(b"\n", 1)[0].decode("utf-8", "replace")


def configure(settings, data_dir, headless):
    # Start, restart, or stop listening and advertising to match the
    # settings and the saved token.  Nothing listens without a token.
    global _listener, _name
    _name = str(settings.get("net_name") or "").strip() or socket.gethostname()
    try:
        port = int(settings.get("net_port") or DEFAULT_PORT)
    except (TypeError, ValueError):
        _log(f"The net_port setting should be a number, using {DEFAULT_PORT}", LOG_ERROR)
        port = DEFAULT_PORT
    address = str(settings.get("net_listen") or "").strip()
    token = None if headless else read_token(data_dir)
    if _listener is not None and token is not None and \
            (_listener.name, _listener.address, _listener.port, _listener.token) == (_name, address, port, token):
        return
    stop()
    if token is None:
        return
    listener = Listener(_name, address, port, token)
    try:
        listener.start()
    except OSError as e:
        _log(f"Unable to take messages from other machines on {address or 'port'} {port}: {e}", LOG_ERROR)
        return
    _listener = listener
    try:
        native.net_advertise(_name, port)
    except ValueError as e:
        _log(f"Other machines on this network won't find {_name} by looking: {e}")
    _log(f"Taking messages from other machines as {_name}, on {f'{address} port' if address else 'port'} {port}")


def stop():
    global _listener
    if _listener is not None:
        _listener.stop()
        _listener = None
        try:
            native.net_advertise("", 0)
        except ValueError:
            pass


def dispatch(item, extra):
    # Run a handler for a message that arrived; returns the function to run
    # and how to describe it
    job = json.loads(extra)

    def go():
        global _sender
        _sender = job["from"]
        try:
            item.func(*job["args"], **job["kwargs"])
        finally:
            _sender = None
    return go, f"message {job['name']!r} from {job['from']}"


# Sending

_addresses = {}


def _split_address(machine, port):
    # "host", "host:1234", or "[::1]:1234" to (host, port, explicit)
    if machine.startswith("[") and "]" in machine:
        host, _, rest = machine[1:].partition("]")
        if rest.startswith(":") and rest[1:].isdigit():
            return host, int(rest[1:]), True
        return host, port, False
    if machine.count(":") == 1:
        host, _, rest = machine.partition(":")
        if rest.isdigit():
            return host, int(rest), True
    return machine, port, False


def _connect(machine, port):
    # A connection to the machine: by its address or DNS name (Tailscale's
    # MagicDNS answers for tailnet machines), else by asking the local
    # network over Bonjour
    host, port, explicit = _split_address(machine, port)
    tries = []
    if machine in _addresses:
        tries.append(_addresses[machine])
    tries.append((host, port))
    errors = []
    for address in tries:
        try:
            return socket.create_connection(address, timeout=CONNECT_TIMEOUT)
        except OSError as e:
            errors.append(e)
    if not explicit:
        found = native.net_resolve(host, RESOLVE_MS)
        if found is not None:
            try:
                conn = socket.create_connection(tuple(found), timeout=CONNECT_TIMEOUT)
                _addresses[machine] = tuple(found)
                return conn
            except OSError as e:
                errors.append(e)
    _addresses.pop(machine, None)
    raise NetError(f"Unable to reach {machine}: {errors[-1]}")


def send(token, sender, machine, port, name, args, kwargs):
    line = pack(token, sender, machine, name, args, kwargs)
    with _connect(machine, port) as conn:
        conn.settimeout(REPLY_TIMEOUT)
        try:
            conn.sendall(line)
            reply = _read_line(conn)
        except OSError as e:
            raise NetError(f"Lost the connection to {machine}: {e}") from None
    try:
        reply = json.loads(reply)
    except (TypeError, ValueError):
        raise NetError(f"{machine} didn't answer like MicroKeys") from None
    if not reply.get("ok"):
        raise NetError(str(reply.get("error") or f"{machine} turned the message down"))
