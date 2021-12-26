using System;
using System.Collections.Generic;
using System.Text;
using System.Windows.Forms;

namespace Script
{
    [System.ComponentModel.DesignerCategory("Code")]
    class BackgroundForm : Form
    {
        Timer _timer;
        DateTime _timerTick;
        int _step = 0;

        public BackgroundForm()
        {
            ControlBox = Program.ShowTitleBars;
            DoubleClick += BackgroundForm_DoubleClick;
            BackColor = Program.BackColor;
            StartPosition = FormStartPosition.Manual;
            if (Program.ShowTitleBars)
            {
                Text = "Background";
            }
            _timer = new Timer();
            _timer.Interval = 50;
            _timer.Enabled = true;
            _timerTick = DateTime.Now + TimeSpan.FromMilliseconds(1000);
            _timer.Tick += Timer_Tick;
        }

        void Timer_Tick(object sender, EventArgs e)
        {
            if (DateTime.Now >= _timerTick)
            {
                if (!Program.FixUpLocations(_step))
                {
                    _timer.Enabled = false;
                }
                _timerTick += TimeSpan.FromMilliseconds(100);
                _step++;
            }
        }

        void BackgroundForm_DoubleClick(object sender, EventArgs e)
        {
            Close();
        }
    }
}
