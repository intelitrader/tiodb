using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.Tracing;
using System.IO;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace tioLogReplay.Libs
{
    public class TioLogParser
    {
        private TioConnection Tio { get; set; }
        private string Path { get; set; }
        public int Speed { get; }
        public int Delay { get; }
        public bool Follow { get; }
        public bool Pause { get; }

        public TioLogParser(string address, string path, int speed, int delay, bool follow, bool pause)
        {
            Tio = TioConnection.Connect(address);
            Path = path;
            Speed = speed;
            Delay = delay;
            Follow = follow;
            Pause = pause;
        }

        public void Clone()
        {
            string entry;
            LogEntry log;
            using (StreamReader reader = new StreamReader(new FileStream(Path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)))
            {
                while ((entry = reader.ReadLine()) != null)
                {
                    log = new LogEntry(entry);
                    Tio.SendCommand(log.ToFullCommand());
                }
            }
        }

        public void Replay()
        {
            using (StreamReader reader = new StreamReader(new FileStream(Path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)))
            {
                LogEntry log;
                string line;

                //start at the end of the file
                long lastMaxOffset = reader.BaseStream.Length;

                Console.WriteLine("\nWaiting for file to grow...");

                bool commandSent = false;

                while (true)
                {
                    //if a command was not sent, don't print that the program is waiting 
                    if (commandSent)
                    {
                        Console.WriteLine("\nWaiting for file to grow...");
                        commandSent = false;
                    }

                    Thread.Sleep(100);

                    //if the file size has not changed, idle
                    if (reader.BaseStream.Length == lastMaxOffset)
                        continue;

                    //seek to the last max offset
                    reader.BaseStream.Seek(lastMaxOffset, SeekOrigin.Begin);

                    //read out of the file until the EOF
                    while ((line = reader.ReadLine()) != null)
                    {
                        log = new LogEntry(line);

                        if (Delay > 0)
                            IdleByLogTime(log);

                        Tio.SendCommand(log.ToFullCommand());
                    }

                    commandSent = true;

                    //update the last max offset
                    lastMaxOffset = reader.BaseStream.Position;
                }
            }
        }

        private void IdleByLogTime(LogEntry log)
        {
            TimeSpan timePassed = DateTime.Now - log.Time;
            var waitDecimal = Delay - Math.Floor(timePassed.TotalSeconds);
            var wait = (int)Math.Floor(waitDecimal);

            if (wait >= 0)
            {
                Console.WriteLine($"\nDelay time is {Delay} seconds. It will take more {wait} seconds for the command to be sent.\n");
                Thread.Sleep(wait * 1000);
            }
            else
            {
                Console.WriteLine("Delay was timeouted, inputed an entry manually or your server time is wrong.");
                Console.WriteLine("Your command will be sent without any delay.");
            }
        }
    }
}


