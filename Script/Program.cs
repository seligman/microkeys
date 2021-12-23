using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Script
{
    static class Program
    {
        public static Color ForeColor = Color.FromArgb(255, 255, 220);
        public static Color BackColor = Color.FromArgb(255, 0, 0);

        public static TextForm Code;
        public static TextForm Other;

        [STAThread]
        static void Main()
        {
            Application.SetHighDpiMode(HighDpiMode.SystemAware);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            //Code = new TextForm("MicroPython.py");
            //Other = new TextForm("Untitled");
            //Application.Run(new BackgroundForm());
            Application.Run(new KeyboardForm());
        }
    }
}
