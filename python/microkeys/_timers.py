# every(), after() and at(): run functions on a schedule.  A thread of our
# own keeps time and hands each timer to the script thread when it's due,
# so timers take their turn with hotkeys and everything else.

import datetime
import threading
import time

from ._native import native, FIRE_TIMER
from . import _bindings

# A daily time missed by more than this (the computer was asleep, say) waits
# for the next day
LATE = 60.0
# How soon to try again with a one-time timer that couldn't be handed over
RETRY = 1.0


def _timer(what, kind, seconds, when, func):
    collector = _bindings._collector(what)
    source = _bindings._source()
    created = time.monotonic()

    def register(f):
        _bindings.check_function(f, what)
        collector.add(collector.timers, _bindings.TimerEntry(kind, seconds, when, f, source, created))
        return f

    if func is not None:
        return register(func)
    return register


def every(seconds=0, func=None, *, minutes=0, hours=0):
    """Run a function over and over, at a fixed interval.

        @every(minutes=5)
        def check_inbox():
            ...

    The first run comes one interval after the script loads (or after the
    macro that called every() finishes).  If a run is still waiting its
    turn when the next one is due, they count as one.
    """
    if callable(seconds):
        raise TypeError("every() needs an interval, like @every(minutes=5)")
    interval = float(seconds) + float(minutes) * 60 + float(hours) * 3600
    if interval < 0.1:
        raise ValueError("every() needs an interval of at least 0.1 seconds")
    return _timer("every", "every", interval, None, func)


def after(seconds, func=None):
    """Run a function once, `seconds` from now.

        after(2.5, lambda: keys.type("later"))

    From a script's top level, "now" is when the script loads.
    """
    if callable(seconds):
        raise TypeError("after() needs a delay, like after(5, func)")
    delay = float(seconds)
    if delay < 0:
        raise ValueError("after() needs a delay of 0 or more seconds")
    return _timer("after", "after", delay, None, func)


def parse_time(when):
    # "9:30", "21:05:10", a datetime.time, or a datetime.datetime
    if isinstance(when, (datetime.datetime, datetime.time)):
        return when
    if isinstance(when, str):
        parts = when.strip().split(":")
        if 2 <= len(parts) <= 3 and all(p.strip().isdigit() for p in parts):
            try:
                return datetime.time(*[int(p) for p in parts])
            except ValueError:
                pass
    raise ValueError(f"at() takes a time like '9:30' or '21:05:10', a datetime.time, or a datetime.datetime, not {when!r}")


def at(when, func=None):
    """Run a function at a time of day, every day, or once at a date and time.

        @at("9:00")
        def good_morning():
            ...

        at(datetime.datetime(2026, 12, 24, 17, 0), leave_early)

    `when` is "HH:MM" or "HH:MM:SS" in 24-hour local time, a datetime.time,
    or a datetime.datetime for one time only.  A daily time that passes
    while the computer sleeps is skipped until the next day; a one-time
    date and time that's already past runs right away.
    """
    return _timer("at", "at", 0.0, parse_time(when), func)


def next_daily(when, now):
    # The first time after `now` (seconds since the epoch) that the clock
    # reads `when`
    day = datetime.date.fromtimestamp(now)
    while True:
        due = datetime.datetime.combine(day, when).timestamp()
        if due > now:
            return due
        day += datetime.timedelta(days=1)


class Scheduler:
    # Hands timers to the script thread as they come due.  Each timer is
    # handed over at most once until it has run.
    def __init__(self):
        self.cond = threading.Condition()
        self.timers = {}
        self.waiting = set()
        self.thread = None
        self.stopping = False

    def _first_due(self, entry):
        # Monotonic seconds for every() and after(), wall clock for at()
        if entry.kind == "every":
            return time.monotonic() + entry.seconds
        if entry.kind == "after":
            return entry.created + entry.seconds
        if isinstance(entry.when, datetime.datetime):
            return entry.when.timestamp()
        return next_daily(entry.when, time.time())

    def set(self, entries):
        # The timers now in effect; ones already scheduled keep their place
        with self.cond:
            timers = {}
            for entry in entries:
                if entry.spent:
                    continue
                old = self.timers.get(entry.id)
                timers[entry.id] = old if old is not None and old[0] is entry else [entry, self._first_due(entry)]
            self.timers = timers
            self.waiting &= set(timers)
            if timers and self.thread is None and not self.stopping:
                self.thread = threading.Thread(target=self._loop, name="MicroKeys timers", daemon=True)
                self.thread.start()
            self.cond.notify()

    def ran(self, id):
        with self.cond:
            self.waiting.discard(id)

    def stop(self):
        with self.cond:
            self.stopping = True
            self.cond.notify()
        if self.thread is not None:
            self.thread.join(2)

    def due(self, now_mono, now_wall):
        # Hand over whatever is due and work out when each runs next.
        # Returns how long until the next one, at most 30 seconds so a
        # change to the clock is noticed.
        wait = 30.0
        for id, item in list(self.timers.items()):
            entry, due = item
            now = now_wall if entry.kind == "at" else now_mono
            if due > now:
                wait = min(wait, due - now)
                continue
            if entry.once():
                # Held while paused, or while too much is waiting to run
                if not native.paused() and native.post(FIRE_TIMER, id, ""):
                    entry.spent = True
                    del self.timers[id]
                else:
                    item[1] = now + RETRY
                    wait = min(wait, RETRY)
                continue
            # A repeating timer just misses the runs it can't have
            late = entry.kind == "at" and now - due > LATE
            if not late and id not in self.waiting and not native.paused() and native.post(FIRE_TIMER, id, ""):
                self.waiting.add(id)
            if entry.kind == "every":
                item[1] = due + entry.seconds if due + entry.seconds > now else now + entry.seconds
            else:
                item[1] = next_daily(entry.when, now + 1)
            wait = min(wait, item[1] - now)
        return max(wait, 0.0)

    def _loop(self):
        with self.cond:
            while not self.stopping:
                self.cond.wait(self.due(time.monotonic(), time.time()))
