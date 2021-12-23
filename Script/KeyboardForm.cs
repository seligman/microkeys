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

        void AddKey(string key, float width = 1f)
        {
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

            /*
            AddKey("esc");
            AddKey("f1");
            AddKey("f2");
            AddKey("f3");
            AddKey("f4");
            AddKey("f5");
            AddKey("f6");
            AddKey("f7");
            AddKey("f8");
            AddKey("f9");
            AddKey("f10");
            AddKey("f11");
            AddKey("f12");
            AddKey("del", -1);
            AddRow();
            */

            AddKey("`");
            AddKeys("1", "2", "3", "4", "5", "6", "7", "8", "9", "0");
            AddKey("-");
            AddKey("=");
            AddKey("<-", 1.33f);
            AddRow();

            AddKey("tab", -1);
            AddKeys("q", "w", "e", "r", "t", "y", "u", "i", "o", "p");
            AddKey("[");
            AddKey("]");
            AddKey("\\");
            AddRow();

            AddKey("caps", 1.66f);
            AddKeys("a", "s", "d", "f", "g", "h", "j", "k", "l");
            AddKey(";");
            AddKey("'");
            AddKey("enter", -1);
            AddRow();

            AddKey("shift", 2f);
            AddKeys("z", "x", "c", "v", "b", "n", "m");
            AddKey(",");
            AddKey(".");
            AddKey("/");
            AddKey("shift", -1);
            AddRow();

            AddKey("ctrl", 1.2f);
            AddKey("win");
            AddKey("alt", 1.2f);
            AddKey("", -1);
            AddKey("alt", 1.2f);
            AddKey("ctrl", 1.2f);
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
