import time
from microkeys import hotkey, hotstring, clip, keys

@hotkey("ctrl+alt+d")
def type_date():
    keys.type(time.strftime("%Y-%m-%d"))

@hotkey("ctrl+alt+e")
def evaluate():
    text = clip.copy_selection()
    keys.type(f"{text} = {eval(text)}")

hotstring(";sig", "Best regards,\nSam")
