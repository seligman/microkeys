using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace Script
{
    class KeyboardForm : Form
    {
        Timer _timer;
        DateTime _started;
        List<Script> _script;

        List<Script> GetScript(int i)
        {
            if (i == 0)
            {
                return Script.FollowScript(50,
                    "+win",
                    "'r",
                    "-win",
                    "'https",
                    "+shift",
                    "';",
                    "-shift",
                    "'//www.google.com",
                    ">enter"
                );
            }

            return null;
        }

        public KeyboardForm()
        {
            /*
            for (int i = 1; i < 256; i++)
            {
                WinAPI.RegisterHotKey(Handle, i, 0, (uint)i);
            }
            */
            Paint += KeyboardForm_Paint;
            Resize += KeyboardForm_Resize;
            _timer = new Timer
            {
                Enabled = false,
                Interval = 10,
            };
            _timer.Tick += Timer_Tick;
            Width = 1000;
            DoubleBuffered = true;

            for (int i = 0; ; i++)
            {
                var temp = GetScript(i);
                if (temp == null)
                {
                    break;
                }
                WinAPI.RegisterHotKey(Handle, i + 1, 0, (uint)WinAPI.KeyCode.F1 + (uint)i);
            }

            /*
            AddKey("esc", vk: WinAPI.KeyCode.Escape);
            AddKey("f1", vk: WinAPI.KeyCode.F1);
            AddKey("f2", vk: WinAPI.KeyCode.F2);
            AddKey("f3", vk: WinAPI.KeyCode.F3);
            AddKey("f4", vk: WinAPI.KeyCode.F4);
            AddKey("f5", vk: WinAPI.KeyCode.F5);
            AddKey("f6", vk: WinAPI.KeyCode.F6);
            AddKey("f7", vk: WinAPI.KeyCode.F7);
            AddKey("f8", vk: WinAPI.KeyCode.F8);
            AddKey("f9", vk: WinAPI.KeyCode.F9);
            AddKey("f10", vk: WinAPI.KeyCode.F10);
            AddKey("f11", vk: WinAPI.KeyCode.F11);
            AddKey("f12", vk: WinAPI.KeyCode.F12);
            AddKey("del", width: -1, vk: WinAPI.KeyCode.Delete);
            AddRow();
            */

            AddKey("`", vk: WinAPI.KeyCode.Oem3);
            AddKeys("1", "2", "3", "4", "5", "6", "7", "8", "9", "0");
            AddKey("-", vk: WinAPI.KeyCode.OemMinus);
            AddKey("=", vk: WinAPI.KeyCode.Equal);
            AddKey("<-", width: 1.33f, vk: WinAPI.KeyCode.Backspace);
            AddRow();

            AddKey("tab", width: -1, vk: WinAPI.KeyCode.Tab);
            AddKeys("q", "w", "e", "r", "t", "y", "u", "i", "o", "p");
            AddKey("[", vk: WinAPI.KeyCode.Oem4);
            AddKey("]", vk: WinAPI.KeyCode.Oem6);
            AddKey("\\", vk: WinAPI.KeyCode.Oem5);
            AddRow();

            AddKey("caps", width: 1.66f, vk: WinAPI.KeyCode.Capital);
            AddKeys("a", "s", "d", "f", "g", "h", "j", "k", "l");
            AddKey(";", vk: WinAPI.KeyCode.Oem1);
            AddKey("'", vk: WinAPI.KeyCode.Oem7);
            AddKey("enter", width: -1, vk: WinAPI.KeyCode.Return);
            AddRow();

            AddKey("shift", 2f, vk: WinAPI.KeyCode.Shift);
            AddKeys("z", "x", "c", "v", "b", "n", "m");
            AddKey(",", vk: WinAPI.KeyCode.OemComma);
            AddKey(".", vk: WinAPI.KeyCode.OemPeriod);
            AddKey("/", vk: WinAPI.KeyCode.Oem2);
            AddKey("shift", width: -1, vk: WinAPI.KeyCode.Shift);
            AddRow();

            AddKey("ctrl", width: 1.2f, vk: WinAPI.KeyCode.LControl);
            AddKey("win", vk: WinAPI.KeyCode.LWin);
            AddKey("alt", width: 1.2f, vk: WinAPI.KeyCode.LMenu);
            AddKey("", width: -1, vk: WinAPI.KeyCode.Space);
            AddKey("alt", width: 1.2f, vk: WinAPI.KeyCode.RMenu);
            AddKey("ctrl", width: 1.2f, vk: WinAPI.KeyCode.RControl);
            AddRow();

            ExpandKeys();
        }

        void Timer_Tick(object sender, EventArgs e)
        {
            if (_script.Count == 0)
            {
                _timer.Enabled = false;
                return;
            }

            int cur = (int)((DateTime.Now - _started).TotalMilliseconds);
            if (cur >= _script[0].At)
            {
                bool found = false;
                foreach (var key in _keys)
                {
                    if (key.Desc == _script[0].Key)
                    {
                        if (!found)
                        {
                            found = true;
                            WinAPI.keybd_event((byte)key.VK, 0, _script[0].Down ? 0 : WinAPI.KeybdEventFlag.KeyUp, 0);
                        }
                        key.Down = _script[0].Down;
                    }
                }
                Invalidate();
                _script.RemoveAt(0);
            }
        }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == 0x312)
            {
                bool found = false;
                for (int i = 0; ; i++)
                {
                    var temp = GetScript(i);
                    if (temp == null)
                    {
                        break;
                    }
                    if ((int)m.WParam == (i + 1))
                    {
                        found = true;
                        _started = DateTime.Now + TimeSpan.FromMilliseconds(500);
                        _script = temp;
                        _timer.Enabled = true;
                        break;
                    }
                }

                if (!found)
                {
                    Debug.WriteLine("0x{0:X} 0x{1:X08}", m.WParam.ToInt32(), m.LParam.ToInt32());
                }
            }
            base.WndProc(ref m);
        }

        class Key
        {
            public string Desc;
            public float X;
            public float Y;
            public float Width;
            public float Height;
            public WinAPI.KeyCode VK;
            public RectangleF Rect;
            public bool Down;
        }

        List<Key> _keys = new List<Key>();

        float _y = 0;
        float _x = 0;
        float _width = 0;
        float _height = 0;

        void AddKeys(params string[] keys)
        {
            foreach (var key in keys)
            {
                AddKey(key);
            }
        }

        void AddKey(string key, float width = 1f, WinAPI.KeyCode vk = WinAPI.KeyCode.None)
        {
            if (key.Length == 1 && ((key.ToUpper()[0] >= 'A' && key.ToUpper()[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9')))
            {
                vk = (WinAPI.KeyCode)key.ToUpper()[0];
            }

            Key temp = new Key
            {
                Desc = key,
                X = _x,
                Y = _y,
                Width = width,
                Height = 1f,
                VK = vk,
                Down = false,
            };
            _x += width;
            _width = Math.Max(_width, _x);
            _keys.Add(temp);
        }

        void AddRow()
        {
            _y++;
            _x = 0;
            _height = Math.Max(_height, _y);
        }

        void ExpandKeys()
        {
            for (float y = 0; y < _height; y++)
            {
                float usedWidth = 0;
                foreach (var key in _keys)
                {
                    if (key.Y == y)
                    {
                        if (key.Width >= 0)
                        {
                            usedWidth += key.Width;
                        }
                    }
                }

                float x = 0;
                foreach (var key in _keys)
                {
                    if (key.Y == y)
                    {
                        key.X = x;
                        if (key.Width < 0)
                        {
                            key.Width = _width - usedWidth;
                        }
                        x += key.Width;
                    }
                }
            }
        }

        void KeyboardForm_Resize(object sender, EventArgs e)
        {
            Invalidate();
        }

        void KeyboardForm_Paint(object sender, PaintEventArgs e)
        {
            var g = e.Graphics;
            using (var font = new Font("Segoe UI", 10))
            using (var sf = (StringFormat)StringFormat.GenericDefault.Clone())
            {
                sf.Alignment = StringAlignment.Center;
                sf.LineAlignment = StringAlignment.Center;
                foreach (var key in _keys)
                {
                    var rt = new RectangleF(
                        key.X / _width * ClientSize.Width,
                        key.Y / _height * ClientSize.Height,
                        key.Width / _width * ClientSize.Width,
                        key.Height / _height * ClientSize.Height);
                    key.Rect = rt;
                    if (key.Down)
                    {
                        g.FillRectangle(Brushes.Black, rt.X, rt.Y, rt.Width, rt.Height);
                    }
                    g.DrawRectangle(Pens.Red, rt.X, rt.Y, rt.Width, rt.Height);
                    g.DrawString(key.Desc, font, Brushes.Blue, rt, sf);
                }
            }
        }
    }
}
