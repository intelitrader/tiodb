#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

#include <string>
#include <map>
#include <vector>

#include <iostream>
#include <sstream>
#include <fstream>

#include <thread>
#include <future>

#include <chrono>
#include <iomanip> // get_time

#include <tioclient.h>
#include <tioclient.hpp>
#include <tioclient_internals.h>

using namespace std::chrono;

namespace Utils
{
	inline std::vector<std::string> Split(const std::string& str, char delimiter, bool skip_empty = false)
	{
		std::vector<std::string> tokens;

		size_t pos_current{};
		size_t pos_last{};
		size_t length{};

		while(true)
		{
			pos_current = str.find(delimiter, pos_last);
			if(pos_current == std::string::npos)
				pos_current = str.size();

			length = pos_current - pos_last;
			if(!skip_empty || (length != 0))
				tokens.emplace_back(str.substr(pos_last, length));

			if(pos_current == str.size())
				break;
			else
				pos_last = pos_current + 1;
		}

		return tokens;
	}

	inline std::string Join(const std::vector<std::string>& strings, char delimiter)
	{
		if(strings.empty())
			return "";

		if(strings.size() == 1)
			return strings[0];

		std::stringstream ss;

		for(auto p = strings.begin(); p != strings.end(); ++p) {
			ss << *p;
			if(p != strings.end() - 1)
				ss << delimiter;
		}

		return ss.str();
	}

	inline bool StartsWith(std::string s, std::string suffix) {
		return (s.rfind(suffix, 0) == 0);
	}

	inline bool EndsWith(std::string s, std::string suffix) {
		return (std::equal(suffix.rbegin(), suffix.rend(), s.rbegin()));
	}

	inline system_clock::time_point TimeFromString(std::string& time, const char* format = "%Y-%m-%d %T")
	{
		double r;
		struct std::tm tm;
		std::istringstream ss(time);
		ss >> std::get_time(&tm, format) >> r;

		const auto date = time_point_cast<std::chrono::microseconds>(system_clock::from_time_t(mktime(&tm))) + microseconds(lround(r * std::micro::den));
		return date;
	}
}


class TioContainer
{
	void* container_;
	std::string name_;
	tio::IContainerManager* containerManager_;
public:
	TioContainer()
		: container_(nullptr)
		, containerManager_(nullptr)
	{
	}

	void create(tio::Connection* cn, const std::string& name, const std::string& containerType)
	{
		int result;

		containerManager_ = cn->container_manager();

		result = containerManager_->create(name.c_str(), containerType.c_str(), &container_);

		tio::ThrowOnTioClientError(result);

		name_ = name;
	}

	void push_back(const struct TIO_DATA* key, const struct TIO_DATA* value)
	{
		int ret = containerManager_->container_push_back(container_, key, value, nullptr);

		tio::ThrowOnTioClientError(ret);
	}

	void push_front(const struct TIO_DATA* key, const struct TIO_DATA* value)
	{
		int ret = containerManager_->container_push_front(container_, key, value, nullptr);

		tio::ThrowOnTioClientError(ret);
	}

	void pop_back()
	{
		TIO_DATA* value = new TIO_DATA();
		tiodata_set_as_none(value);
		int ret = containerManager_->container_pop_back(container_, nullptr, value, nullptr);

		tio::ThrowOnTioClientError(ret);
	}

	void pop_front()
	{
		TIO_DATA* value = new TIO_DATA();
		tiodata_set_as_none(value);
		int ret = containerManager_->container_pop_front(container_, nullptr, value, nullptr);

		tio::ThrowOnTioClientError(ret);
	}

	void set(const struct TIO_DATA* key, const struct TIO_DATA* value)
	{
		int ret = containerManager_->container_set(container_, key, value, nullptr);

		tio::ThrowOnTioClientError(ret);
	}

	void insert(const struct TIO_DATA* key, const struct TIO_DATA* value)
	{
		int ret = containerManager_->container_insert(container_, key, value, nullptr);

		tio::ThrowOnTioClientError(ret);
	}

	void erase(const struct TIO_DATA* key)
	{
		int ret = containerManager_->container_delete(container_, key);

		tio::ThrowOnTioClientError(ret);
	}

	void clear()
	{
		int ret = containerManager_->container_clear(container_);

		tio::ThrowOnTioClientError(ret);
	}

	void propset(const struct TIO_DATA* key, const struct TIO_DATA* value)
	{
		int ret = containerManager_->container_propset(container_, key, value);

		tio::ThrowOnTioClientError(ret);
	}
};

class LogEntry
{
public:
	std::string line_;
	std::string command_;
	int handle_;
	system_clock::time_point dateTime_;
	TIO_DATA* key_;
	TIO_DATA* value_;

	TIO_DATA* Deserialize(std::string info, std::string data, int &size)
	{
		TIO_DATA *tioData = new TIO_DATA();

		// Take type out info (format: "s12"; 's' for string. 'n' is null)
		auto type = info[0];

		if(type == 'n')
		{
			tiodata_set_as_none(tioData);
			return tioData;
		}

		// Get the size of data
		info.erase(0, 1);
		size = std::stoi(info);

		auto fieldData = data.substr(0, size);

		switch(type)
		{
			case 's':
				tiodata_set_string_and_size(tioData, &data[0], size);
				break;
			case 'i':
				tiodata_set_int(tioData, std::stoi(fieldData));
				break;
			case 'd':
				tiodata_set_double(tioData, std::stod(fieldData));
				break;
		}

		return tioData;
	}

	LogEntry(std::string& line)
	{
		line_ = line;

		if(Utils::EndsWith(line_, ",n,"))
		{
			line_.resize(line_.size() - 3);
		}

		auto entry = Utils::Split(line_, ',');

		dateTime_ = Utils::TimeFromString(entry[0]);
		command_ = entry[1];
		handle_ = std::stoi(entry[2]);

		std::string keyInfo = entry[3];

		std::vector<std::string> sub = { entry.begin() + 4, entry.end() };
		std::string rest = Utils::Join(sub, ',');

		int size = 0;
		key_ = Deserialize(keyInfo, rest, size);

		rest = rest.substr((int)size + 1, rest.size());
		entry = Utils::Split(rest, ',');

		std::string valueInfo = entry[0];

		sub = { entry.begin() + 1, entry.end() };
		rest = Utils::Join(sub, ',');

		value_ = Deserialize(valueInfo, rest, size);
	}
};

class StatsLogger
{
	int containerCount_ = 0;
	int messageCount_ = 0;
	int lastLogMessageCount_ = 0;
	int totalData_ = 0;
	int totalChanges_ = 0;
	system_clock::time_point lastLog_;

public:
	StatsLogger()
	{
		lastLog_ = system_clock::now();
	}

	void OnLogEntry(LogEntry log)
	{
		messageCount_ += 1;

		constexpr int DATE_SIZE = 24;
		totalData_ += log.line_.size() - DATE_SIZE;

		if(log.command_ == "create")
		{
			containerCount_ += 1;
		}
		else
		{
			totalChanges_ += 1;
		}
	}

	void Log()
	{
		//auto delta = duration_cast<std::chrono::milliseconds>(system_clock::now() - lastLog_).count() / 1000.0;
		auto delta = (system_clock::now() - lastLog_).count() / 10000000.0;
		auto msgCount = messageCount_ - lastLogMessageCount_;
		auto persec = msgCount / (delta > 0 ? delta : 1);
		auto totalKb = totalData_ / 1024;

		std::cout << messageCount_ << " msgs, " << containerCount_ << " containers, " << totalChanges_ << " changes, " << totalKb << "kb so far, " << persec << " msgs/s" << std::endl;

		lastLog_ = system_clock::now();
		lastLogMessageCount_ = messageCount_;
	}
};

class TioLogParser
{
	tio::Connection cn_;
	std::map<int, TioContainer*> containers_;

	std::istream* input_;

	std::string path_;
	int speed_;
	int delay_;
	bool follow_;
	int maxNetworkBatchMessages_;

	StatsLogger logger_;

	std::thread publisherThread_;
	std::vector<LogEntry> logQueue_;
	std::mutex logQueueMutex_;
	std::condition_variable logQueueCondition_;
	std::atomic<bool> shouldStop_ = false;

public:
	TioLogParser(const std::string & address, int port, std::string path, int speed, int delay, bool follow, int maxNetworkBatchMessages)
		: path_(path)
		, speed_(speed)
		, delay_(delay)
		, follow_(follow)
		, maxNetworkBatchMessages_(maxNetworkBatchMessages)
	{
		cn_.Connect(address, port);

		if(path_ == "stdin")
		{
			input_ = &std::cin;
		}
		else
		{
			input_ = new std::ifstream(path_, std::ios::in | std::ios::binary);
		}
	}

	~TioLogParser()
	{
		if(path_ != "stdin")
		{
			delete input_;
		}

		{
			std::unique_lock<std::mutex> lock(logQueueMutex_);
			shouldStop_ = true;
		}

		logQueueCondition_.notify_one();

		if(publisherThread_.joinable())
		{
			publisherThread_.join();
		}
	}

	void Replay()
	{
		publisherThread_ = std::thread(&TioLogParser::PublisherThread, this);

		std::string line;
		long lastMaxOffset = 0;

		for(;;)
		{
			if(follow_ && path_ != "stdin")
			{
				std::ifstream* is = dynamic_cast<std::ifstream*>(input_);

				(*is).seekg(0, std::ios::end);
				int currentOffset = (*is).tellg();

				if(currentOffset == lastMaxOffset)
				{
					std::this_thread::sleep_for(std::chrono::seconds(1));
					continue;
				}

				// Seek to the last max offset
				(*is).seekg(lastMaxOffset, std::ios::beg);
			}

			while(!(*input_).eof() && std::getline(*input_, line))
			{
				if(line == "\r" || line == "\n")
				{
					continue;
				}

				{
					std::unique_lock<std::mutex> lock(logQueueMutex_);
					logQueue_.push_back(LogEntry(line));
				}

				logQueueCondition_.notify_one();
			}

			if(follow_ && path_ != "stdin")
			{
				std::cout << "\nWaiting for file to grow..." << std::endl;

				std::ifstream* is = dynamic_cast<std::ifstream*>(input_);
				(*is).seekg(0, std::ios::end);
				lastMaxOffset = (*is).tellg();
			}
			else
			{
				break;
			}
		}
	}

	bool OnLogEntry(LogEntry log)
	{
		if(log.command_ == "create")
		{
			auto key = std::string(reinterpret_cast<const char*>(log.key_->string_), log.key_->string_size_);
			if(Utils::StartsWith(key, "__"))
			{
				return false;
			}
			auto value = std::string(reinterpret_cast<const char*>(log.value_->string_), log.value_->string_size_);

			auto container = new TioContainer();
			container->create(&cn_, key, value);
			containers_[log.handle_] = container;
		}
		else if(log.command_ == "group_add")
		{
			auto key = std::string(reinterpret_cast<const char*>(log.key_->string_), log.key_->string_size_);
			auto value = std::string(reinterpret_cast<const char*>(log.value_->string_), log.value_->string_size_);
			cn_.AddToGroup(key, value);
		}
		else
		{
			auto pos = containers_.find(log.handle_);
			auto& container = pos->second;

			if(log.command_ == "push_back")
			{
				container->push_back(log.key_, log.value_);
			}
			else if(log.command_ == "push_front")
			{
				container->push_front(log.key_, log.value_);
			}
			else if(log.command_ == "pop_back")
			{
				container->pop_back();
			}
			else if(log.command_ == "pop_front")
			{
				container->pop_front();
			}
			else if(log.command_ == "set")
			{
				container->set(log.key_, log.value_);
			}
			else if(log.command_ == "insert")
			{
				container->insert(log.key_, log.value_);
			}
			else if(log.command_ == "delete")
			{
				container->erase(log.key_);
			}
			else if(log.command_ == "clear")
			{
				container->clear();
			}
			else if(log.command_ == "propset")
			{
				container->propset(log.key_, log.value_);
			}
			else
			{
				return false;
			}
		}

		return true;
	}

private:
	void PublisherThread()
	{
		int sendCount = 0;
		while(!shouldStop_)
		{
			std::vector<LogEntry> localLogQueue;

			{
				std::unique_lock<std::mutex> lock(logQueueMutex_);

				while(logQueue_.empty())
				{
					logQueueCondition_.wait(lock);
				}

				logQueue_.swap(localLogQueue);
			}

			int batchCount = 0;
			std::shared_ptr<tio::Connection::TioScopedNetworkBatch> networkBatch;

			for(const auto& log : localLogQueue)
			{
				if(log.command_ == "create" || log.command_ == "group_add")
				{
					networkBatch.reset();
					batchCount = 0;
				}
				else if(!networkBatch)
				{
					networkBatch = std::make_shared<tio::Connection::TioScopedNetworkBatch>(cn_);
				}

				if(batchCount == maxNetworkBatchMessages_)
				{
					networkBatch.reset();
					batchCount = 0;
				}

				if(delay_ > 0)
				{
					SleepIfNeeded(log.dateTime_);
				}

				bool ret = OnLogEntry(log);

				if(ret)
				{
					logger_.OnLogEntry(log);

					sendCount += 1;
					batchCount += 1;

					if(sendCount % 100000 == 0)
					{
						logger_.Log();
						//std::cout << "Sent: " << log.line_ << std::endl;
					}
				}
			}
		}
	}

	void SleepIfNeeded(system_clock::time_point datetime)
	{
		auto diffTime = duration_cast<std::chrono::seconds>(system_clock::now() - datetime).count();
		auto waitSecs = delay_ - diffTime;

		if(waitSecs > 0)
		{
			std::cout << "\nDelay time is " << delay_ << " seconds. It will take more " << waitSecs << " seconds for the command to be sent.\n" << std::endl;
			std::this_thread::sleep_for(std::chrono::seconds(waitSecs));
		}

	}
};


struct TioLogReplayConfig
{
	std::string address = "localhost";
	unsigned short port = 2605;
	std::string file = "stdin";

	unsigned speed = 0;
	unsigned delay = 0;
	bool follow = false;
	int maxNetworkBatchMessages = 1000;
};


bool ParseCommandLine(int argc, char* argv[], TioLogReplayConfig* config)
{
	try
	{
		bool ret = false;
		for(int i = 0; i < argc; i++)
		{
			if(Utils::StartsWith(argv[i], "--address"))
			{
				config->address = argv[++i];
			}
			else if(Utils::StartsWith(argv[i], "--port"))
			{
				config->port = std::stoi(argv[++i]);
			}
			else if(Utils::StartsWith(argv[i], "--file-path"))
			{
				config->file = argv[++i];
				ret = true;
			}
			else if(Utils::StartsWith(argv[i], "--speed"))
			{
				config->speed = std::stoi(argv[++i]);
			}
			else if(Utils::StartsWith(argv[i], "--delay"))
			{
				config->delay = std::stoi(argv[++i]);
			}
			else if(Utils::StartsWith(argv[i], "--follow"))
			{
				config->follow = true;
			}
			else if(Utils::StartsWith(argv[i], "--max-network-batch-messages"))
			{
				config->maxNetworkBatchMessages = std::stoi(argv[++i]);
			}
		}
		return ret;
	}
	catch(std::runtime_error& e)
	{
		std::cerr << "ParseCommandLine Exception: " << e.what() << std::endl;
		return false;
	}
}



int main(int argc, char* argv[])
{
	TioLogReplayConfig config;

	std::cout << "\r\nTioLogReplay. Intelitrader Tecnologia." << std::endl;

	if(!ParseCommandLine(argc, argv, &config))
	{
		std::cout << "\n\nHelp: "
			<< "\n  --file-path \tSpecify the full path for the log file. Or you can set \"stdin\" as input "
			<< "\n  --address \tDefines the address for Tio server. (default=localhost) "
			<< "\n  --port \tDefines the port for Tio server. (default=2605) "
			<< "\n  --delay \tMessage delay relative to original message time. We will wait if necessary to make sure the message will be replicated X second after the time message was written to log. (default=0) "
			<< "\n  --speed \tSpeed that messages will be send to tio. 0 = as fast as possible. (default=0) "
			<< "\n  --follow \tFollow the file. Will wait for the file to grow to continue. "
			<< "\n  --max-network-batch-messages\n\t\tHow many messages sent before reset network batch mode. (default=1000) "
			<< std::endl;

		return -1;
	}

	TioLogParser tioLogParser(config.address, config.port, config.file, config.speed, config.delay, config.follow, config.maxNetworkBatchMessages);

	tioLogParser.Replay();

	return 0;
}

