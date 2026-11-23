using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace Script
{
    [System.ComponentModel.DesignerCategory("Code")]
    class ControlForm : Form
    {
        int _buttonWidth = 150;
        int _buttonHeight = 30;

        public ControlForm()
        {
            AddButton("Get sizes", GetSizes_Click);
            AddButton("Run Ctrl+Alt+E script", RunEvaluate_Click);
            AddButton("Run test script", RunTest_Click);
            AddButton("Close", Close_Click);

            ClientSize = new Size(_buttonWidth + 10, _y + 5);
            MinimizeBox = false;
            MaximizeBox = false;
            Text = "Control";
            StartPosition = FormStartPosition.Manual;

            Show();
        }

        void RunTest_Click(object sender, EventArgs e)
        {
            Program.Other.BringToFront();
            Program.Keyboard.RunScript(Script.FollowScript(250,
                 "'This is a test! 12+12=24?"
            ));
        }

        void RunEvaluate_Click(object sender, EventArgs e)
        {
            RunEvaluate();
        }

        void RunEvaluate()
        {
            // Type a formula, select it, and press the hotkey from demo/MicroKeys.py
            Program.Other.BringToFront();
            Program.Other.Activate();
            WinAPI.SetForegroundWindow(Program.Other.Handle);
            Program.Keyboard.RunScript(Script.FollowScript(100,
                "'(100 + 11) * 9"
            ), Script.FollowScript(250,
                "+ctrl",
                ">a",
                "-ctrl",
                "+ctrl",
                "+alt",
                ">e",
                "-alt",
                "-ctrl"
            ));
        }

        public void RunDemo()
        {
            // Run the script, then leave the result up for a few seconds, note what
            // the text box ended up with for record_demo.py to check, and close
            Program.Keyboard.Finished += (sender, e) =>
            {
                var timer = new Timer { Interval = 4000, Enabled = true };
                timer.Tick += (s, a) =>
                {
                    timer.Enabled = false;
                    File.WriteAllText(Path.Combine(Path.GetTempPath(), "microkeys_demo_result.txt"), Program.Other.Content);
                    Program.Background.Close();
                };
            };
            RunEvaluate();
        }

        void GetSizes_Click(object sender, EventArgs e)
        {
            Debug.WriteLine("---------------------------------------------------------------------------------");
            Debug.WriteLine($"SetPosition(Background, {Program.Background.Location.X}, {Program.Background.Location.Y}, {Program.Background.Size.Width}, {Program.Background.Size.Height});");
            Debug.WriteLine($"SetPosition(Code, {Program.Code.Location.X}, {Program.Code.Location.Y}, {Program.Code.Size.Width}, {Program.Code.Size.Height});");
            if (WinAPI.GetWindowRect(Program.FindMicroKeys(), out WinAPI.RECT rt))
            {
                Debug.WriteLine($"SetMicroKeys({rt.Left}, {rt.Top}, {rt.Width}, {rt.Height});");
            }
            Debug.WriteLine($"SetPosition(Other, {Program.Other.Location.X}, {Program.Other.Location.Y}, {Program.Other.Size.Width}, {Program.Other.Size.Height});");
            Debug.WriteLine($"SetPosition(Keyboard, {Program.Keyboard.Location.X}, {Program.Keyboard.Location.Y}, {Program.Keyboard.Size.Width}, {Program.Keyboard.Size.Height});");
            Debug.WriteLine($"SetPosition(Control, {Program.Control.Location.X}, {Program.Control.Location.Y}, -1, -1);");
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
