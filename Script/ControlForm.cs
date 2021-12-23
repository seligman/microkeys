using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace Script
{
    class ControlForm : Form
    {
        int _buttonWidth = 150;
        int _buttonHeight = 30;

        public ControlForm()
        {
            AddButton("Get Sizes", GetSizes_Click);
            AddButton("Run Alt-Z script", RunAltZ_Click);
            AddButton("Close", Close_Click);

            ClientSize = new Size(_buttonWidth + 10, _y + 5);
            MinimizeBox = false;
            MaximizeBox = false;
            Text = "Control";
            StartPosition = FormStartPosition.Manual;

            Show();
        }

        void RunAltZ_Click(object sender, EventArgs e)
        {
            var script = Script.FollowScript(250,
                "+alt",
                ">z",
                "-alt"
            );
            Program.Keyboard.RunScript(script);
            Program.Other.BringToFront();
        }

        void GetSizes_Click(object sender, EventArgs e)
        {
            Debug.WriteLine("---------------------------------------------------------------------------------");
            Debug.WriteLine($"SetPosition(Background, {Program.Background.Location.X}, {Program.Background.Location.Y}, {Program.Background.Size.Width}, {Program.Background.Size.Height});");
            Debug.WriteLine($"SetPosition(Code, {Program.Code.Location.X}, {Program.Code.Location.Y}, {Program.Code.Size.Width}, {Program.Code.Size.Height});");
            WinAPI.EnumWindows(EnumProc, IntPtr.Zero);
            Debug.WriteLine($"SetPosition(Other, {Program.Other.Location.X}, {Program.Other.Location.Y}, {Program.Other.Size.Width}, {Program.Other.Size.Height});");
            Debug.WriteLine($"SetPosition(Keyboard, {Program.Keyboard.Location.X}, {Program.Keyboard.Location.Y}, {Program.Keyboard.Size.Width}, {Program.Keyboard.Size.Height});");
            Debug.WriteLine($"SetPosition(Control, {Program.Control.Location.X}, {Program.Control.Location.Y}, -1, -1);");
        }

        bool EnumProc(IntPtr hWnd, IntPtr lParam)
        {
            var sb = new StringBuilder(255);
            WinAPI.GetWindowText(hWnd, sb, 255);
            if (sb.ToString() == "MicroKeys")
            {
                WinAPI.GetWindowRect(hWnd, out WinAPI.RECT rt);
                Debug.WriteLine($"SetMicroKeys({rt.Left}, {rt.Top}, {rt.Width}, {rt.Height});");
                return false;
            }
            else
            {
                return true;
            }
        }

        void Close_Click(object sender, EventArgs e)
        {
            Program.Background.Close();
        }

        int _y = 5;

        void AddButton(string desc, EventHandler handler)
        {
            var temp = new Button
            {
                Text = desc,
                Location = new Point(5, _y),
                Size = new Size(_buttonWidth, _buttonHeight),
            };
            _y += temp.Height + 5;
            temp.Click += handler;
            Controls.Add(temp);
        }
    }
}
