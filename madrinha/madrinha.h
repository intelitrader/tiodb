#ifndef _MADRINHA_
#define _MADRINHA_

#include "../client/cpp/tioclient.hpp"

#include <boost/program_options.hpp>

#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <future>

namespace madrinha {

struct server {
  std::string host{};
  std::uint16_t port{};
};

struct config {
  madrinha::server master;
  std::vector<madrinha::server> slaves;
};

void string_to_server(const std::string &s, madrinha::server &sc) {
  auto pos = s.find(':');
  if (pos != std::string::npos) {
    sc.host = s.substr(0, pos);
    auto port = std::stoi(s.substr(pos + 1), nullptr);
    if (port > 65355 || port <= 0) { ///@todo fix std::numeric_limits<uint16_t>::max() in
                        /// Visual Studio
      sc.host = "";
      sc.port = 0;
      throw std::invalid_argument("port out of range, min is 1  and max is 65535");
    }
    sc.port = static_cast<uint16_t>(port);
  } else {
    auto port = std::stoi(s.substr(pos + 1), nullptr);
    if (port > 65355 || port <= 0) { ///@todo fix std::numeric_limits<uint16_t>::max() in
                        /// Visual Studio
      sc.host = "";
      sc.port = 0;
      throw std::invalid_argument("port out of range, min is 1  and max is 65535");
    }
    sc.host = "127.0.0.1";
    sc.port = static_cast<uint16_t>(port);
  }
};

int parser_arguments(int argc, const char *argv[], config &ret) {
  using namespace boost::program_options;
  int r = 0;
  options_description desc("Allowes options");
  desc.add_options()
      ("help,h", "help screen")
      ("master,m", value<std::string>(), "Master Cluster")
      ("slaves,s", value<std::vector<std::string>>()->multitoken()->composing(), "Slaves");

  try {
    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    if (vm.count("master") && vm.count("slaves")) {
      string_to_server(vm["master"].as<std::string>(), ret.master);

      auto slaves = vm["slaves"].as<std::vector<std::string>>();

      for (const auto &slave : slaves) {
        madrinha::server tmp;
        string_to_server(slave, tmp);
        ret.slaves.emplace_back(tmp);
      }

    } else {
        std::cout << "Usage: " << argv[0] << " <(--master | -m )> ([<host>:] |{localhost:} )<port> ";    
        std::cout << " <(--slave |-s)> ([<host>:] |{localhost:} )<port>..." << std::endl;
        r = 1;
    }

  } catch (std::exception &e) {
    std::cout << e.what();
    r = 1;
  }
  return r;
};

class slave_async {
  server master_;
  server slave_;
  std::unique_ptr<tio::Connection> master_conn_;
  std::unique_ptr<tio::Connection> slave_conn_;
  tio::containers::map<std::string, std::string> clusters_container_;
  std::unique_ptr<tio::containers::map<std::string, std::string>> meta_container_;
  std::future<void> thread_;

  void open_connection() {
      master_conn_ = std::make_unique<tio::Connection>(master_.host, master_.port);
      slave_conn_ = std::make_unique<tio::Connection>(slave_.host, slave_.port);
  }

  void create_and_open_container() {
    using namespace std::placeholders;

    auto handle = [&](const std::string host, const std::string &containerName,
                      const std::string &eventName, const std::string &key,
                      const std::string &value) {
      if (eventName == "set") {
        clusters_container_[key] = host;
      } else if (eventName == "query") {

        bool notMeta = key.find("__meta__") != 0;
        bool isGroups = key.find("__meta__/groups") == 0;
        bool hasKey = !clusters_container_.get(key, "").empty();

        if ((notMeta || isGroups) && !hasKey) {
          clusters_container_[key] = host;
        }
      }
    };

    clusters_container_.create(master_conn_.get(), "__meta__/clusters",
                               "volatile_map");

    meta_container_ =
        std::make_unique<tio::containers::map<std::string, std::string>>();

    meta_container_->open(slave_conn_.get(), "__meta__/containers");

    std::string host(slave_conn_->host() + ":" +
                     std::to_string(slave_conn_->port()));

    auto handleCb = std::bind<void>(handle, host, _1, _2, _3, _4);

    meta_container_->subscribe(handleCb);

    meta_container_->query(handleCb);
  }

  void worker() {
    try {
      open_connection();

      create_and_open_container();

      for (;;) {
        unsigned int timeout = 1;
        slave_conn_->WaitForNextEventAndDispatch(&timeout);
      }
    } catch (std::exception &e) {
      std::cout << e.what() << std::endl;
      exit(-1);
    }
  }

public:
  slave_async(const server master, const server slave)
      : master_(master), slave_(slave) {}

  void run() {
    thread_ = std::async(std::launch::async, &slave_async::worker, this);
  }
};

class madrinha_server {
public:
  madrinha_server(const config &config) : config_(config){};

  void run() {
    for (int i = 0; i < config_.slaves.size(); i++) {
      try {
        auto slave =
            std::make_unique<slave_async>(config_.master, config_.slaves[i]);

        slave->run();

        slaves_.emplace_back(std::move(slave));

        std::cout << "running slave " << (i+1) << " in: " << config_.slaves[i].host
                  << ":" << config_.slaves[i].port << std::endl;
  
      } catch (std::exception &e) {
        std::cout << e.what() << std::endl;
        return;
      }
    }
    std::cout << "running master in: " << config_.master.host << ":"
              << config_.master.port << std::endl;
  }

  // delete
  madrinha_server(const madrinha_server &) = delete;
  madrinha_server(const madrinha_server &&) = delete;
  madrinha_server &operator=(const madrinha_server &) = delete;
  madrinha_server &&operator=(const madrinha_server &&) = delete;

private:
  config config_;
  std::vector<std::unique_ptr<slave_async>> slaves_;
};
}; // namespace madrinha

#endif
