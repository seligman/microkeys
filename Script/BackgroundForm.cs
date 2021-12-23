using System;
using System.Collections.Generic;
using System.Text;
using System.Windows.Forms;

namespace Script
{
    class BackgroundForm : Form
    {
        public BackgroundForm()
        {
            ControlBox = false;
            DoubleClick += BackgroundForm_DoubleClick;
            BackColor = Program.BackColor;
        }

        void BackgroundForm_DoubleClick(object sender, EventArgs e)
        {
            Close();
        }
    }
}
