using System;

using IASL.Models;

namespace IASL.ViewModels
{
    public class StockDetailViewModel : BaseViewModel
    {
        public Stock Stock { get; set; }
        public StockDetailViewModel(Stock stock = null)
        {
            Title = stock?.Code;
            Stock = stock;
        }
    }
}
