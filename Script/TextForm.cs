using System;
using System.Collections.Generic;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace Script
{
    class TextForm : Form
    {
        TextBox _text;
        public TextForm(string title)
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
            Controls.Add(_text);
            Show();
        }
    }
}
