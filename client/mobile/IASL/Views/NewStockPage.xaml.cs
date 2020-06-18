using System;
using System.Collections.Generic;
using System.ComponentModel;
using Xamarin.Forms;
using Xamarin.Forms.Xaml;

using IASL.Models;
using System.Linq;

namespace IASL.Views
{
    // Learn more about making custom code visible in the Xamarin.Forms previewer
    // by visiting https://aka.ms/xamarinforms-previewer
    [DesignTimeVisible(false)]
    public partial class NewStockPage : ContentPage
    {
        public Stock Stock { get; set; }

        public NewStockPage()
        {
            InitializeComponent();

            Stock = new Stock
            {
                Code = "",
            };

            BindingContext = this;
        }

        async void Save_Clicked(object sender, EventArgs e)
        {
            var mockDataStore = new IASL.Services.MockDataStore();
            var stock = mockDataStore.GetStockAsyncByCode(Stock.Code).Result;
            if (stock != null)
            {
                MessagingCenter.Send(this, "AddStock", stock);
                await Navigation.PopModalAsync();
            }
            else
            {
                ErrorLabel.Text = "O ativo não foi encontrado, por favor certifique-se de colocar um código válido";
            }
        }

        async void Cancel_Clicked(object sender, EventArgs e)
        {
            await Navigation.PopModalAsync();
        }
    }
}