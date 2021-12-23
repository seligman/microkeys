using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace Script
{
    class WinAPI
    {
        public enum KeyCode : uint
        {
            None = 0x0,
            LButton = 0x1,
            RButton = 0x2,
            Cancel = 0x3,
            MButton = 0x4,
            XButton1 = 0x5,
            XButton2 = 0x6,
            Backspace = 0x8,
            Tab = 0x9,
            Clear = 0xC,
            Return = 0xD,
            Shift = 0x10,
            Control = 0x11,
            Menu = 0x12,
            Alt = 0x12,
            Pause = 0x13,
            Capital = 0x14,
            Kana = 0x15,
            Hangeul = 0x15,
            Hangul = 0x15,
            Junja = 0x17,
            Final = 0x18,
            Hanja = 0x19,
            Kanji = 0x19,
            Escape = 0x1B,
            Convert = 0x1C,
            Nonconvert = 0x1D,
            Accept = 0x1E,
            ModeChange = 0x1F,
            Space = 0x20,
            Prior = 0x21,
            PageUp = 0x21,
            Next = 0x22,
            PageDown = 0x22,
            End = 0x23,
            Home = 0x24,
            Left = 0x25,
            Up = 0x26,
            Right = 0x27,
            Down = 0x28,
            Select = 0x29,
            Print = 0x2A,
            Execute = 0x2B,
            Snapshot = 0x2C,
            Insert = 0x2D,
            Delete = 0x2E,
            Help = 0x2F,
            Num0 = 0x30,
            Num1 = 0x31,
            Num2 = 0x32,
            Num3 = 0x33,
            Num4 = 0x34,
            Num5 = 0x35,
            Num6 = 0x36,
            Num7 = 0x37,
            Num8 = 0x38,
            Num9 = 0x39,
            LetA = 0x41,
            LetB = 0x42,
            LetC = 0x43,
            LetD = 0x44,
            LetE = 0x45,
            LetF = 0x46,
            LetG = 0x47,
            LetH = 0x48,
            LetI = 0x49,
            LetJ = 0x4A,
            LetK = 0x4B,
            LetL = 0x4C,
            LetM = 0x4D,
            LetN = 0x4E,
            LetO = 0x4F,
            LetP = 0x50,
            LetQ = 0x51,
            LetR = 0x52,
            LetS = 0x53,
            LetT = 0x54,
            LetU = 0x55,
            LetV = 0x56,
            LetW = 0x57,
            LetX = 0x58,
            LetY = 0x59,
            LetZ = 0x5A,
            LWin = 0x5B,
            RWin = 0x5C,
            Apps = 0x5D,
            Sleep = 0x5F,
            NumPad0 = 0x60,
            NumPad1 = 0x61,
            NumPad2 = 0x62,
            NumPad3 = 0x63,
            NumPad4 = 0x64,
            NumPad5 = 0x65,
            NumPad6 = 0x66,
            NumPad7 = 0x67,
            NumPad8 = 0x68,
            NumPad9 = 0x69,
            Multiply = 0x6A,
            Add = 0x6B,
            Separator = 0x6C,
            Subtract = 0x6D,
            Decimal = 0x6E,
            Divide = 0x6F,
            F1 = 0x70,
            F2 = 0x71,
            F3 = 0x72,
            F4 = 0x73,
            F5 = 0x74,
            F6 = 0x75,
            F7 = 0x76,
            F8 = 0x77,
            F9 = 0x78,
            F10 = 0x79,
            F11 = 0x7A,
            F12 = 0x7B,
            F13 = 0x7C,
            F14 = 0x7D,
            F15 = 0x7E,
            F16 = 0x7F,
            F17 = 0x80,
            F18 = 0x81,
            F19 = 0x82,
            F20 = 0x83,
            F21 = 0x84,
            F22 = 0x85,
            F23 = 0x86,
            F24 = 0x87,
            NumLock = 0x90,
            Scroll = 0x91,
            Equal = 0x92,
            Jisho = 0x92,
            Masshou = 0x93,
            Touroku = 0x94,
            Loya = 0x95,
            Roya = 0x96,
            LShift = 0xA0,
            RShift = 0xA1,
            LControl = 0xA2,
            RControl = 0xA3,
            LMenu = 0xA4,
            RMenu = 0xA5,
            BrowserBack = 0xA6,
            BrowserForward = 0xA7,
            BrowserRefresh = 0xA8,
            BrowserStop = 0xA9,
            BrowserSearch = 0xAA,
            BrowserFavorites = 0xAB,
            BrowserHome = 0xAC,
            VolumeMute = 0xAD,
            VolumeDown = 0xAE,
            VolumeUp = 0xAF,
            MediaNextTrack = 0xB0,
            MediaPrevTrack = 0xB1,
            MediaStop = 0xB2,
            MediaPlayPause = 0xB3,
            LaunchMail = 0xB4,
            LaunchMediaSelect = 0xB5,
            LaunchApp1 = 0xB6,
            LaunchApp2 = 0xB7,
            Oem1 = 0xBA,
            OemPlus = 0xBB,
            OemComma = 0xBC,
            OemMinus = 0xBD,
            OemPeriod = 0xBE,
            Oem2 = 0xBF,
            Oem3 = 0xC0,
            Oem4 = 0xDB,
            Oem5 = 0xDC,
            Oem6 = 0xDD,
            Oem7 = 0xDE,
            Oem8 = 0xDF,
            OemAx = 0xE1,
            Oem102 = 0xE2,
            IcoHelp = 0xE3,
            Ico00 = 0xE4,
            ProcessKey = 0xE5,
            IcoClear = 0xE6,
            Packet = 0xE7,
            OemReset = 0xE9,
            OemJump = 0xEA,
            OemPa1 = 0xEB,
            OemPa2 = 0xEC,
            OemPa3 = 0xED,
            OemWsctrl = 0xEE,
            OemCusel = 0xEF,
            OemAttn = 0xF0,
            OemFinish = 0xF1,
            OemCopy = 0xF2,
            OemAuto = 0xF3,
            OemEnlw = 0xF4,
            OemBackTab = 0xF5,
            Attention = 0xF6,
            CurrentSelection = 0xF7,
            ExtraSelection = 0xF8,
            EndOfFile = 0xF9,
            Play = 0xFA,
            Zoom = 0xFB,
            NoName = 0xFC,
            Pa1 = 0xFD,
            OemClear = 0xFE,
            MouseFirst = 0x100,
            MouseLClick = 0x101,
            MouseRClick = 0x102,
            MouseLDown = 0x103,
            MouseLUp = 0x104,
            MouseRDown = 0x105,
            MouseRUp = 0x106,
        }
        public enum KeybdEventFlag : uint
        {
            ExtendedKey = 0x1,
            KeyUp = 0x2,
        }
        [StructLayout(LayoutKind.Sequential)]
        public struct RECT
        {
            public override int GetHashCode()
            {
                return (Left.GetHashCode() ^
                    Right.GetHashCode() ^
                    Top.GetHashCode() ^
                    Bottom.GetHashCode());
            }

            public override bool Equals(object obj)
            {
                if (obj is RECT)
                {
                    return (
                        ((RECT)obj).Left == Left &&
                        ((RECT)obj).Right == Right &&
                        ((RECT)obj).Top == Top &&
                        ((RECT)obj).Bottom == Bottom);
                }
                else if (obj is Rectangle)
                {
                    return (
                        ((Rectangle)obj).Left == Left &&
                        ((Rectangle)obj).Right == Right &&
                        ((Rectangle)obj).Top == Top &&
                        ((Rectangle)obj).Bottom == Bottom);
                }
                else
                {
                    return base.Equals(obj);
                }
            }

            public int left;
            public int top;
            public int right;
            public int bottom;

            public int Left
            {
                get
                {
                    return left;
                }
                set
                {
                    left = value;
                }
            }

            public int Right
            {
                get
                {
                    return right;
                }
                set
                {
                    right = value;
                }
            }

            public int Bottom
            {
                get
                {
                    return bottom;
                }
                set
                {
                    bottom = value;
                }
            }

            public int Top
            {
                get
                {
                    return top;
                }
                set
                {
                    top = value;
                }
            }

            public void SetFrom(Control ctrl)
            {
                left = ctrl.Left;
                top = ctrl.Top;
                right = ctrl.Right;
                bottom = ctrl.Bottom;
            }

            public RECT(Rectangle rt)
            {
                this.left = rt.Left;
                this.top = rt.Top;
                this.bottom = rt.Bottom;
                this.right = rt.Right;
            }

            public RECT(int left, int top, int right, int bottom)
            {
                this.left = left;
                this.top = top;
                this.right = right;
                this.bottom = bottom;
            }

            public static implicit operator RECT(Control ctrl)
            {
                return new RECT(ctrl.Left, ctrl.Top, ctrl.Right, ctrl.Bottom);
            }

            public static implicit operator Rectangle(RECT value)
            {
                return new Rectangle(value.Left, value.Top, value.Width, value.Height);
            }

            public static implicit operator RECT(Rectangle value)
            {
                return new RECT(value.Left, value.Top, value.Right, value.Bottom);
            }

            public int Height
            {
                get
                {
                    return bottom - top;
                }
            }

            public int Width
            {
                get
                {
                    return right - left;
                }
            }

            public Size Size
            {
                get
                {
                    return new Size(Width, Height);
                }
            }
        }

        public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool SetForegroundWindow(IntPtr hWnd);
        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);
        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
        [DllImport("user32.dll")]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
        [DllImport("user32.dll")]
        public static extern bool MoveWindow(IntPtr hWnd, int X, int Y, int nWidth, int nHeight, bool bRepaint);
        [DllImport("user32.dll")]
        public static extern void keybd_event(byte bVk, byte bScan, KeybdEventFlag dwFlags, uint dwExtraInfo);
    }
}
