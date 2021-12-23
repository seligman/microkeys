using System;
using System.Collections.Generic;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace Script
{
    class KeyboardForm : Form
    {
        class Key
        {
            public string Desc;
            public float X;
            public float Y;
            public float Width;
            public float Height;
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
            // TODO: Use VK
            Key temp = new Key
            {
                Desc = key,
                X = _x,
                Y = _y,
                Width = width,
                Height = 1f,
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

        public KeyboardForm()
        {
            Paint += KeyboardForm_Paint;
            Resize += KeyboardForm_Resize;
            Width = 1000;

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

            AddKey("`", vk: WinAPI.KeyCode.OemBackTab); // TODO
            AddKeys("1", "2", "3", "4", "5", "6", "7", "8", "9", "0");
            AddKey("-", vk: WinAPI.KeyCode.OemMinus);
            AddKey("=", vk: WinAPI.KeyCode.Equal);
            AddKey("<-", width: 1.33f, vk: WinAPI.KeyCode.Backspace);
            AddRow();

            AddKey("tab", width: -1, vk: WinAPI.KeyCode.Tab);
            AddKeys("q", "w", "e", "r", "t", "y", "u", "i", "o", "p");
            AddKey("[", vk: WinAPI.KeyCode.None); // TODO
            AddKey("]", vk: WinAPI.KeyCode.None); // TODO
            AddKey("\\", vk: WinAPI.KeyCode.None); // TODO
            AddRow();

            AddKey("caps", width: 1.66f, vk: WinAPI.KeyCode.None); // TODO
            AddKeys("a", "s", "d", "f", "g", "h", "j", "k", "l");
            AddKey(";", vk: WinAPI.KeyCode.None); // TODO
            AddKey("'", vk: WinAPI.KeyCode.None); // TODO
            AddKey("enter", width: -1, vk: WinAPI.KeyCode.Return);
            AddRow();

            AddKey("shift", 2f, vk: WinAPI.KeyCode.Shift);
            AddKeys("z", "x", "c", "v", "b", "n", "m");
            AddKey(",", vk: WinAPI.KeyCode.None); // TODO
            AddKey(".", vk: WinAPI.KeyCode.None); // TODO
            AddKey("/", vk: WinAPI.KeyCode.None); // TODO
            AddKey("shift", width: -1, vk: WinAPI.KeyCode.Shift);
            AddRow();

            AddKey("ctrl", width: 1.2f, vk: WinAPI.KeyCode.Control);
            AddKey("win", vk: WinAPI.KeyCode.None); // TODO
            AddKey("alt", width: 1.2f, vk: WinAPI.KeyCode.None); // TODO
            AddKey("", width: -1, vk: WinAPI.KeyCode.Space);
            AddKey("alt", width: 1.2f, vk: WinAPI.KeyCode.None); // TODO
            AddKey("ctrl", width: 1.2f, vk: WinAPI.KeyCode.None); // TODO
            AddRow();

            ExpandKeys();
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
                    g.DrawRectangle(Pens.Red, rt.X, rt.Y, rt.Width, rt.Height);
                    g.DrawString(key.Desc, font, Brushes.Blue, rt, sf);
                }
            }
        }
    }
}
