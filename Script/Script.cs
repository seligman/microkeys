using System;
using System.Collections.Generic;
using System.Text;

/*
        Script language:
    !<delay>  - Delay <delay> ms
    '<string> - Type out <string> as is
    ><key>    - Press and release <key>
    +<key>    - Press <key>
    -<key>    - Release <key>
*/

namespace Script
{
    class Script
    {
        public string Key;
        public bool Down;
        public int At;

        public static List<Script> FollowScript(int delay, params string[] keys)
        {
            List<Script> ret = new List<Script>();
            int at = 0;
            foreach (var cur in keys)
            {
                if (cur.Substring(0, 1) == "!")
                {
                    at += int.Parse(cur.Substring(1));
                }
                else if (cur.Substring(0, 1) == "'")
                {
                    bool shift = false;

                    for (int i = 1; i < cur.Length; i++)
                    {
                        GetKey(cur.Substring(i, 1), out string desc, out bool wantShift);
                        if (desc != null)
                        {
                            if (desc == " ")
                            {
                                desc = "";
                            }
                            if (shift != wantShift)
                            {
                                shift = wantShift;
                                ret.Add(new Script()
                                {
                                    Key = "shift",
                                    Down = wantShift,
                                    At = at,
                                });
                                at += delay;
                            }
                            ret.Add(new Script()
                            {
                                Key = desc,
                                Down = true,
                                At = at,
                            });
                            at += delay;
                            ret.Add(new Script()
                            {
                                Key = desc,
                                Down = false,
                                At = at,
                            });
                            at += delay;
                        }
                    }

                    if (shift)
                    {
                        ret.Add(new Script()
                        {
                            Key = "shift",
                            Down = false,
                            At = at,
                        });
                        at += delay;
                    }
                }
                else if (cur.Substring(0, 1) == ">")
                {
                    ret.Add(new Script()
                    {
                        Key = cur.Substring(1),
                        Down = true,
                        At = at,
                    });
                    at += delay;
                    ret.Add(new Script()
                    {
                        Key = cur.Substring(1),
                        Down = false,
                        At = at,
                    });
                    at += delay;
                }
                else
                {
                    ret.Add(new Script()
                    {
                        Key = cur.Substring(1),
                        Down = cur.Substring(0, 1) == "+",
                        At = at,
                    });
                    at += delay;
                }
            }
            return ret;
        }

        static void GetKey(string cur, out string desc, out bool wantShift)
        {
            string lower = " `1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./";
            string upper = " ~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";
            if (lower.Contains(cur))
            {
                desc = cur;
                wantShift = false;
            }
            else if (upper.Contains(cur))
            {
                desc = lower.Substring(upper.IndexOf(cur), 1);
                wantShift = true;
            }
            else
            {
                desc = null;
                wantShift = false;
            }
        }
    }
}
