#include "pch.h"
#include "../../client/c/tioclient.h"
#include "../../client/cpp/tioclient.hpp"
#include "../../client/c/tioclient_internals.h"
#include <iostream>     // std::cout
#include <sstream>      // std::stringstream
#include <random>
#include <iomanip>
#include <map>
#include <algorithm>
#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof(array[0]))
#endif

using std::thread;
using std::function;
using std::vector;
using std::string;
using std::pair;
using std::to_string;
using std::shared_ptr;
using std::make_shared;
using std::unordered_map;
using std::accumulate;
using std::atomic;

using std::cout;
using std::endl;

using std::chrono::high_resolution_clock;
using std::chrono::duration_cast;
using std::chrono::milliseconds;

static int assertiveEventCount = 0;

class TioTestRunner
{
  vector<function<void(void)>> tests_;
  bool running_;

  vector<thread> threads_;
  vector<string> exceptions_;
  atomic<unsigned> finishedThreads_;

public:

  TioTestRunner()
    : running_(false)
  {
    reset();
  }

  void reset()
  {
    tests_.clear();
    running_ = false;
    finishedThreads_ = 0;
    exceptions_.clear();
  }

  void add_test(function<void(void)> f)
  {
    tests_.push_back(f);
  }

  void join()
  {
    for (auto& t : threads_)
      t.join();
  }

  void run()
  {
    start();
    join();
  }

  operator bool()
  {
    return exceptions_.size() == 0;
  }

  vector<string> exceptions()
  {
    return exceptions_;
  }

  bool finished() const
  {
    return finishedThreads_ == threads_.size();
  }

  void start()
  {
    for (auto f : tests_)
    {
      threads_.emplace_back(
        thread(
          [f, this]()
          {
            while (!running_)
              std::this_thread::yield();

            try
            {
              f();
            }
            catch (std::exception& e)
            {
              exceptions_.push_back(e.what());
            }

            finishedThreads_++;
          }
      ));
    }

    running_ = true;
  }
};

string get_seq_value(string container_name, int operation)
{
  std::stringstream ss;
  ss << container_name << ";" << operation << ";" << "_01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901";
  string val = ss.str();
  val = val.substr(0, 48);
  return val;
}

int vector_perf_test_c(TIO_CONNECTION* cn, TIO_CONTAINER* container, unsigned operations, bool networkBatch)
{
  TIO_DATA v;

  tiodata_init(&v);

  if (networkBatch)
    tio_begin_network_batch(cn);

  for (unsigned a = 0; a < operations; ++a)
  {
    string val = get_seq_value(container->name, a);

    tiodata_set_string_and_size(&v, val.c_str(), (unsigned int)val.size());

    tio_container_push_back(container, NULL, &v, NULL);
  }

  if (networkBatch)
    tio_finish_network_batch(cn);
  tiodata_free(&v);
  return 0;
}

int map_perf_test_c(TIO_CONNECTION* cn, TIO_CONTAINER* container, unsigned operations, bool networkBatch)
{
  TIO_DATA k, v;

  tiodata_init(&k);
  tiodata_init(&v);

  if (networkBatch)
    tio_begin_network_batch(cn);

  string prefix = to_string(reinterpret_cast<uint64_t>(cn)) + "_";

  for (unsigned a = 0; a < operations; ++a)
  {
    char buffer[16];
    sprintf(buffer, "%d", a);
    string keyAsString = prefix + buffer;
    tiodata_set_string_and_size(&k, keyAsString.c_str(), (unsigned int)keyAsString.size());

    tiodata_set_int(&v, a);

    tio_container_set(container, &k, &v, NULL);
  }

  if (networkBatch)
    tio_finish_network_batch(cn);

  tiodata_free(&k);
  tiodata_free(&v);

  return 0;
}


typedef int(*PERF_FUNCTION_C)(TIO_CONNECTION*, TIO_CONTAINER*, unsigned int, bool);


int measure(TIO_CONNECTION* cn, TIO_CONTAINER* container, unsigned test_count,
  PERF_FUNCTION_C perf_function, unsigned* persec, bool networkBatch)
{
  int ret;

  auto start = std::chrono::high_resolution_clock::now();

  ret = perf_function(cn, container, test_count, networkBatch);
  if (TIO_FAILED(ret)) return ret;

  auto delta = std::chrono::high_resolution_clock::now() - start;
  if (std::chrono::duration_cast<std::chrono::milliseconds>(delta).count() == 0)
      *persec = (unsigned)(test_count * 1000);
  else
      *persec = (unsigned)((test_count * 1000) / std::chrono::duration_cast<std::chrono::milliseconds>(delta).count());

  return ret;
}

struct ContainerData
{
  string name;
  string type;
  ContainerData(string p_name, string p_type)
  {
    name = p_name;
    type = p_type;
  }
};
class TioTesterSubscriber
{
  ContainerData container_name_;
  string host_name_;
  thread thread_;
  bool should_stop_;
  uint64_t item_count_;
  uint64_t client_count_;
  std::map<std::string, uint64_t> eventCount_;
  bool run_sequence_test = true;
public:

  TioTesterSubscriber(const string& host_name,
    const string& container_name,
    const string& container_type,
    uint64_t item_count,
    uint64_t client_count)
    : host_name_(host_name)
    , should_stop_(false)
    , container_name_(container_name, container_type)
    , item_count_(item_count)
    , client_count_(client_count)
  {
  }

  TioTesterSubscriber(const TioTesterSubscriber&) = delete;
  TioTesterSubscriber& operator = (const TioTesterSubscriber&) = delete;
  TioTesterSubscriber(TioTesterSubscriber&& rhv) = delete;

  ~TioTesterSubscriber()
  {
    assert(!thread_.joinable());
  }

  void set_containers(decltype(container_name_)& values)
  {
    container_name_ = values;
  }

  void start()
  {
    should_stop_ = false;

    thread_ = std::move(
      thread([&]()
        {
          tio::Connection connection(host_name_);
          tio::containers::list<string> container;

            container.create(
              &connection,
              container_name_.name,
              container_name_.type);

            int startPosition = 0;

            auto eventHandler = [this](const string& containerName, const string& eventName, const int& key, const string& value)
            {
                ++this->eventCount_[eventName];
            };

            container.subscribe(eventHandler, &startPosition);

            do
            {
              unsigned timeout = 1;
              connection.WaitForNextEventAndDispatch(&timeout);
              should_stop_ = eventCount_["clear"] == client_count_;
            }
            while ( ! should_stop_ );

            if (eventCount_["push_back"] != item_count_ * client_count_)
            {
                std::cerr << "\tNumber of events isn't the same as expected\n";
                std::cerr << "\tEvent Count: " << eventCount_["push_back"];
                std::cerr << "\tExpected: " << item_count_ * client_count_ << endl;
            }

            connection.Disconnect();

        }));
  }

  void clear()
  {
    thread t;
    thread_.swap(t);
  }

  void stop()
  {
    if (!thread_.joinable())
      return;
  }

  void join()
  {
    if (!thread_.joinable())
      return;

    thread_.join();

  }

};


class TioStressTest
{
  string host_name_;
  string container_name_;
  string container_type_;
  PERF_FUNCTION_C perf_function_;
  unsigned test_count_;
  unsigned* persec_;
  bool networkBatch_;
public:

  TioStressTest(
    const string& host_name,
    const string& container_name,
    const string& container_type,
    PERF_FUNCTION_C perf_function,
    unsigned test_count,
    unsigned* persec,
    bool networkBatch)
    : host_name_(host_name)
    , container_name_(container_name)
    , container_type_(container_type)
    , perf_function_(perf_function)
    , test_count_(test_count)
    , persec_(persec)
    , networkBatch_(networkBatch)
  {

  }

  void operator()()
  {
    tio::Connection connection(host_name_);
    tio::containers::list<string> container;
    container.create(&connection, container_name_, container_type_);

    measure(connection.cnptr(), container.handle(), test_count_, perf_function_, persec_, networkBatch_);

    container.clear();
  }
};


struct OrderBookEntry
{
  string orderid;
  string broker;

  double price;
  unsigned qty;
  string date;
  string time;
  unsigned position;
};

struct TradeEntry
{
  string buyer;
  string seller;
  unsigned tradeId;
  double price;
  unsigned qty;
  double netchange;
  string date;
  string time;
};

template<typename T>
std::string X1Serialize(T begin, T end)
{
  std::stringstream header, values;

  // message identification
  header << "X1";

  // field count
  header << std::setfill('0') << std::setw(4) << std::hex << (end - begin) << "C";

  for (T i = begin; i != end; ++i)
  {
    unsigned int currentStreamSize = static_cast<unsigned int>(values.str().size());
    size_t size = 0;
    char dataTypeCode = 'S'; // always string

    values << *i;
    size = values.str().size() - currentStreamSize;
    values << " ";

    header << std::setfill('0') << std::setw(4) << std::hex << size << dataTypeCode;
  }

  header << " " << values.str();

  return header.str();
}

void ToTioData(const OrderBookEntry& entry, TIO_DATA* tiodata)
{
  string fields[] = {
    entry.orderid,
    entry.broker,
    to_string(entry.price),
    to_string(entry.qty),
    entry.date,
    entry.time,
  };

  string serialized = X1Serialize(fields, fields + _countof(fields));
  tiodata_set_string_and_size(tiodata, serialized.c_str(), static_cast<unsigned int>(serialized.length()));
}

void ToTioData(const TradeEntry& entry, TIO_DATA* tiodata)
{
  string fields[] = {
    entry.buyer,
    entry.seller,
    to_string(entry.tradeId),
    to_string(entry.price),
    to_string(entry.qty),
    to_string(entry.netchange),
    entry.date,
    entry.time,
  };

  string serialized = X1Serialize(fields, fields + _countof(fields));
  tiodata_set_string_and_size(tiodata, serialized.c_str(), static_cast<unsigned int>(serialized.length()));
}

void FromTioData(const TIO_DATA* tiodata, OrderBookEntry* value)
{}

void FromTioData(const TIO_DATA* tiodata, TradeEntry* value)
{}

inline unsigned GenerateRandomNumber(unsigned start, unsigned end) noexcept
{
  thread_local static std::mt19937 rg{ std::random_device{}() };
  std::uniform_int_distribution<unsigned> dr(start, end);

  return dr(rg);
}

inline std::string GenerateRandomString(std::string::size_type length = 10) noexcept
{
  static auto& chrs = "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  thread_local static std::mt19937 rg{ std::random_device{}() };
  std::uniform_int_distribution<std::string::size_type> pick(0, sizeof(chrs) - 2);

  std::string s;

  s.reserve(length);

  while (length--)
    s += chrs[pick(rg)];

  return s;
}

template<typename V, typename R>
inline std::ostream& operator<<(std::ostream& os, std::chrono::duration<V, R> const& d)
{
  auto hours = std::chrono::duration_cast<std::chrono::hours>(d);
  auto minutes = std::chrono::duration_cast<std::chrono::minutes>(d % std::chrono::hours(1));
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(d % std::chrono::minutes(1));
  auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(d % std::chrono::seconds(1));
  auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(d % std::chrono::milliseconds(1));

  os << std::setfill('0')
    << std::setw(2)
    << hours.count()
    << ":"
    << std::setw(2)
    << minutes.count()
    << ":"
    << std::setw(2)
    << seconds.count()
    << ":"
    << std::setw(3)
    << milliseconds.count()
    << ":"
    << std::setw(6)
    << microseconds.count()
    << "\n";

  return os;
}


struct FeederChannelConfig
{
  unsigned TotalOperations{};
  bool UseNetworkBatch{};
  std::string Symbol{};
  std::string Hostname{};
  short Port{};
};


class Instrument
{
  tio::Connection* connection_;
  tio::containers::list<OrderBookEntry> book_buy_container_;
  tio::containers::list<OrderBookEntry> book_sell_container_;
  tio::containers::list<TradeEntry> trade_container_;
  tio::containers::map<std::string, std::string> properties_container_;
  std::map<unsigned, OrderBookEntry> local_buy_book_;
  std::map<unsigned, OrderBookEntry> local_sell_book_;
  string containerPrefix_;
  FeederChannelConfig config_;
  unsigned nextBookPosition_;
  unsigned nextTradeId_;
public:
  explicit Instrument(
    tio::Connection* connection,
    FeederChannelConfig const& config) noexcept
    : connection_(connection)
    , containerPrefix_("intelimarket/bvmf/")
    , config_(config)
    , nextBookPosition_(0)
    , nextTradeId_(0)
  {}

  Instrument(Instrument const& rhs) noexcept
    : connection_(rhs.connection_)
    , containerPrefix_(rhs.containerPrefix_)
    , config_(rhs.config_)
    , nextBookPosition_(0)
    , nextTradeId_(0)
  {}

  enum MDEntryType
  {
    Bid = 0,
    Ask = 1,
    Trade = 3,
    Price = 4
  };

  enum BookAction
  {
    New = 0,
    Update = 1
  };

  void RunByMdEntryType()
  {
    for (unsigned i = 0; i < config_.TotalOperations; ++i)
    {
      auto entryType = static_cast<MDEntryType>(GenerateRandomNumber(0, 2));

      OnOrderBookChange(entryType);

      if( i % 4 == 0 )
      {
        OnTrade();
        OnPrice();
      }
    }
  }

  void Initialize()
  {
    book_buy_container_.create(connection_, containerPrefix_ + config_.Symbol + "/book_buy", "volatile_list");
    book_buy_container_.clear();

    book_sell_container_.create(connection_, containerPrefix_ + config_.Symbol + "/book_sell", "volatile_list");
    book_sell_container_.clear();

    trade_container_.create(connection_, containerPrefix_ + config_.Symbol + "/trades", "volatile_list");
    trade_container_.clear();

    properties_container_.create(connection_, containerPrefix_ + config_.Symbol + "/properties", "volatile_map");
    properties_container_.clear();

    properties_container_.set("BidSize", "0");
    properties_container_.set("BidPrice", "0");
    properties_container_.set("Price", "0");
  }
private:
  void OnOrderBookChange(MDEntryType entryType)
  {
    auto& book_container_ = entryType == MDEntryType::Ask ? 
      book_buy_container_: book_sell_container_;
    auto& local_book_ = entryType == MDEntryType::Ask ? 
      local_buy_book_: local_sell_book_;

    {
      // new
      OrderBookEntry entry;

      entry.orderid = GenerateRandomString(7);
      entry.broker = "127";
      entry.price = 96923;
      entry.qty = 2;
      entry.position = nextBookPosition_;

      book_container_.push_back(entry);
      local_book_[entry.position] = entry;
      nextBookPosition_++;
    }
    {
      // update
      if (local_book_.empty())
        return;

      std::unique_ptr<tio::Connection::TioScopedNetworkBatch> networkBatch;
      if (config_.UseNetworkBatch)
        networkBatch = std::make_unique<tio::Connection::TioScopedNetworkBatch>(*connection_);

      auto bookIndex = GenerateRandomNumber(0, static_cast<unsigned int>(local_book_.size()) - 1);
      OrderBookEntry& entry = local_book_[bookIndex];

      entry.price = GenerateRandomNumber(9600, 9700);
      entry.qty = 1;

      book_container_.set(bookIndex, entry);
    }
  }

  void OnTrade()
  {
    std::unique_ptr<tio::Connection::TioScopedNetworkBatch> networkBatch;
    if (config_.UseNetworkBatch)
      networkBatch = std::make_unique<tio::Connection::TioScopedNetworkBatch>(*connection_);

    TradeEntry entry;

    entry.buyer = GenerateRandomNumber(1, 200);
    entry.seller = GenerateRandomNumber(1, 200);
    entry.price = GenerateRandomNumber(9600, 9700);
    entry.qty = GenerateRandomNumber(1, 15);
    entry.date = "20200918";
    entry.time = "174629041";
    entry.tradeId = nextTradeId_;

    trade_container_.push_back(entry);

    nextTradeId_++;
  }

  void OnPrice()
  {
    std::unique_ptr<tio::Connection::TioScopedNetworkBatch> networkBatch;
    if (config_.UseNetworkBatch)
      networkBatch = std::make_unique<tio::Connection::TioScopedNetworkBatch>(*connection_);

    unsigned price = GenerateRandomNumber(9600, 9700);;
    properties_container_.set("Price", to_string(price));
  }
};

class FeederChannel
{
  tio::Connection connection_;
  FeederChannelConfig config_;
  std::unique_ptr<Instrument> instrument_;
public:
  explicit FeederChannel(FeederChannelConfig const& config)
    : connection_(config.Hostname, config.Port)
    , config_(config)
  {
    instrument_ = std::make_unique<Instrument>(&connection_, config_);
    instrument_->Initialize();
  }

  void Run()
  {
    instrument_->RunByMdEntryType();
  }

  std::string symbol() const
  {
    return config_.Symbol;
  }
};

class FeederClient
{
  tio::Connection connection_;
  std::string symbol_;
  tio::containers::list<OrderBookEntry> book_buy_container_;
  tio::containers::list<OrderBookEntry> book_sell_container_;
  tio::containers::list<TradeEntry> trade_container_;
  tio::containers::map<std::string, std::string> properties_container_;
  unsigned bookEventCount_;
  unsigned tradeEventCount_;
  unsigned priceEventCount_;
  string containerPrefix_;
  bool shouldStop_;
public:
  explicit FeederClient(std::string const& hostname, short port, std::string const& symbol)
    : connection_(hostname, port)
    , symbol_(symbol)
    , bookEventCount_(0)
    , tradeEventCount_(0)
    , priceEventCount_(0)
    , containerPrefix_("intelimarket/bvmf/")
    , shouldStop_(false)
  {
    book_buy_container_.open(&connection_, containerPrefix_ + symbol_ + "/book_buy");
    book_sell_container_.open(&connection_, containerPrefix_ + symbol_ + "/book_sell");
    trade_container_.open(&connection_, containerPrefix_ + symbol_ + "/trades");
    properties_container_.open(&connection_, containerPrefix_ + symbol_ + "/properties");
  }

  void SubscribeAll()
  {
    SubscribeBook();
    SubscribeTrades();
    SubscribePrice();

    while (!shouldStop_)
    {
      unsigned timeout = 1;
      connection_.WaitForNextEventAndDispatch(&timeout);
    }
  }

  void Stop()
  {
    shouldStop_ = true;
  }

  unsigned TotalEventCount() const
  {
    return bookEventCount_ + tradeEventCount_ + priceEventCount_;
  }
private:
  void SubscribeBook()
  {
    auto eventHandler = [this](const string& containerName, const string& eventName, const int& key, const OrderBookEntry& value)
    {
      if (eventName == "snapshot_end")
        return;

      ++bookEventCount_;
    };

    int startPosition = 0;
    book_buy_container_.subscribe(eventHandler, &startPosition);
    book_sell_container_.subscribe(eventHandler, &startPosition);
  }

  void SubscribeTrades()
  {
    auto eventHandler = [this](const string& containerName, const string& eventName, const int& key, const TradeEntry& value)
    {
      if (eventName == "snapshot_end")
        return;

      ++tradeEventCount_;
    };

    int startPosition = 0;
    trade_container_.subscribe(eventHandler, &startPosition);
  }

  void SubscribePrice()
  {
    auto eventHandler = [this](const string& containerName, const string& eventName, const string& key, const string& value)
    {
      if (eventName == "snapshot_end")
        return;

      ++priceEventCount_;
    };

    int startPosition = 0;
    properties_container_.subscribe(eventHandler, &startPosition);
  }
};

namespace bp = boost::process;

class Process
{
  boost::asio::io_context ios_;

  bp::ipstream out_;
  std::stringstream ss_;
  std::thread reader_thread_;

  bp::pipe in_;

  bp::child p_;
  int exit_code_ = 0;
  bp::pid_t pid_ = 0;
public:
  explicit Process(string exec, std::vector<std::string> args)
    : p_(exec, args,
      bp::std_out > out_,
      bp::std_in < in_,
      bp::on_exit([&](int exit, std::error_code) { exit_code_ = exit; }),
      ios_)
  {
    pid_ = p_.id();

    reader_thread_ = std::thread([&]() {
      std::string line;
      while (std::getline(out_, line)) {
        ss_.str(line);
      }
    });
  }

  ~Process() {
    Stop();
  }

  void SendInput(std::string input)
  {
    in_.write(input.c_str(), input.size());
    in_.close();
  }

  void Stop()
  {
    ios_.run();
    if (p_.running()) p_.wait();
    if (reader_thread_.joinable()) reader_thread_.join();
  }

  string GetOutput()
  {
    if (reader_thread_.joinable()) reader_thread_.join();

    return ss_.str();
  }
};

class UmdfFeederStressTest
{
  std::vector<std::shared_ptr<FeederChannelConfig>> feeders_;
  std::vector<std::shared_ptr<FeederClient>> clients_;
  std::list<std::shared_ptr<Process>> feedersProcesses_;
  std::list<std::shared_ptr<Process>> clientsProcesses;
  std::vector<std::string> symbols_;
  std::uint64_t feedersTotalOperations_;
  std::uint64_t clientsTotalOperations_;
  std::string hostname_;
  short port_;
  unsigned feedersSize_;
  unsigned clientsSize_;
  bool useNetworkBatch_;
  unsigned totalOperations_;
  string execPath_;
public:
  explicit UmdfFeederStressTest(
    std::string const& hostname, short port, unsigned feedersSize, unsigned clientsSize, bool useNetworkBatch, unsigned totalOperations, string execPath) noexcept
    : feedersTotalOperations_(0)
    , clientsTotalOperations_(0)
    , hostname_(hostname)
    , port_(port)
    , feedersSize_(feedersSize)
    , clientsSize_(clientsSize)
    , useNetworkBatch_(useNetworkBatch)
    , totalOperations_(totalOperations)
    , execPath_(execPath)
  {}

  void InitializeChannels()
  {
    for (unsigned i = 0; i < feedersSize_; ++i)
    {
      auto config = std::make_shared<FeederChannelConfig>();
      config->TotalOperations = totalOperations_;
      config->Hostname = hostname_;
      config->Port = port_;
      config->UseNetworkBatch = useNetworkBatch_;
      config->Symbol = GenerateRandomString(6);

      symbols_.push_back(config->Symbol);

      feedersTotalOperations_ += config->TotalOperations;

      // create the containers
      FeederChannel channel(*config);

      feeders_.push_back(config);
    }
  }

  void Run()
  {
    auto start = std::chrono::high_resolution_clock::now();

    std::cout << "Total Feeders: " << feeders_.size() << "\n";
    std::cout << "Symbols:" << ([&](){ string ret; for( auto f: feeders_ ) ret += " " + f->Symbol; return ret; })() << "\n";
    std::cout << "Total Clients: " << clientsSize_ << "\n";
    std::cout << "Total Feeders Operations: " << feedersTotalOperations_ << "\n";
    std::cout << "NetworkBatch: " << std::boolalpha << useNetworkBatch_ << "\n";

    StartClientsProcesses();

    StartChannelsProcesses();

    StopChannelsProcesses();

    StopClientsProcesses();

    auto delta = std::chrono::high_resolution_clock::now() - start;
    auto feedersOperationsPersec = (feedersTotalOperations_ * 1000) / std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
    auto clientsOperationsPersec = (clientsTotalOperations_ * 1000) / std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();

    std::cout << "Time elapsed: " << delta;
    std::cout << "Feeders total operations per second: " << feedersOperationsPersec << "\n";
    std::cout << "Clients total events received per second: " << clientsOperationsPersec << "\n";
  }

private:
  void StartChannelsProcesses() {
    for (auto& feeder : feeders_)
    {
      std::vector<std::string> args{
        "--run-feeder-channel-process",
        "--umdf-feeder-process-symbol", feeder->Symbol,
        "--umdf-feeder-test-total-operations", to_string(feeder->TotalOperations),
        "--host", feeder->Hostname,
        "--port", to_string(feeder->Port)
      };
      if (!feeder->UseNetworkBatch) {
        args.push_back("--disable-network-batch");
      }

      auto p = std::make_shared<Process>(execPath_, args);

      feedersProcesses_.push_back(p);
    }
  }

  void StopChannelsProcesses() {
    for (auto& p : feedersProcesses_) {
      p->Stop();
    }
  }

  void StartClientsProcesses() {
    for (unsigned i = 0; i < clientsSize_; ++i)
    {
      std::string symbol = symbols_.size() >= clientsSize_ ?
        symbols_[i]
        : symbols_[GenerateRandomNumber(0, std::max(static_cast<unsigned int>(symbols_.size())-1
            ,static_cast < unsigned int>(0)))];
      assert(!symbol.empty());

      std::vector<std::string> args{
        "--run-feeder-client-process",
        "--umdf-feeder-process-symbol", symbol,
        "--host", hostname_,
        "--port", to_string(port_)
      };
      auto p = std::make_shared<Process>(execPath_, args);

      clientsProcesses.push_back(p);
    }
  }

  void StopClientsProcesses() {
    for (auto& p : clientsProcesses) {
      p->SendInput("\n");
      p->Stop();
    }

    for (auto& p : clientsProcesses) {
      string output = p->GetOutput();

      auto configData = ParseProcessOutput(output);

      //std::cout << "FEEDER CLIENT OUTPUT: " << output << "\n";
      clientsTotalOperations_ += std::stoi(configData["operations"]);
    }
  }

  std::unordered_map<std::string, std::string> ParseProcessOutput(std::string input) {
    // Here we will store the resulting config data
    std::unordered_map<std::string, std::string> configData;

    std::stringstream ss(input);

    while (ss.good())
    {
      string substr;
      getline(ss, substr, '|');
      // If the line contains a colon, we treat it as valid data
      if (substr.find(':') != std::string::npos) {
        // Split data in line into an id and a value part and save it
        std::istringstream iss{ substr };
        std::string id{}, value{};
        if (std::getline(std::getline(iss, id, ':') >> std::ws, value)) {
          // Add config data to our map
          configData[id] = value;
        }
      }
    }
    return configData;
  }
};

string generate_container_name()
{
  static unsigned seq = 0;
  static string prefix = "_test_" + to_string(std::chrono::steady_clock::now().time_since_epoch().count());

  return prefix + "_" + to_string(++seq) + "_";
}

void TEST_deadlock_on_disconnect(const char* hostname)
{
  static const unsigned PUBLISHER_COUNT = 60;
  static const unsigned ITEM_COUNT = 2 * 1000;

  const string container_type("volatile_list");

  cout << "START: deadlock test" << endl;

  tio::Connection subscriberConnection(hostname);

  vector<tio::containers::list<string>> containers(PUBLISHER_COUNT);
  vector<unsigned> persec(PUBLISHER_COUNT);

  for (unsigned a = 0; a < PUBLISHER_COUNT; a++)
  {
    containers[a].create(&subscriberConnection, generate_container_name(), container_type);
    containers[a].subscribe(std::bind([]() {}));
  }

  TioTestRunner runner;

  for (unsigned a = 0; a < PUBLISHER_COUNT; a++)
  {
    runner.add_test(
      TioStressTest(
        hostname,
        containers[a].name(),
        container_type,
        &vector_perf_test_c,
        ITEM_COUNT,
        &persec[a],
        true)
    );
  }

  runner.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  subscriberConnection.Disconnect();

  unsigned publicationCount = 0;

  for (int a = 0;; a++)
  {
    if (runner.finished())
      break;


    cout << "creating new subscribers... ";

    tio::Connection newConnection(hostname);
    vector<tio::containers::list<string>> newContainers(PUBLISHER_COUNT);

    for (unsigned a = 0; a < PUBLISHER_COUNT; a++)
    {
      newContainers[a].open(&newConnection, containers[a].name());
      newContainers[a].subscribe(std::bind([&]() { publicationCount++; }));
    }

    cout << "done." << endl;

    for (int b = 0; b < 1; b++)
    {
      unsigned timeout = 1;
      newConnection.WaitForNextEventAndDispatch(&timeout);
    }

    newConnection.Disconnect();

    cout << "not yet. Publication count = " << publicationCount << (a > 10 ? " PROBABLY DEADLOCK" : "") << endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  cout << "no deadlock" << endl;

  runner.join();
}


void TEST_connection_stress(
  const char* hostname,
  unsigned connection_count)
{
  cout << "START: connection stress test, " << connection_count << " connections" << endl;
  vector<shared_ptr<tio::Connection>> connections;
  connections.reserve(connection_count);
  unsigned log_step = 1000;

  auto start = std::chrono::high_resolution_clock::now();

  for (unsigned a = 0; a < connection_count; a++)
  {
    connections.push_back(make_shared<tio::Connection>(hostname));

    if (a % log_step == 0)
      cout << a << "  connections" << endl;
  }

  cout << "disconnecting..." << endl;

  connections.clear();

  auto delta = std::chrono::high_resolution_clock::now() - start;

  cout << "FINISHED: connection stress test, " << std::chrono::duration_cast<std::chrono::milliseconds>(delta).count() << "ms" << endl;
}


void TEST_container_concurrency(
  const char* hostname,
  unsigned max_client_count,
  unsigned item_count,
  const char* container_type,
  PERF_FUNCTION_C perf_function,
  bool networkBatch
)
{
  cout << "START: container concurrency test, type=" << container_type << endl;

  for (unsigned client_count = 1; client_count <= max_client_count; client_count *= 2)
  {
    TioTestRunner testRunner;

    unsigned items_per_thread = item_count / client_count;

    cout << client_count << " clients, " << items_per_thread << " items per thread... ";

    auto start = high_resolution_clock::now();

    string container_name = generate_container_name();

    for (unsigned a = 0; a < client_count; a++)
    {
      testRunner.add_test(
        [=]()
        {
          tio::Connection connection(hostname);
          tio::containers::list<string> c;

          c.create(&connection, container_name, container_type);

          perf_function(connection.cnptr(), c.handle(), items_per_thread, networkBatch);
        }
      );
    }

    testRunner.run();

    auto delta_ms = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
    auto persec = item_count / delta_ms;

    tio::Connection connection(hostname);
    tio::containers::list<string> c;

    c.open(&connection, container_name);

    const char* errorMessage = (c.size() 
      != (static_cast<unsigned long>(items_per_thread) * client_count)) ?
      " LOST RECORDS!" : "";

    cout << delta_ms << "ms (" << persec << "k/s)" << errorMessage << endl;
  }

  cout << "FINISH: container concurrency test" << endl;
}


void TEST_create_lots_of_containers(const char* hostname,
  unsigned container_count,
  unsigned client_count,
  unsigned item_count,
  bool networkBatch)
{
  tio::Connection connection(hostname);
  vector<tio::containers::list<string>> containers(container_count);

  TioTestRunner testRunner;

  unsigned containers_per_thread = container_count / client_count;

  cout << "START: container stress test, " << container_count << " containers, "
    << client_count << " clients" << endl;

  auto start = high_resolution_clock::now();

  for (unsigned a = 0; a < client_count; a++)
  {
    testRunner.add_test(
      [hostname, prefix = "l_" + to_string(a), container_count, item_count, networkBatch]()
    {
      tio::Connection connection(hostname);
      vector<tio::containers::list<string>> containers(container_count);

      for (unsigned a = 0; a < container_count; a++)
      {
        auto& c = containers[a];
        string name = prefix + to_string(a);
        c.create(&connection, name, "volatile_list");
      }

      for (unsigned a = 0; a < container_count; a++)
      {
        auto& c = containers[a];
        vector_perf_test_c(connection.cnptr(), c.handle(), item_count, networkBatch);
      }

      containers.clear();
    }
    );
  }

  testRunner.run();

  auto delta = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();

  cout << "FINISH: container stress test, " << delta << "ms" << endl;

  containers.clear();
}


void TEST_data_stress_test(const char* hostname,
  unsigned max_client_count,
  unsigned max_subscribers,
  unsigned item_count,
  bool networkBatch)
{
  cout << "START: data stress test, "
    << "MAX_CLIENTS=" << max_client_count
    << ", MAX_SUBSCRIBERS=" << max_subscribers
    << ", ITEM_COUNT=" << item_count
    << endl;

  int baseline = 0;

  {
    TioTestRunner runner;

    string test_description = "single volatile list, one client";
    unsigned persec;

    runner.add_test(
      TioStressTest(
        hostname,
        generate_container_name(),
        "volatile_list",
        &vector_perf_test_c,
        item_count,
        &persec,
        networkBatch));

    runner.run();

    baseline = persec;

    cout << test_description << ": " << persec << " ops/sec (baseline)" << endl;
  }

  for (unsigned client_count = 1; client_count <= max_client_count; client_count *= 2)
  {
    for (unsigned subscriber_count = 0; subscriber_count <= max_subscribers; subscriber_count *= 2)
    {
      TioTestRunner runner;

      string test_description = "single volatile list, clients=" + to_string(client_count) +
        ", subscribers=" + to_string(subscriber_count);
      vector<shared_ptr<TioTesterSubscriber>> subscribers;

      vector<unsigned> persec(client_count);

      string container_name = generate_container_name();
      string container_type = "volatile_list";

      for (unsigned a = 0; a < client_count; a++)
      {
        runner.add_test(
          TioStressTest(
            hostname,
            container_name,
            container_type,
            &vector_perf_test_c,
            item_count,
            &persec[a],
            networkBatch));
      }

      for (unsigned a = 0; a < subscriber_count; a++)
      {
        subscribers.emplace_back(new TioTesterSubscriber(hostname, container_name, container_type, item_count, client_count));
        (*subscribers.rbegin())->start();
      }

      runner.run();

      for (auto& subscriber : subscribers)
      {
        subscriber->stop();
      }

      for (auto& subscriber : subscribers)
      {
        subscriber->join();
      }

      cout << test_description << ": ";

      int total = accumulate(persec.begin(), persec.end(), 0);

      float vs_baseline = ((float)total / baseline) * 100.0f;

      cout << "total " << total << " ops/sec"
        << ", perf vs baseline=" << vs_baseline << "% - ";

      for (unsigned p : persec)
        cout << p << ",";

      cout << endl;

      if (subscriber_count == 0)
        subscriber_count = 1;
    }
  }


  for (int client_count = 1; client_count <= 1024; client_count *= 2)
  {
    TioTestRunner runner;

    string test_description = "multiple volatile lists, client count=" + to_string(client_count);

    vector<unsigned> persec(client_count);

    for (int a = 0; a < client_count; a++)
    {
      runner.add_test(
        TioStressTest(
          hostname,
          generate_container_name(),
          "volatile_list",
          &vector_perf_test_c,
          item_count / client_count * 2,
          &persec[a],
          networkBatch));
    }

    runner.run();

    cout << test_description << ": ";

    unsigned total = 0;

    for (unsigned p : persec)
    {
      cout << p << ", ";
      total += p;
    }

    cout << "total " << total << " ops/sec" << endl;
  }
}

void group_callback_function(int result, void* handle, void* /*cookie*/, unsigned int event_code,
  const char* group_name, const char* container_name,
  const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
{
  static unsigned count = 0;
  cout
    << "handle=" << handle << ", "
    << "event_code=" << tio_event_code_to_string(event_code) << ", "
    << "group_name=" << group_name << ", "
    << "container_name=" << container_name << ", "
    << "k=" << (key->data_type == TIO_DATA_TYPE_STRING ? key->string_ : "") << ", "
    << "v=" << (value->data_type == TIO_DATA_TYPE_STRING ? value->string_ : "") << ", "
    << "#=" << ++count << endl;
}

void dump_pr1_message_to_stdout(const char* messageDump)
{
  cout << messageDump << endl;
}

void TEST_intelimarket_groups(const char* hostname)
{
  tio::Connection cn1(hostname);

  tio_set_dump_message_function(&dump_pr1_message_to_stdout);

  tio_group_set_subscription_callback(cn1.cnptr(), &group_callback_function, nullptr);
  tio_group_subscribe(cn1.cnptr(), "intelimarket/security_type/all/properties", "0");

  for (int a = 0; ; a++)
  {
    cn1.WaitForNextEventAndDispatch(nullptr);
  }
}

void TEST_groups(const char* hostname)
{
  tio::Connection cn1(hostname);
  tio::containers::list<string> l1;

  tio_set_dump_message_function(&dump_pr1_message_to_stdout);

  l1.create(&cn1, "a", "volatile_list");
  l1.push_back("l1-0");
  l1.push_back("l1-1");
  l1.push_back("l1-2");
  l1.AddToGroup("g");

  tio_group_set_subscription_callback(cn1.cnptr(), &group_callback_function, nullptr);
  tio_group_subscribe(cn1.cnptr(), "g", "0");

  for (int a = 0; a < 5; a++)
  {
    cn1.WaitForNextEventAndDispatch(nullptr);
  }
}

void TEST_parallel_data_stress(const char* hostname,
  short port,
  unsigned test_count,
  unsigned cycles,
  unsigned instances,
  bool networkBatch)
{
  for (unsigned cycle = 0; cycle < cycles; cycle++)
  {
#ifdef _WIN32
    TioTestRunner testRunner;
    vector<unsigned> persec(instances);
    vector<string> containerName(instances);

    for (unsigned instance = 0; instance < instances; instance++)
    {
      containerName[instance] = generate_container_name();

      testRunner.add_test
      ([=, &persec]()
        {
          tio::Connection cn(hostname, port);
          tio::containers::map<string, string> container;

          container.create(&cn, containerName[instance], "volatile_map");
          measure(cn.cnptr(), container.handle(), test_count, &map_perf_test_c, &persec[instance], networkBatch);
        }
      );
    }
    testRunner.run();

    for (unsigned instance = 0; instance < instances; instance++)
    {
      cout << "MAP " << containerName[instance] << " C:" << cycle + 1 << " R:" << persec[instance] << " per sec" << endl;
    }
#else
    for (unsigned instance = 0; instance < instances; instance++)
    {
      if (fork() == 0)
      {
        tio::Connection cn(hostname, port);
        tio::containers::map<string, string> container;

        string containerName = generate_container_name();
        container.create(&cn, containerName, "volatile_map");

        unsigned persec = 0;

        measure(cn.cnptr(), container.handle(), test_count, &map_perf_test_c, &persec, networkBatch);

        cout << "MAP " << containerName << " C:" << cycle + 1 << " R:" << persec << " per sec" << endl;
        exit(0);
      }
    }

    for (unsigned instance = 0; instance < instances; instance++)
    {
      // Wait for each child process to finish
      wait(NULL);
    }
#endif
  }
}


#ifndef NDEBUG
unsigned VOLATILE_TEST_COUNT = 250 * 1000;
unsigned PERSISTENT_TEST_COUNT = 80 * 1000;
unsigned MAX_CLIENTS = 8;
unsigned CONNECTION_STRESS_TEST_COUNT = 10 * 1000;
unsigned MAX_SUBSCRIBERS = 16;
unsigned CONTAINER_TEST_COUNT = 2 * 1000;
unsigned CONTAINER_TEST_ITEM_COUNT = 10 * 1000;

unsigned CONCURRENCY_TEST_ITEM_COUNT = 50 * 1000;
#else
unsigned VOLATILE_TEST_COUNT = 250 * 1000;
unsigned PERSISTENT_TEST_COUNT = 5 * 1000;
unsigned MAX_CLIENTS = 64;

//
// There is a TCP limit on how many connections we can make at same time,
// so we can't add much than that
//
unsigned CONNECTION_STRESS_TEST_COUNT = 100 * 1000;
unsigned MAX_SUBSCRIBERS = 64;
unsigned CONTAINER_TEST_COUNT = 10 * 1000;
unsigned CONTAINER_TEST_ITEM_COUNT = 100 * 1000;

unsigned CONCURRENCY_TEST_ITEM_COUNT = 250 * 1000;
#endif

int main(int argc, char* argv[])
{
  namespace po = boost::program_options;

  try
  {
    string hostname = "localhost";
    short port = 2605;
    bool disableNetworkBatch = false;
    unsigned instances = 1;
    unsigned cycles = 1;
    unsigned feedersTotal = 1;
    unsigned clientsTotal = 1;
    bool runTestParallelDataStress = false;
    bool runTestUmdfFeederStress = false;
    unsigned feedersTotalOfOperations = VOLATILE_TEST_COUNT;
    bool isFeederChannelProcess = false;
    bool isFeederClientProcess = false;
    bool runTestDataStress = false;
    bool runTestDeadlockOnDisconnect = false;
    bool runTestConnectionStress = false;
    bool runTestContainerConcurrency = false;
    string feederProcessSymbol;
#ifndef _WIN32
    bool unixSockets = false;
#endif
    bool help = false;
    po::options_description desc("Options");

    desc.add_options()
#ifdef _WIN32
      ("instances", po::value<unsigned>(&instances), "number of parallel clients in stress test (threads). If not informed, 1")
#else
      ("unix-socket", po::bool_switch(&unixSockets), "forces local unix socket, host option is ignored")
      ("instances", po::value<unsigned>(&instances), "number of parallel clients in stress test (processes). If not informed, 1")
#endif
      ("host", po::value<string>(&hostname), "tio host. If not informed, localhost")
      ("port", po::value<short>(&port), "connecting port. If not informed, 2605")
      ("cycles", po::value<unsigned>(&cycles), "number of execution cycles. If not informed, 1")
      ("disable-network-batch", po::bool_switch(&disableNetworkBatch), "Always wait for commands response from the hub")
      ("run-test-parallel-data-stress", po::bool_switch(&runTestParallelDataStress), "Run test TEST_parallel_data_stress")
      ("run-test-umdf-feeder-stress", po::bool_switch(&runTestUmdfFeederStress), "Run test UmdfFeederStressTest")
      ("umdf-feeder-test-total-feeders", po::value<unsigned>(&feedersTotal), "Configure total of feeders to run concurrently")
      ("umdf-feeder-test-total-clients", po::value<unsigned>(&clientsTotal), "Configure total of clients to run concurrently")
      ("umdf-feeder-test-total-operations", po::value<unsigned>(&feedersTotalOfOperations), "Total of operations that feeders will send to the Tio")
      ("run-feeder-channel-process", po::bool_switch(&isFeederChannelProcess), "Run as feeder channel process")
      ("run-feeder-client-process", po::bool_switch(&isFeederClientProcess), "Run as feeder client process")
      ("umdf-feeder-process-symbol", po::value<string>(&feederProcessSymbol), "Symbol of feeder child process")
      ("umdf-feeder-test-data-stress", po::bool_switch(&runTestDataStress), "Run data stress test")
      ("umdf-feeder-test-deadlock-on-disconnect", po::bool_switch(&runTestDeadlockOnDisconnect), "Run deadlock test")
      ("umdf-feeder-test-test-connection-stress", po::bool_switch(&runTestConnectionStress), "Run connection stress test")
      ("umdf-feeder-test-test-container-concurrency", po::bool_switch(&runTestContainerConcurrency), "Run container concurrency test")
      ("help", po::bool_switch(&help), "show help");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    bool isChildProcess = isFeederChannelProcess || isFeederClientProcess;
    bool someTest = runTestParallelDataStress
      || runTestUmdfFeederStress
      || isChildProcess
      || runTestDataStress
      || runTestDeadlockOnDisconnect
      || runTestConnectionStress
      || runTestContainerConcurrency;

    if (help || !someTest)
    {
      std::cerr << desc << endl;
      return EXIT_FAILURE;
    }


#ifndef _WIN32
    if (unixSockets)
    {
      hostname = "/var/run/tio_";

      cout << "connect locally on " << hostname << port << endl;
    }
#endif

    if (!isChildProcess)
    {
      cout << "Tiobench, The Information Overlord Benchmark. Copyright Rodrigo Strauss (www.1bit.com.br)" << endl;
      cout << "tiobench starting..." << endl;
    }

    if (runTestParallelDataStress)
    {
      cout << "n=" << VOLATILE_TEST_COUNT << endl;
      TEST_parallel_data_stress(hostname.c_str(), port, VOLATILE_TEST_COUNT, cycles, instances, !disableNetworkBatch);
    }

    if (isChildProcess)
    {
      if (isFeederChannelProcess)
      {
        auto start = std::chrono::high_resolution_clock::now();

        FeederChannelConfig config;
        config.TotalOperations = feedersTotalOfOperations;
        config.Hostname = hostname;
        config.Port = port;
        config.UseNetworkBatch = !disableNetworkBatch;
        config.Symbol = feederProcessSymbol;

        FeederChannel channel(config);
        channel.Run();

        auto delta = std::chrono::high_resolution_clock::now() - start;
        auto operationsPerSec = (static_cast<unsigned long long>(feedersTotalOfOperations) * 1000) 
          / std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();

        std::cout << "|operations:" << operationsPerSec << "|time:" << delta << "|";
      }
      else if (isFeederClientProcess)
      {
        std::thread thread;
        FeederClient client(hostname, port, feederProcessSymbol);
        thread = std::thread(&FeederClient::SubscribeAll, std::ref(client));

        std::string exit;
        std::getline(std::cin, exit);
        std::cout << exit << std::endl;

        client.Stop();
        auto totalOperations = client.TotalEventCount();

        thread.join();

        std::cout << "|operations:" << totalOperations << "|";
      }
    }
    else if (runTestUmdfFeederStress)
    {
      string execPath = argv[0];
      UmdfFeederStressTest test(hostname, port, feedersTotal, clientsTotal, !disableNetworkBatch, feedersTotalOfOperations, execPath);
      test.InitializeChannels();
      test.Run();
    }
    else if (runTestDataStress)
    {
      //
      // LOTS OF CONTAINERS
      //
      TEST_data_stress_test(hostname.c_str(),
        MAX_CLIENTS,
        MAX_SUBSCRIBERS,
        CONTAINER_TEST_ITEM_COUNT,
        !disableNetworkBatch);
    }
    else if (runTestDeadlockOnDisconnect)
    {
      //
      // DEADLOCK ON DISCONNECT
      //
      TEST_deadlock_on_disconnect(hostname.c_str());
    }
    else if (runTestConnectionStress)
    {
      //
      // LOTS OF CONNECTIONS
      //
      TEST_connection_stress(hostname.c_str(), CONNECTION_STRESS_TEST_COUNT);
    }
    else if (runTestContainerConcurrency)
    {
      //
      // CONCURRENCY
      //
      TEST_container_concurrency(
        hostname.c_str(),
        MAX_CLIENTS,
        CONCURRENCY_TEST_ITEM_COUNT,
        "volatile_map",
        map_perf_test_c,
        !disableNetworkBatch);

      TEST_container_concurrency(
        hostname.c_str(),
        MAX_CLIENTS,
        CONCURRENCY_TEST_ITEM_COUNT,
        "volatile_list",
        vector_perf_test_c,
        !disableNetworkBatch);
    }
  }
  catch (std::exception& ex)
  {
    cout << "EXCEPTION: " << ex.what() << endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
