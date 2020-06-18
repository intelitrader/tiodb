using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Runtime.Serialization.Json;
using System.Threading;
using System.Threading.Tasks;
using IASL.Models;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace IASL.Services
{
    public class MockDataStore : IDataStore<Stock>
    {
        readonly List<Stock> stocks;
        HttpClient client = new HttpClient();

        private List<Stock> possibleStocks = new List<Stock>()
            {
                new Stock { Id = Guid.NewGuid().ToString(), Code = "ABEV3", Price=14.32m, OpeningPrice=1m, AveragePrice=2m, MinPrice=3m, MaxPrice=4m },
                new Stock { Id = Guid.NewGuid().ToString(), Code = "AZUL4", Price=19.08m, OpeningPrice=10m, AveragePrice=20m, MinPrice=30m, MaxPrice=40m },
                new Stock { Id = Guid.NewGuid().ToString(), Code = "BTOW3", Price=86.03m, OpeningPrice=100m, AveragePrice=200m, MinPrice=300m, MaxPrice=400m },
                new Stock { Id = Guid.NewGuid().ToString(), Code = "B3SA3", Price=49.58m, OpeningPrice=1000m, AveragePrice=2000m, MinPrice=3000m, MaxPrice=4000m },
                new Stock { Id = Guid.NewGuid().ToString(), Code = "BBSE3", Price=28.05m, OpeningPrice=10000m, AveragePrice=20000m, MinPrice=30000m, MaxPrice=40000m },
            };
        public MockDataStore()
        {
            stocks = new List<Stock>();
        }

        public async Task<IEnumerable<Stock>> RandomizeStocksAsync(IEnumerable<Stock> stocks, bool forceRefresh = false)
        {
            var r = Convert.ToDecimal((new Random().NextDouble()*0.2)+0.9);
            foreach(Stock stock in stocks)
            {
                stock.Price = Math.Round(stock.Price*r, 2);
                stock.OpeningPrice = Math.Round(stock.OpeningPrice * r, 2);
                stock.AveragePrice = Math.Round(stock.AveragePrice * r, 2);
                stock.MinPrice = Math.Round(stock.MinPrice * r, 2);
                stock.MaxPrice = Math.Round(stock.MaxPrice * r, 2);
            }
            return await Task.FromResult(stocks);
        }
        public async Task<bool> AddStockAsync(Stock stock)
        {
            stocks.Add(stock);

            return await Task.FromResult(true);
        }

        public async Task<bool> UpdateStockAsync(Stock stock)
        {
            var oldStock = stocks.Where((Stock arg) => arg.Id == stock.Id).FirstOrDefault();
            stocks.Remove(oldStock);
            stocks.Add(stock);

            return await Task.FromResult(true);
        }

        public async Task<bool> DeleteStockAsync(string id)
        {
            var oldStock = stocks.Where((Stock arg) => arg.Id == id).FirstOrDefault();
            stocks.Remove(oldStock);

            return await Task.FromResult(true);
        }

        public async Task<Stock> GetStockAsync(string id)
        {
            return await Task.FromResult(stocks.FirstOrDefault(s => s.Id == id));
        }
        public async Task<Stock> GetStockAsyncByCode(string code)
        {
            //const string URL = "http://demo.intelitrader.com.br:8081/iwg/snapshot";
            //string urlParameters = "?t=webgateway&c=666&petr4,1";

            //client.BaseAddress = new Uri(URL);

            //client.DefaultRequestHeaders.Accept.Add(
            //new MediaTypeWithQualityHeaderValue("application/json"));

            //HttpResponseMessage response = client.GetAsync(urlParameters).Result;  // Blocking call! Program will wait here until a response is received or a timeout occurs.
            //if (response.IsSuccessStatusCode)
            //{
            //    // Parse the response body.
            //    var dataObjects = response.Content;  //Make sure to add a reference to System.Net.Http.Formatting.dll
            //    return new Stock();
            //    //foreach (var d in dataObjects)
            //    //{
            //    //    Console.WriteLine("{0}", d.Name);
            //    //}
            //}

            var requisicaoWeb = WebRequest.CreateHttp("http://demo.intelitrader.com.br:8081/iwg/snapshot?t=webgateway&c=666&q=petr4,1");
            requisicaoWeb.Method = "GET";
            requisicaoWeb.UserAgent = "RequisicaoWebDemo";


            using (var resposta = requisicaoWeb.GetResponse())
            {
                var streamDados = resposta.GetResponseStream();
                StreamReader reader = new StreamReader(streamDados);
                object objResponse = reader.ReadToEnd();
                var post = JsonConvert.DeserializeObject(objResponse.ToString());
                JObject json = JObject.Parse(objResponse.ToString());
                var code2 = json.SelectToken("Value")[0].Value<string>("S");
                var price2 = json.SelectToken("Value")[0].SelectToken("Ps").SelectToken("AP").Value<decimal>();
                Console.WriteLine(objResponse.ToString());
                Console.ReadLine();
                streamDados.Close();
                resposta.Close();
            }

            //Uri uri = new Uri("http://demo.intelitrader.com.br:8081/iwg/snapshot?t=webgateway&c=666&q=petr4,1");
            //HttpResponseMessage response = await client.GetAsync(uri);
            //if (response.IsSuccessStatusCode)
            //{
            //    string content = await response.Content.ReadAsStringAsync();
            //    var stocks = JsonConvert.DeserializeObject<List<Stock>>(content);
            //}

            return await Task.FromResult(possibleStocks.SingleOrDefault(s => s.Code == code));
        }

        public async Task<IEnumerable<Stock>> GetStocksAsync(bool forceRefresh = false)
        {
            var ret = client.GetAsync("http://demo.intelitrader.com.br:8081/iwg/snapshot?t=webgateway&c=666&q=petr4,1");

            return await Task.FromResult(stocks);
        }

        public async Task<IEnumerable<Stock>> GetStocksAsyncById(IEnumerable<string> ids, bool forceRefresh = false)
        {
            return await Task.FromResult(stocks.Where(d => ids.Contains(d.Id)));
        }
    }
}


public class Post
{
    public string _meta { get; set; }
    public string Value { get; set; }
}