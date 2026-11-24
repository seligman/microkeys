# Export and import: every script, the settings, and the store in one zip,
# for moving to another machine or keeping a copy.  Importing shows what
# would change, then merges (files and settings from the zip win, the rest
# stays) or replaces (everything matches the zip).  Either way the old
# scripts and settings are exported to a backup first.

import datetime
import json
import os
import zipfile

from ._native import native
from . import _settings, _store

MANIFEST = "microkeys-export.json"
FORMAT = 1
# Settings that belong to one machine and never travel
LOCAL_SETTINGS = {"script_folder"}
# Files and folders left out of an export
SKIP_NAMES = {"__pycache__", "pyrightconfig.json"}
MAX_TOTAL = 256 * 1024 * 1024


def _script_files(script_dir):
    # Relative path (with "/") to full path, for everything in the script
    # folder worth carrying over
    ret = {}
    for dirpath, dirnames, filenames in os.walk(script_dir):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_NAMES and not d.startswith("."))
        for name in sorted(filenames):
            if name in SKIP_NAMES or name.startswith("."):
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, script_dir).replace(os.sep, "/")
            ret[rel] = full
    return ret


def _user_settings(data_dir):
    fn = os.path.join(data_dir, "settings.json")
    if not os.path.isfile(fn):
        return {}
    with open(fn, "rt", encoding="utf-8-sig") as f:
        data = json.load(f)
    return data if isinstance(data, dict) else {}


def export(path, script_dir, data_dir):
    # Write the zip; returns how many script files went in
    files = _script_files(script_dir)
    settings = {k: v for k, v in _user_settings(data_dir).items() if k not in LOCAL_SETTINGS}
    _store.save()
    manifest = {"format": FORMAT, "version": native.version(),
                "created": datetime.datetime.now().astimezone().isoformat(timespec="seconds")}
    tmp = path + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(MANIFEST, json.dumps(manifest, indent=4) + "\n")
        zf.writestr("settings.json", json.dumps(settings, indent=4) + "\n")
        zf.writestr("store.json", json.dumps(dict(_store.store), indent=4, sort_keys=True, ensure_ascii=False) + "\n")
        for rel, full in files.items():
            zf.write(full, "scripts/" + rel)
    os.replace(tmp, path)
    return len(files)


class Plan:
    # What an import would do, worked out before anything changes
    def __init__(self):
        self.files = {}
        self.settings = {}
        self.store = {}
        self.added = []
        self.changed = []
        self.removed = []
        self.settings_changed = []
        self.settings_removed = []

    def merge_summary(self):
        parts = []
        if self.added:
            parts.append(f"{len(self.added)} new file{_s(self.added)}")
        if self.changed:
            parts.append(f"{len(self.changed)} changed file{_s(self.changed)}")
        if self.settings_changed:
            parts.append(f"{len(self.settings_changed)} setting{_s(self.settings_changed)}")
        return ", ".join(parts) if parts else "nothing new"


def _s(items):
    return "" if len(items) == 1 else "s"


def _safe(rel):
    # A path inside the zip that stays inside the script folder
    if not rel or rel.startswith("/") or "\\" in rel or ":" in rel:
        return False
    parts = rel.split("/")
    return all(p not in ("", ".", "..") for p in parts)


def read(path, script_dir, data_dir):
    # Check the zip and work out what importing it would change.  Raises
    # ValueError if it isn't a MicroKeys export.
    plan = Plan()
    try:
        zf = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as e:
        raise ValueError(f"{os.path.basename(path)} isn't a zip file: {e}") from None
    with zf:
        names = zf.namelist()
        if MANIFEST not in names:
            raise ValueError(f"{os.path.basename(path)} isn't a MicroKeys export")
        try:
            manifest = json.loads(zf.read(MANIFEST))
        except ValueError:
            raise ValueError(f"{os.path.basename(path)} has a damaged {MANIFEST}") from None
        if not isinstance(manifest, dict) or manifest.get("format") != FORMAT:
            raise ValueError(f"{os.path.basename(path)} is from a newer MicroKeys; update this one first")
        if sum(info.file_size for info in zf.infolist()) > MAX_TOTAL:
            raise ValueError(f"{os.path.basename(path)} is too big to import")
        for info in zf.infolist():
            if info.is_dir() or not info.filename.startswith("scripts/"):
                continue
            rel = info.filename[len("scripts/"):]
            if not _safe(rel):
                raise ValueError(f"{os.path.basename(path)} has a file that would land outside the script folder: {info.filename}")
            plan.files[rel] = zf.read(info)
        for name, attr in (("settings.json", "settings"), ("store.json", "store")):
            if name in names:
                try:
                    data = json.loads(zf.read(name))
                except ValueError:
                    raise ValueError(f"{os.path.basename(path)} has a damaged {name}") from None
                if isinstance(data, dict):
                    setattr(plan, attr, data)
    for key in LOCAL_SETTINGS:
        plan.settings.pop(key, None)

    have = _script_files(script_dir)
    for rel, data in sorted(plan.files.items()):
        if rel not in have:
            plan.added.append(rel)
        else:
            with open(have[rel], "rb") as f:
                if f.read() != data:
                    plan.changed.append(rel)
    plan.removed = sorted(rel for rel in have if rel not in plan.files)
    current = _user_settings(data_dir)
    plan.settings_changed = sorted(k for k, v in plan.settings.items() if current.get(k) != v)
    plan.settings_removed = sorted(k for k in current if k not in plan.settings and k not in LOCAL_SETTINGS)
    return plan


def describe(plan, limit=8):
    # The preview shown before importing
    lines = []
    for title, items in (("New files", plan.added), ("Changed files", plan.changed),
                         ("Files only here (Replace removes them)", plan.removed),
                         ("Settings that differ", plan.settings_changed),
                         ("Settings only here (Replace resets them)", plan.settings_removed)):
        if items:
            shown = ", ".join(items[:limit]) + (f", and {len(items) - limit} more" if len(items) > limit else "")
            lines.append(f"{title}: {shown}")
    if not lines:
        lines.append("The scripts and settings here already match the zip.")
    return "\n".join(lines)


def _merged_settings(current, incoming):
    # Settings from the zip win, but lists and tables are combined
    ret = dict(current)
    for key, value in incoming.items():
        have = ret.get(key)
        if isinstance(have, list) and isinstance(value, list):
            ret[key] = have + [x for x in value if x not in have]
        elif isinstance(have, dict) and isinstance(value, dict):
            ret[key] = dict(have, **value)
        else:
            ret[key] = value
    return ret


def apply(plan, replace, script_dir, data_dir):
    # Make the changes.  The script folder's own files are written in place,
    # so the watcher reloads once it's done.
    os.makedirs(script_dir, exist_ok=True)
    if replace:
        have = _script_files(script_dir)
        for rel in plan.removed:
            os.unlink(have[rel])
    for rel, data in plan.files.items():
        full = os.path.join(script_dir, *rel.split("/"))
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as f:
            f.write(data)

    current = _user_settings(data_dir)
    if replace:
        settings = dict(plan.settings)
        for key in LOCAL_SETTINGS:
            if key in current:
                settings[key] = current[key]
    else:
        settings = _merged_settings(current, plan.settings)
    fn = os.path.join(data_dir, "settings.json")
    tmp = fn + ".tmp"
    with open(tmp, "wt", encoding="utf-8") as f:
        json.dump(settings, f, indent=4)
        f.write("\n")
    os.replace(tmp, fn)

    if replace:
        _store.store.clear()
    _store.store.update(plan.store)
    _store.save()


def backup_path(data_dir):
    folder = os.path.join(data_dir, "backups")
    os.makedirs(folder, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    return os.path.join(folder, f"before-import-{stamp}.zip")


def default_name():
    return f"MicroKeys-{datetime.date.today().isoformat()}.zip"

