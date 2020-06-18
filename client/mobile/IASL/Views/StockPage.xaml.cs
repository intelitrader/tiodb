using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xamarin.Forms;
using Xamarin.Forms.Xaml;

using IASL.Models;
using IASL.Views;
using IASL.ViewModels;

namespace IASL.Views
{
    // Learn more about making custom code visible in the Xamarin.Forms previewer
    // by visiting https://aka.ms/xamarinforms-previewer
    [DesignTimeVisible(false)]
    public partial class StocksPage : ContentPage
    {
        StocksViewModel viewModel;

        public StocksPage()
        {
            InitializeComponent();

            BindingContext = viewModel = new StocksViewModel();
            
            Device.StartTimer(TimeSpan.FromSeconds(2), () =>
            {
                Device.BeginInvokeOnMainThread(() => OnAppearing());
                return true;
            });
        }

        async void OnStockSelected(object sender, EventArgs args)
        {
            var layout = (BindableObject)sender;
            var stock = (Stock)layout.BindingContext;
            await Navigation.PushAsync(new StockDetailPage(new StockDetailViewModel(stock)));
        }

        async void AddStock_Clicked(object sender, EventArgs e)
        {
            await Navigation.PushModalAsync(new NavigationPage(new NewStockPage()));
        }

        protected override void OnAppearing()
        {
            base.OnAppearing();

            if (viewModel.Stocks.Count != 0)
            {
                viewModel.IsBusy = true;
            }
            else
            {
                viewModel.IsBusy = false;
            }


        }
    }
}