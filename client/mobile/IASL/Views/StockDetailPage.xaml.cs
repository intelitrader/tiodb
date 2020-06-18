using System;
using System.ComponentModel;
using Xamarin.Forms;
using Xamarin.Forms.Xaml;

using IASL.Models;
using IASL.ViewModels;
using System.Threading;
using System.Linq;

namespace IASL.Views
{
    // Learn more about making custom code visible in the Xamarin.Forms previewer
    // by visiting https://aka.ms/xamarinforms-previewer
    [DesignTimeVisible(false)]
    public partial class StockDetailPage : ContentPage
    {
        StockDetailViewModel viewModel;
        private CancellationTokenSource cancellation;
        public StockDetailPage(StockDetailViewModel viewModel)
        {
            LoadContentDetailsPage(viewModel);
            this.cancellation = new CancellationTokenSource();

            //Device.StartTimer(TimeSpan.FromSeconds(2), () =>
            //{
            //    Device.BeginInvokeOnMainThread(() => LoadContentDetailsPage(viewModel));
            //    return true;
            //});

        }

        public StockDetailPage()
        {
            InitializeComponent();

            var stock = new Stock
            {
                Code = "Ação 1"

            };

            viewModel = new StockDetailViewModel(stock);
            BindingContext = viewModel;
        }

        private void LoadContentDetailsPage(StockDetailViewModel viewModel)
        {
            InitializeComponent();

            BindingContext = this.viewModel = viewModel;
        }

        public void StartAutoUpload()
        {
            CancellationTokenSource cts = this.cancellation; // safe copy
            Device.StartTimer(TimeSpan.FromSeconds(2), () => {
                if (cts.IsCancellationRequested) return false;
                Device.BeginInvokeOnMainThread(() => LoadContentDetailsPage(viewModel));
                return true;
            });
        }

        public void StopAutoUpload()
        {
            Interlocked.Exchange(ref this.cancellation, new CancellationTokenSource()).Cancel();
        }

        protected override void OnAppearing()
        {
            base.OnAppearing();

            this.StartAutoUpload();
        }

        protected override void OnDisappearing()
        {
            base.OnDisappearing();

            this.StopAutoUpload();
        }
    }
}