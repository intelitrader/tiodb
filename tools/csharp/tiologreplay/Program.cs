using CommandLine;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;
using tioLogReplay.Libs;

namespace tioLogReplay
{
    class Program
    {
        static void Main(string[] args)
        {
            Parser.Default.ParseArguments<Options>(args)
                  .WithParsed(o =>
                  {
                      if (!File.Exists(o.Path))
                          throw new FileNotFoundException("Log not found");

                      var tioLogParser = new TioLogParser(
                          o.Address,
                          o.Path,
                          o.Speed,
                          o.Delay,
                          o.Follow,
                          o.Pause);

                      tioLogParser.Clone();
                      tioLogParser.Replay();
                  });
        }
    }
}