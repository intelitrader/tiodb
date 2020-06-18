using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace IASL.Services
{
    public interface IDataStore<T>
    {
        Task<bool> AddStockAsync(T item);
        Task<bool> UpdateStockAsync(T item);
        Task<bool> DeleteStockAsync(string id);
        Task<T> GetStockAsync(string id);
        Task<IEnumerable<T>> GetStocksAsync(bool forceRefresh = false);

        Task<IEnumerable<T>> RandomizeStocksAsync(IEnumerable<T> items, bool forceRefresh = false);
    }
}
