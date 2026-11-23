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

        // With --demo the evaluate script runs by itself once everything is
        // in place, and the app closes when it's done, for record_demo.py
        public static bool Demo = false;

        [STAThread]
        static void Main(string[] args)
        {
            Demo = args.Contains("--demo");
            Application.SetHighDpiMode(HighDpiMode.DpiUnawareGdiScaled);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            Code = new TextForm("MicroKeys.py", "MicroKeys.py", 11);
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
                    SetPosition(Code, 15, 35, 470, 300);
                    break;
                case 2:
                    SetMicroKeys(495, 145, 490, 190);
                    break;
                case 3:
                    SetPosition(Other, 495, 35, 490, 100);
                    break;
                case 4:
                    SetPosition(Keyboard, 155, 345, 690, 230);
                    break;
                case 5:
                    SetPosition(Control, 1210, 75, -1, -1);
                    break;
                case 6:
                    if (Demo)
                    {
                        Control.RunDemo();
                    }
                    break;
                default:
                    return false;
            }
            return true;
        }

        public static IntPtr FindMicroKeys()
        {
            // The MicroKeys log window, which lives hidden behind the tray icon
            return WinAPI.FindWindow("MicroKeysMainWindow", null);
        }

        static void SetMicroKeys(int x, int y, int width, int height)
        {
            var hwnd = FindMicroKeys();
            if (hwnd != IntPtr.Zero)
            {
                WinAPI.ShowWindow(hwnd, 1);
                WinAPI.MoveWindow(hwnd, x, y, width, height, true);
                WinAPI.SetForegroundWindow(hwnd);
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
