"""Messages between machines.

One machine's macro can run a handler on another:

    # On the laptop
    @hotkey("ctrl+alt+l")
    def lock_desktop():
        net.send("desktop", "lock")

    # On the desktop
    @net.on("lock")
    def lock():
        run("rundll32.exe user32.dll,LockWorkStation")

* `send` - send a message to another machine
* `on` - run a function when a message arrives
* `sender` - which machine sent the message being handled
* `name` - this machine's name, as other machines know it
* `machines` - MicroKeys machines found on the local network

Machines have to be paired first.  On one, choose **Networking >
Generate a pairing token** from the MicroKeys menu; on the others, choose
**Networking > Enter a pairing token** and paste it in.  Only machines with
the same token talk to each other, and nothing listens until a token is set.

Machines find each other by name: Tailscale's MagicDNS, ordinary DNS, or
Bonjour on the local network.  There's no relay, so they have to share a
network or a VPN.  A message is signed with the token, so it can't be
forged or changed on the way, but it isn't secret; a VPN such as Tailscale
encrypts it.
"""

from ._native import native, OFFLINE
from . import _bindings, _net, _settings
from ._net import NetError

__all__ = ["send", "on", "sender", "name", "machines", "NetError"]

# Set by the runtime: the folder the token is kept in
_data_dir = None


def send(machine, message, *args, **kwargs):
    """Send a message to another machine, which runs its handler for it.

        net.send("desktop", "open_url", "https://example.com")
        net.send("laptop.tailnet-name.ts.net", "notify", text="Build done")

    `machine` is the other machine's name (its `net_name` setting, by default
    its computer name), a DNS name, or an address, optionally with
    ":port".  The arguments go to the handler, so they have to be things
    JSON can hold: strings, numbers, True, False, None, and lists and dicts
    of them.

    It returns once the other machine has taken the message, before the
    handler runs there.  Raises `NetError` if the machine can't be reached,
    isn't paired with this one, or has nothing that handles `message`.
    """
    if not isinstance(message, str) or not message:
        raise ValueError("net.send() needs a message name")
    if OFFLINE:
        native.say(f"send {message!r} to {machine} (only in MicroKeys)")
        return
    token = _net.read_token(_data_dir) if _data_dir else None
    if token is None:
        raise NetError("This machine isn't paired; choose Networking > Generate a pairing token or Enter a pairing token from the MicroKeys menu")
    port = int(_settings.current.get("net_port") or _net.DEFAULT_PORT)
    _net.send(token, _net.name(), str(machine), port, message, args, kwargs)


def on(message, func=None):
    """Run a function when another machine sends this message.

        @net.on("notify")
        def notify(text):
            ui.toast(text)

    The function is given the arguments the sender passed to `send()`.
    Handlers run one at a time, like hotkeys, and not while MicroKeys is
    paused.  A message nothing handles is logged and turned down.
    """
    return _bindings.message(message, func)


def sender():
    """The name of the machine whose message is being handled, or None
    outside a handler."""
    return _net.sender()


def name():
    """This machine's name for messages: the `net_name` setting, or the
    computer's name if that's empty."""
    return _net.name() or str(_settings.current.get("net_name") or "") or _computer_name()


def _computer_name():
    import socket
    return socket.gethostname()


def machines(timeout=2.0):
    """The names of MicroKeys machines on the local network that are taking
    messages, found over Bonjour within `timeout` seconds.  Machines that
    are only reachable over a VPN don't answer this way, but `send()` still
    reaches them by name."""
    return sorted(set(native.net_browse(int(float(timeout) * 1000))), key=str.lower)
