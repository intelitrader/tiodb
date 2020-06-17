using CommandLine;
using System.ComponentModel;

namespace tioLogReplay
{
    public class Options
    {
        [Option("address",
                Required = false,
                HelpText = "Defines the address for Tio replay server.\n" +
                           "It supports 'ip_address', 'ip_address:port' and 'port' (which will use localhost) formats.\n" +
                           "Default is localhost:2605",
                Default = "localhost:2605")]

        public string Address { get; set; }

        [Option("file_path",
            Required = true,
            HelpText = "You need to specify the full path for your log file")]
        public string Path { get; set; }

        [Option("speed",
            Required = false,
            HelpText = "Speed that messages will be sent to Tio. 0 = as fast as possible",
            Default = 0)]
        public int Speed { get; set; }

        [Option("delay",
            Required = false,
            HelpText = "Message delay relative to original message time. " +
            "We will wait if necessary to make sure the message will be replicated XX second after the time message was written to log",
            Default = 0)]
        public int Delay { get; set; }

        [Option("follow",
            Required = false,
            HelpText = "TODO",
            Default = false)]
        public bool Follow { get; set; }

        [Option("pause",
            Required = false,
            HelpText = "Pauses the server while loading. * *This will disconnect all client during load time * *",
            Default = false)]
        public bool Pause { get; set; }
    }
}