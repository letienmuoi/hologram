using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace hologram
{
    public class Win32
    {
        //mouse position
        [DllImport("User32.Dll")]
        public static extern long SetCursorPos(int x, int y);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool GetCursorPos(ref POINT pt);

        public const int MOUSEEVENTF_MOVE = 0x0001;
        public const int MOUSEEVENTF_ABSOLUTE = 0x8000;

        [DllImport("user32.dll", CharSet = CharSet.Auto, CallingConvention = CallingConvention.StdCall)]
        public static extern void mouse_event(long dwFlags, long dx, long dy, long cButtons, long dwExtraInfo);

        [StructLayout(LayoutKind.Sequential)]
        public struct POINT
        {
            public int x;
            public int y;
        }

        //always on top
        public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
        private const UInt32 SWP_NOSIZE = 0x0001;
        private const UInt32 SWP_NOMOVE = 0x0002;
        public const UInt32 TOPMOST_FLAGS = SWP_NOMOVE | SWP_NOSIZE;

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

        //dragable
        public const int WM_NCLBUTTONDOWN = 0xA1;
        public const int HT_CAPTION = 0x2;

        [DllImport("user32.dll")]
        public static extern int SendMessage(IntPtr hWnd, int Msg, int wParam, int lParam);
        [DllImport("user32.dll")]
        public static extern bool ReleaseCapture();
    }

    public partial class Form1 : Form
    {
        Timer timer1;
        RegistryKey rkApp = Registry.CurrentUser.OpenSubKey("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", true);
        MenuItem start_with_windows = new MenuItem("Start with Windows");
        Icon ico;
        public Form1()
        {
            InitializeComponent();

            //set timer
            timer1 = new Timer();
            timer1.Tick += timer1_Tick;
            timer1.Interval = 1000*60*2;
            timer1.Start();

            //transparent window
            this.BackColor = Color.LimeGreen;
            this.TransparencyKey = Color.LimeGreen;

            //set windows postion
            this.StartPosition = FormStartPosition.Manual;
            this.Location = GetLastLocation();
            this.FormBorderStyle = FormBorderStyle.None;
            this.Size = new Size(120, 120);

            
            
            //hide icon on taskbar
            this.ShowInTaskbar = false;

            //context menu
            ContextMenu cm = new ContextMenu();
            start_with_windows.Click += new EventHandler(StartWithWindows_Click);
            //check status
            if (rkApp.GetValue("hologram") == null) start_with_windows.Checked = false;
            else start_with_windows.Checked = true;
            cm.MenuItems.Add(start_with_windows);
            cm.MenuItems.Add(new MenuItem("Exit", new EventHandler(Exit_Click)) );
            this.pictureBox1.ContextMenu = cm;

            //moveable
            this.pictureBox1.MouseDown += Event_MouseDown;

            //always on top
            Win32.SetWindowPos(this.pictureBox1.Handle, Win32.HWND_TOPMOST, 0, 0, 0, 0, Win32.TOPMOST_FLAGS);
            Win32.SetWindowPos(this.Handle, Win32.HWND_TOPMOST, 0, 0, 0, 0, Win32.TOPMOST_FLAGS);
        }

        private void Event_MouseDown(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Left)
            {
                Win32.ReleaseCapture();
                Win32.SendMessage(Handle, Win32.WM_NCLBUTTONDOWN, Win32.HT_CAPTION, 0);
            }
        }

        private void StartWithWindows_Click(object sender, EventArgs e)
        {
            start_with_windows.Checked = !start_with_windows.Checked;
            if(start_with_windows.Checked) rkApp.SetValue("hologram", Application.ExecutablePath);
            else rkApp.DeleteValue("hologram", false);
        }

        private void timer1_Tick(object sender, EventArgs e)
        {
            int rand_number = GetRandomNumber();
            Win32.mouse_event(Win32.MOUSEEVENTF_MOVE, rand_number, rand_number, 0, 0);
            System.Threading.Thread.Sleep(10);
            Win32.mouse_event(Win32.MOUSEEVENTF_MOVE, - rand_number, - rand_number, 0, 0);
        }

        private void Exit_Click(object sender, EventArgs e)
        {
            SaveLastPosition();
            ico.Dispose();
            Application.Exit();
        }

        private Point GetLastLocation()
        {
            string filePath = Path.GetTempPath() + "\\hologram_location.txt";
            if (File.Exists(filePath))
            {
                string[] lines = File.ReadAllLines(filePath);
                if(lines.Count() >= 2)
                    return new Point(Int32.Parse(lines[0]), Int32.Parse(lines[1]));
            }
            return new Point(1800, 900); //default value
        }

        private void SaveLastPosition()
        {
            string filePath = Path.GetTempPath() + "\\hologram_location.txt";
            Point p = this.PointToScreen(new Point(0, 0));
            File.WriteAllText(filePath, p.X.ToString() + '\n' + p.Y.ToString());
        }

        private static readonly Random getrandom = new Random();
        public static int GetRandomNumber(int min = 2, int max = 5)
        {
            lock (getrandom) // synchronize
            {
                return getrandom.Next(min, max);
            }
        }
    }
}
