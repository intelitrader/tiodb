using System;

namespace IASL.Models
{
    public class Stock
    {
        public string Id { get; set; }
        public string Code { get; set; }
        public Decimal Price { get; set; }
        public Decimal OpeningPrice { get; set; }
        public Decimal MaxPrice { get; set; }
        public Decimal MinPrice { get; set; }
        public Decimal AveragePrice { get; set; }
    }
}