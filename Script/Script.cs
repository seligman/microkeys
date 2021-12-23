using System;
using System.Collections.Generic;
using System.Text;

/*
        Script language:
    !<delay>  - Delay <delay> ms
    `<string> - Type out <string> as is
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
                    for (int i = 1; i < cur.Length; i++)
                    {
                        ret.Add(new Script()
                        {
                            Key = cur.Substring(i, 1),
                            Down = true,
                            At = at,
                        });
                        at += delay;
                        ret.Add(new Script()
                        {
                            Key = cur.Substring(i, 1),
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
    }
}
