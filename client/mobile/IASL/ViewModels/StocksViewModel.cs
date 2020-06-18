using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Threading.Tasks;

using Xamarin.Forms;

using IASL.Models;
using IASL.Views;
using IASL.Services;
using System.Linq;

namespace IASL.ViewModels
{
    public class StocksViewModel : BaseViewModel
    {
        public ObservableCollection<Stock> Stocks { get; set; }
        public Command LoadStocksCommand { get; set; }

        public StocksViewModel()
        {
            Title = "Lista de Ativos";
            Stocks = new ObservableCollection<Stock>();
            LoadStocksCommand = new Command(async () => await ExecuteLoadStocksCommand());

            MessagingCenter.Subscribe<NewStockPage, Stock>(this, "AddStock", async (obj, stock) =>
            {
                var newStock = stock as Stock;
                Stocks.Add(newStock);
                await DataStore.AddStockAsync(newStock);
            });
            
        }

        async Task ExecuteLoadStocksCommand()
        {
            IsBusy = true;

            try
            {
                var newStocks = new ObservableCollection<Stock>(Stocks);
                Stocks.Clear();
                var stocks = await DataStore.RandomizeStocksAsync(newStocks, true);
                foreach (var stock in stocks)
                {
                    Stocks.Add(stock);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex);
            }
            finally
            {
                IsBusy = false;
            }
        }
    }
}