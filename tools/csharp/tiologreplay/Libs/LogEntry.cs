using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading.Tasks.Dataflow;

namespace tioLogReplay.Libs
{
    public class LogEntry
    {
        public DateTime Time { get; set; }
        public string Command { get; set; }
        public string Handle { get; set; }
        public string Key { get; set; }
        public string Value { get; set; }
        public string Data { get; set; }
        public string CurrentField { get; set; }

        public LogEntry(string entry)
        {
            if (entry.EndsWith(",n,"))
                entry = entry.Remove(entry.Length - 3);

	    string dateTime;
            string keyInfo;
            string valueInfo;
            string key;
            string value;

            // Set some values to their respective properties // Returns key's value info.
            // Tuple implementaion may slow tiologreplay, added for readability
            (dateTime, this.Command, this.Handle, keyInfo, key, valueInfo, value, _) = entry.Split(',', 7);

            if(Command == "create" || Command == "open")
            {
                this.Key = key;
                this.Value = value;
            }
            else
            {
                // Set values for the final command
                this.Key = Deserialize(keyInfo);
                this.Value = Deserialize(valueInfo);

                if (this.Key != null)
                    this.Data += $"\r\n{key}";
                if (this.Value != null)
                    this.Data += $"\r\n{value}";
            }

            var arr = dateTime.Split(" ");
            var date = arr[0];
            var time = arr[1];

            this.Time = DateTime.Parse(time);
        }

        private string Deserialize(string info)
        {
            // Take type out info (format: "s12"; 's' for string. 'n' is null)
            var type = info[0]; 

            if (type == 'n')
            {
                CurrentField = "key";
                return null;
            }

            // Get only type's numbers
            var sizeL = info.Skip(1); 


            // Turn stringfied numbers into actual numbers
            var size = int.Parse(string.Join("", sizeL)); 

            var field = GetField();

            return type switch
            {
                's' => ($"{field} string {size}"),
                'i' => ($"{field} int {size}"),
                'd' => ($"{field} double {size}"),
                _ => null
            };
        }

        private string GetField()
        {
            if (CurrentField == null)
            {
                CurrentField = "key";
                return "key";
            }
            else
            {
                return "value";
            }
        }

        public string ToFullCommand()
        {
            string fullCommand;

            if (Data == null)
                return Command + ' ' +  Key + ' '  + Value + '\n';

            if (Key != null)
                fullCommand = Command + ' ' + Handle + ' ' + Key + ' ' + Value + Data;
            else
                fullCommand = Command + ' ' + Handle + ' ' + Value + Data;

            return fullCommand + "\r\n" + '\n'; //adds a new line to the command 
        }
    }
}
