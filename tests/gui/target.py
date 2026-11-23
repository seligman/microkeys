#!/usr/bin/env python3

# A window to type into for the GUI tests.  Whatever is in its text box is
# written to a file as it changes, so the tests can check what arrived.
#
# Usage: target.py <title> <output file>

import sys
import tkinter as tk


def main():
    title, out = sys.argv[1], sys.argv[2]
    root = tk.Tk()
    root.title(title)
    root.geometry("500x300+100+100")
    text = tk.Text(root, font=("Segoe UI", 12))
    text.pack(fill="both", expand=True)
    text.focus_set()
    last = [None]

    def save():
        value = text.get("1.0", "end-1c")
        if value != last[0]:
            with open(out, "w", encoding="utf-8", newline="") as f:
                f.write(value)
            last[0] = value
        root.after(50, save)

    save()
    root.mainloop()


if __name__ == "__main__":
    main()
