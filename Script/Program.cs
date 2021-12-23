using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Script
{
    static class Program
    {
        public static Color ForeColor = Color.FromArgb(255, 255, 220);
        public static Color BackColor = Color.FromArgb(0, 0, 0);

        public static TextForm Code;
        public static TextForm Other;
        public static KeyboardForm Keyboard;
        public static BackgroundForm Background;
        public static ControlForm Control;

        public static bool ShowTitleBars = false;

        [STAThread]
        static void Main()
        {
            Application.SetHighDpiMode(HighDpiMode.DpiUnawareGdiScaled);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            Code = new TextForm("MicroKeys.py", "MicroKeys.py");
            Other = new TextForm("Untitled");
            Keyboard = new KeyboardForm();
            Background = new BackgroundForm();
            Control = new ControlForm();

            Application.Run(Background);
        }

        public static bool FixUpLocations(int step)
        {
            switch (step)
            {
                case 0:
                    SetPosition(Background, -50, -50, 1200, 1200);
                    break;
                case 1:
                    SetPosition(Code, 30, 50, 648, 390);
                    break;
                case 2:
                    SetMicroKeys(413, 128, 369, 223);
                    break;
                case 3:
                    SetPosition(Other, 680, 48, 300, 331);
                    break;
                case 4:
                    SetPosition(Keyboard, 286, 378, 690, 204);
                    break;
                case 5:
                    SetPosition(Control, 1210, 75, -1, -1);
                    break;
                default:
                    return false;
            }
            return true;
        }

        static IntPtr _hwndMicro = IntPtr.Zero;
        static void SetMicroKeys(int x, int y, int width, int height)
        {
            WinAPI.EnumWindows(EnumProc, IntPtr.Zero);
            if (_hwndMicro != IntPtr.Zero)
            {
                WinAPI.MoveWindow(_hwndMicro, x, y, width, height, true);
                WinAPI.SetForegroundWindow(_hwndMicro);
            }
        }

        static bool EnumProc(IntPtr hWnd, IntPtr lParam)
        {
            var sb = new StringBuilder(255);
            WinAPI.GetWindowText(hWnd, sb, 255);
            if (sb.ToString() == "MicroKeys")
            {
                _hwndMicro = hWnd;
                return false;
            }
            else
            {
                return true;
            }
        }

        static void SetPosition(Form form, int x, int y, int width, int height)
        {
            if (x != -1 && y != -1)
            {
                form.Location = new Point(x, y);
            }

            if (width > 0 && height > 0)
            {
                form.Size = new Size(width, height);
            }
            WinAPI.SetForegroundWindow(form.Handle);
        }
    }
}
