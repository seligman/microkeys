#!/usr/bin/env python3

# Build the HTML documentation: the guide and cookbook from docs/*.md, plus
# an API reference read from the microkeys package's docstrings.
#
# Usage: gen_docs.py --out <folder>

import argparse
import html
import inspect
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "python"))

import microkeys
from microkeys import keys, clip, mouse, windows, ui

PAGES = [
    ("index.html", "Guide", os.path.join(ROOT, "docs", "guide.md")),
    ("cookbook.html", "Cookbook", os.path.join(ROOT, "docs", "cookbook.md")),
    ("api.html", "API reference", None),
]

CSS = """
:root { --bg: #ffffff; --fg: #1f2328; --muted: #59636e; --code-bg: #f6f8fa; --border: #d1d9e0; --link: #0969da; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #0d1117; --fg: #e6edf3; --muted: #9198a1; --code-bg: #161b22; --border: #3d444d; --link: #4493f8; }
}
body { background: var(--bg); color: var(--fg); font: 16px/1.55 "Segoe UI", system-ui, sans-serif; margin: 0; }
main { max-width: 860px; margin: 0 auto; padding: 16px 24px 64px; }
nav { border-bottom: 1px solid var(--border); padding: 12px 24px; display: flex; gap: 20px; }
nav a { color: var(--link); text-decoration: none; font-weight: 600; }
nav a.here { color: var(--fg); }
a { color: var(--link); }
code, pre { font-family: Consolas, "Cascadia Mono", monospace; font-size: 14px; background: var(--code-bg); }
code { padding: 1px 4px; border-radius: 4px; }
pre { padding: 12px 14px; border-radius: 6px; overflow-x: auto; border: 1px solid var(--border); }
pre code { padding: 0; background: none; }
h1, h2, h3 { line-height: 1.25; }
h2 { border-bottom: 1px solid var(--border); padding-bottom: 4px; margin-top: 32px; }
.sig { font-family: Consolas, "Cascadia Mono", monospace; font-weight: 600; margin-top: 24px; }
.doc { margin-left: 20px; }
.muted { color: var(--muted); }
"""


def inline(text):
    # Inline Markdown: `code`, **bold**, [links](url)
    out = []
    for i, part in enumerate(re.split(r"(`[^`]*`)", text)):
        if i % 2 == 1:
            out.append(f"<code>{html.escape(part[1:-1])}</code>")
            continue
        part = html.escape(part)
        part = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", part)
        part = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', part)
        out.append(part)
    return "".join(out)


def markdown(text):
    # The small part of Markdown the docs use: headings, paragraphs, bullet
    # lists, and fenced code blocks
    out = []
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("```"):
            code = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            out.append(f"<pre><code>{html.escape(chr(10).join(code))}</code></pre>")
            i += 1
            continue
        m = re.match(r"^(#{1,3}) (.*)", line)
        if m:
            level = len(m.group(1))
            anchor = re.sub(r"[^a-z0-9]+", "-", m.group(2).lower()).strip("-")
            out.append(f'<h{level} id="{anchor}">{inline(m.group(2))}</h{level}>')
            i += 1
            continue
        if re.match(r"^[-*] ", line):
            items = []
            while i < len(lines) and (re.match(r"^[-*] ", lines[i]) or (lines[i].startswith("  ") and items)):
                if re.match(r"^[-*] ", lines[i]):
                    items.append(lines[i][2:])
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            out.append("<ul>" + "".join(f"<li>{inline(x)}</li>" for x in items) + "</ul>")
            continue
        if not line.strip():
            i += 1
            continue
        para = []
        while i < len(lines) and lines[i].strip() and not lines[i].startswith("```") and not re.match(r"^(#{1,3} |[-*] )", lines[i]):
            para.append(lines[i].strip())
            i += 1
        out.append(f"<p>{inline(' '.join(para))}</p>")
    return "\n".join(out)


def docstring(obj):
    # A docstring as HTML: indented blocks become code, the rest Markdown
    doc = inspect.getdoc(obj) or ""
    out, block = [], []
    for line in doc.split("\n"):
        if line.startswith("    ") or (block and not line.strip()):
            block.append(line[4:])
            continue
        if block:
            out.append("```\n" + "\n".join(block).rstrip() + "\n```")
            block = []
        out.append(line)
    if block:
        out.append("```\n" + "\n".join(block).rstrip() + "\n```")
    return markdown("\n".join(out))


def signature(name, func):
    try:
        return f"{name}{inspect.signature(func)}"
    except (TypeError, ValueError):
        return f"{name}(...)"


def module_section(title, module, names):
    out = [f'<h2 id="{title}">{html.escape(title)}</h2>']
    if module is not microkeys:
        out.append(docstring(module))
    for name in names:
        obj = getattr(module, name)
        if inspect.isclass(obj):
            out.append(f'<div class="sig" id="{title}.{name}">class {html.escape(name)}</div><div class="doc">{docstring(obj)}')
            for attr, value in obj.__dict__.items():
                if attr.startswith("_"):
                    continue
                if isinstance(value, property):
                    out.append(f'<div class="sig">.{html.escape(attr)}</div><div class="doc">{docstring(value)}</div>')
                elif inspect.isfunction(value):
                    sig = signature(attr, value).replace("(self, ", "(").replace("(self)", "()")
                    out.append(f'<div class="sig">.{html.escape(sig)}</div><div class="doc">{docstring(value)}</div>')
            out.append("</div>")
        elif callable(obj):
            prefix = "" if module is microkeys else f"{title}."
            out.append(f'<div class="sig" id="{title}.{name}">{html.escape(prefix + signature(name, obj))}</div><div class="doc">{docstring(obj)}</div>')
    return "\n".join(out)


def api_page():
    parts = [
        "<h1>API reference</h1>",
        f'<p class="muted">MicroKeys {html.escape(read_version())}.  Everything is in the <code>microkeys</code> package: '
        "<code>from microkeys import hotkey, hotstring, keys, clip</code> and so on.</p>",
        module_section("microkeys", microkeys, ["hotkey", "hotstring", "command", "log", "sleep", "run", "open", "paste_text", "version"]),
        module_section("keys", keys, ["press", "type", "down", "up", "hold"]),
        "<h3>Key names</h3><p>Besides single characters such as <code>a</code>, <code>7</code> or <code>/</code>:</p>",
        "<p>" + ", ".join(f"<code>{k}</code>" for k in keys.NAMED_KEYS) + "</p>",
        "<p>Also accepted: <code>control</code>, <code>option</code>, <code>win</code>, <code>cmd</code>, <code>return</code>, "
        "<code>esc</code>, <code>del</code>, <code>ins</code>, <code>pgup</code>, <code>pgdn</code>, and <code>primary</code> "
        "(Ctrl on Windows, Cmd on macOS).</p>",
        module_section("clip", clip, ["get", "set", "copy_selection", "paste_text", "wait_for_change", "sequence"]),
        module_section("mouse", mouse, ["position", "move", "move_by", "click", "down", "up", "scroll", "drag"]),
        module_section("windows", windows, ["active", "all", "find", "find_one", "wait_for", "monitors", "app", "match", "Window", "Monitor"]),
        module_section("ui", ui, ["msgbox", "input", "choose", "toast"]),
    ]
    return "\n".join(parts)


def read_version():
    with open(os.path.join(ROOT, "src", "version.h")) as f:
        return re.search(r'VERSION_TEXT\s+"([^"]+)"', f.read()).group(1)


def page(title, body, current):
    links = " ".join(f'<a href="{fn}"{" class=\"here\"" if fn == current else ""}>{html.escape(t)}</a>' for fn, t, _ in PAGES)
    return (f"<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
            f"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            f"<title>MicroKeys {html.escape(title)}</title><style>{CSS}</style></head>"
            f"<body><nav>{links}</nav><main>{body}</main></body></html>\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for fn, title, source in PAGES:
        if source is None:
            body = api_page()
        else:
            with open(source, encoding="utf-8") as f:
                body = markdown(f.read())
        with open(os.path.join(args.out, fn), "w", encoding="utf-8") as f:
            f.write(page(title, body, fn))
    print(f"Wrote {len(PAGES)} pages to {args.out}")


if __name__ == "__main__":
    main()
