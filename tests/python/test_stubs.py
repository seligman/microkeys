# The package outside MicroKeys: the offline stand-in for _microkeys, the
# build backend behind "pip install -e", and checks that every copy of the
# native interface agrees with _microkeys.pyi.

import ast
import inspect
import os
import re
import subprocess
import sys
import textwrap
import zipfile

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PYTHON_DIR = os.path.join(ROOT, "python")

sys.path.insert(0, PYTHON_DIR)
import _build_backend
from fake_microkeys import FakeMicrokeys
from microkeys import _offline


def stub_signatures():
    with open(os.path.join(PYTHON_DIR, "_microkeys.pyi"), encoding="utf-8") as f:
        tree = ast.parse(f.read())
    return {node.name: [a.arg for a in node.args.args] for node in tree.body if isinstance(node, ast.FunctionDef)}


def version_h():
    with open(os.path.join(ROOT, "src", "version.h")) as f:
        return re.search(r'VERSION_TEXT\s+"([^"]+)"', f.read()).group(1)


def test_offline_matches_stub():
    for name, args in stub_signatures().items():
        func = getattr(_offline, name, None)
        assert func is not None, f"_offline has no {name}()"
        assert list(inspect.signature(func).parameters) == args, f"_offline.{name}() takes the wrong arguments"


def test_fake_matches_stub():
    fake = FakeMicrokeys()
    for name, args in stub_signatures().items():
        func = getattr(fake, name, None)
        assert func is not None, f"the test fake has no {name}()"
        assert len(inspect.signature(func).parameters) == len(args), f"the test fake's {name}() takes the wrong arguments"


def test_native_module_matches_stub():
    with open(os.path.join(ROOT, "src", "core", "PyModule.cpp"), encoding="utf-8") as f:
        names = re.findall(r'\{ "(\w+)", mk_\w+, METH_', f.read())
    assert sorted(names) == sorted(stub_signatures())


def run_offline(code):
    env = dict(os.environ, PYTHONPATH=PYTHON_DIR)
    return subprocess.run([sys.executable, "-c", textwrap.dedent(code)], capture_output=True, text=True, env=env, timeout=60)


def test_offline_package():
    proc = run_offline("""
        import time
        from microkeys import hotkey, hotstring, every, keys, clip, windows, ui, log, store, unbind

        @hotkey("ctrl+alt+d")
        def type_date():
            keys.type("2026-01-02")
            keys.press("ctrl+s")

        hotstring(";sig", "Best regards")
        every(60, type_date)
        type_date()
        clip.set("copied")
        print(clip.get(), windows.active(), ui.msgbox("Hi", buttons="yesno"), unbind(type_date))
        log("logged")
        store["x"] = 1
        ui.show("Done")
    """)
    assert proc.returncode == 0, proc.stderr
    assert proc.stdout.splitlines() == ["copied None yes 0", "logged"]
    for want in ["[microkeys] bind hotkey Ctrl+Alt+D to type_date (only in MicroKeys)",
                 "[microkeys] bind hotstring ';sig' to 'Best regards'",
                 "[microkeys] type '2026-01-02'\n[microkeys] press ctrl+s",
                 "[microkeys] put 'copied' on the clipboard",
                 "[microkeys] message box 'MicroKeys': 'Hi'",
                 "[microkeys] show 'Done' on screen"]:
        assert want in proc.stderr


def test_wheels(tmp_path):
    name = _build_backend.build_wheel(str(tmp_path))
    assert name == f"microkeys-{version_h()}-py3-none-any.whl"
    with zipfile.ZipFile(tmp_path / name) as zf:
        names = zf.namelist()
        assert "microkeys/__init__.py" in names and "microkeys/py.typed" in names and "_microkeys.pyi" in names
        record = zf.read(f"microkeys-{version_h()}.dist-info/RECORD").decode()
        assert all(n in record for n in names)
    editable = tmp_path / "editable"
    editable.mkdir()
    name = _build_backend.build_editable(str(editable))
    with zipfile.ZipFile(editable / name) as zf:
        assert zf.read("microkeys.pth").decode().strip() == PYTHON_DIR
    assert _build_backend.build_sdist(str(tmp_path)).endswith(".tar.gz")


def test_pip_install_editable(tmp_path):
    # The whole thing, as a user would do it, in a fresh virtual environment
    venv = tmp_path / "venv"
    if subprocess.run([sys.executable, "-m", "venv", str(venv)], capture_output=True).returncode != 0:
        pytest.skip("no venv module")
    python = str(venv / ("Scripts" if os.name == "nt" else "bin") / "python")
    if subprocess.run([python, "-m", "pip", "--version"], capture_output=True).returncode != 0:
        pytest.skip("no pip in the venv")
    proc = subprocess.run([python, "-m", "pip", "install", "--no-index", "-e", PYTHON_DIR], capture_output=True, text=True, timeout=300)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    proc = subprocess.run([python, "-c", "import microkeys; print(microkeys.version(), microkeys.__file__)"],
                          capture_output=True, text=True, cwd=str(tmp_path))
    version, path = proc.stdout.split()
    assert version == version_h()
    assert path.startswith(PYTHON_DIR)


def test_docs_without_updater(tmp_path):
    # The docs a build without the updater ships never mention updates
    script = os.path.join(ROOT, "tools", "gen_docs.py")
    for flags, out in (([], "full"), (["--no-updates"], "plain")):
        subprocess.check_call([sys.executable, "-I", script, "--out", str(tmp_path / out)] + flags, stdout=subprocess.DEVNULL)
    full = (tmp_path / "full" / "index.html").read_text(encoding="utf-8")
    assert "check_updates" in full and "<!--" not in full
    for page in (tmp_path / "plain").iterdir():
        assert not re.search("updat", page.read_text(encoding="utf-8"), re.I), page.name
