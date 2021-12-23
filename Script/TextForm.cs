using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace Script
{
    class TextForm : Form
    {
        TextBox _text;
        public TextForm(string title, string fn = null)
        {
            Text = title;
            MinimizeBox = false;
            MaximizeBox = false;
            _text = new TextBox
            {
                Dock = DockStyle.Fill,
                Multiline = true,
                Font = new Font("Consolas", 12),
                BackColor = Program.BackColor,
                ForeColor = Program.ForeColor,
                ScrollBars = ScrollBars.Both,
            };
            if (fn != null)
            {
                _text.BackColor = Color.FromArgb(30, 30, 30);
                _text.ForeColor = Color.FromArgb(212, 212, 212);
            }
            Controls.Add(_text);
            StartPosition = FormStartPosition.Manual;

            if (fn != null)
            {
                for (int i = 0; i < 4; i++)
                {
                    if (!File.Exists(fn))
                    {
                        fn = Path.Combine("..", fn);
                    }
                }
                if (File.Exists(fn))
                {
                    _text.Text = File.ReadAllText(fn);
                }
            }

            Show();
        }
    }
}
