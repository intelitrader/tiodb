using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace tioLogReplay.Libs
{

    // This class enables tuple use for any IList object
    public static class Extensions
    {
        public static void Deconstruct<T>(this IList<T> list, out T first, out IList<T> rest)
        {

            first = list.Count > 0 ? list[0] : default; // or throw
            rest = list.Skip(1).ToList();
        }

        public static void Deconstruct<T>(this IList<T> list, out T first, out T second, out IList<T> rest)
        {
            first = list.Count > 0 ? list[0] : default; // or throw
            second = list.Count > 1 ? list[1] : default; // or throw
            rest = list.Skip(2).ToList();
        }
        public static void Deconstruct<T>(this IList<T> list, out T first, out T second, out T third, out T fourth, out T fifth, out T sixth, out T seventh, out IList<T> rest)
        { 
            first = list.Count > 0 ? list[0] : default; // or throw
            second = list.Count > 1 ? list[1] : default; // or throw
            third = list.Count > 2 ? list[2] : default; // or throw
            fourth = list.Count > 3 ? list[3] : default; // or throw
            fifth = list.Count > 4 ? list[4] : default; // or throw
            sixth = list.Count > 5 ? list[5] : default; // or throw
            seventh = list.Count > 6 ? list[6] : default; // or throw
            rest = list.Skip(4).ToList();
        }
    }
}
