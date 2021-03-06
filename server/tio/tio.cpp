/*
Tio: The Information Overlord
Copyright 2010 Rodrigo Strauss (http://www.1bit.com.br)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "pch.h"
#include "tio.h"
#include "TioTcpServer.h"
#include "Container.h"
#include "MemoryStorage.h"
//#include "BdbStorage.h"
#include "LogDbStorage.h"
#include "../../client/cpp/tioclient.hpp"
#include "tiomutex.h"

#if TIO_PYTHON_PLUGIN_SUPPORT
#include "TioPython.h"
#endif

using namespace tio;

using std::shared_ptr;
using std::make_shared;
using boost::scoped_array;
using std::cout;
using std::endl;
using std::queue;

void LoadStorageTypes(ContainerManager* containerManager, const string& dataPath)
{
	auto mem = make_shared<tio::MemoryStorage::MemoryStorageManager>();
	
	//shared_ptr<ITioStorageManager> ldb = 
	//	shared_ptr<ITioStorageManager>(new tio::LogDbStorage::LogDbStorageManager(dataPath));

	containerManager->RegisterFundamentalStorageManagers(mem, mem, mem);

//	containerManager->RegisterStorageManager("bdb_map", bdb);
//	containerManager->RegisterStorageManager("bdb_vector", bdb);

	/*containerManager->RegisterStorageManager("persistent_list", ldb);
	containerManager->RegisterStorageManager("persistent_map", ldb);*/
}

void SetupContainerManager(
	tio::ContainerManager* manager, 
	const string& dataPath,
	const vector< pair<string, string> >& aliases)
{
	LoadStorageTypes(manager, dataPath);

	pair<string, string> p;
	BOOST_FOREACH(p, aliases)
	{
		manager->AddAlias(p.first, p.second);
	}
}

void RunServer(tio::ContainerManager* containerManager,
			   unsigned short port, 
			   const vector< pair<string, string> >& users,
			   const string& logFilePath,
			   unsigned threadCount,
			   SERVER_OPTIONS options)
{
	namespace asio = boost::asio;
	using namespace boost::asio::ip;
	using std::thread;
	using std::vector;

	asio::io_context io_service;
	string local_endpoint_path = string("/var/run/tio_") + std::to_string(port);
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
	TioTcpServer::local_endpoint_t le(local_endpoint_path);
#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS
	TioTcpServer::endpoint_t e(tcp::v4(), port);

	auto remove_local_endpoint = [&]()
	{
		if (boost::filesystem::exists(local_endpoint_path))
		{
			if (!boost::filesystem::remove(local_endpoint_path))
			{
				cout << "Error emoving " << local_endpoint_path << endl;
			}
		}
	};

	auto handler = [&](const boost::system::error_code& error, int signal_number)
	{
		std::cout << "Exiting..." << signal_number << std::endl;
		remove_local_endpoint();
		exit(1);
	};

	remove_local_endpoint();


	//
	// default aliases
	//
	if(users.size())
	{
		shared_ptr<ITioContainer> usersContainer = containerManager->CreateContainer("volatile_map", "__users__");

		pair<string, string> p;
		BOOST_FOREACH(p, users)
		{
			usersContainer->Set(p.first, p.second, "clean");
		}
	}

	asio::io_service::work work(io_service);

	tio::TioTcpServer tioServer(*containerManager, 
		io_service, 
		e, 
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
		le, 
#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS
		logFilePath);

	tioServer.Start();

	vector<thread> threads;

	for (unsigned a = 0; a < threadCount; a++)
	{
		threads.emplace_back(
			[&]()
			{
				if (options.ioServiceMethod == IoServiceMethod::poll)
				{
					for (;;)
					{
						io_service.poll();
						std::this_thread::sleep_for(std::chrono::microseconds(options.ioServicePollSleepTime));
					}
				}
				else
				{
					io_service.run();
				}
			});
	}

	boost::asio::signal_set signals(io_service, SIGINT);
	signals.async_wait(handler);

	cout << "Up and running, " << threadCount << " threads" << endl;

	for (auto& t : threads)
	{
		if (t.joinable())
			t.join();
	}
}

class cpp2c
{
	TIO_DATA c_;
public:
	cpp2c(const TioData& v)
	{
		tiodata_set_as_none(&c_);

		switch(v.GetDataType())
		{
		case TioData::String:
			tiodata_set_string_and_size(&c_, v.AsSz(), v.GetSize());
			break;
		case TioData::Int:
			tiodata_set_int(&c_, v.AsInt());
			break;
		case TioData::Double:
			tiodata_set_double(&c_, v.AsDouble());
			break;
		default:
			return;
		}
	}

	operator TIO_DATA*()
	{
		return &c_;
	}

};

class c2cpp
{
	TioData cpp_;
	TIO_DATA* c_;
	bool out_;
public:
	c2cpp(const TIO_DATA* c)
	{
		c_ = const_cast<TIO_DATA*>(c);
		out_ = false;

		if(!c_)
			return;

		switch(c->data_type)
		{
		case TIO_DATA_TYPE_DOUBLE:
			cpp_.Set(c->double_);
			break;
		case TIO_DATA_TYPE_INT:
			cpp_.Set(c->int_);
			break;
		case TIO_DATA_TYPE_STRING:
			cpp_.Set(c->string_, c->string_size_);
			break;
		default:
			return;
		}
	}

	~c2cpp()
	{
		if(out_ && c_)
		{
			switch(cpp_.GetDataType())
			{
			case TioData::String:
				tiodata_set_string_and_size(c_, cpp_.AsSz(), cpp_.GetSize());
				break;
			case TioData::Int:
				tiodata_set_int(c_, cpp_.AsInt());
				break;
			case TioData::Double:
				tiodata_set_double(c_, cpp_.AsDouble());
				break;
			default:
				return;
			}
		}
	}

	operator TioData()
	{
		return cpp_;
	}

	TioData* inptr()
	{
		if(cpp_.IsNull())
			return NULL;

		return &cpp_;
	}

	TioData* outptr()
	{
		if(!c_)
			return NULL;

		out_ = true;
		return &cpp_;
	}
};

class LocalContainerManager : public IContainerManager
{
private:
	tio::ContainerManager& containerManager_;
	std::map<void*, unsigned int> subscriptionHandles_;

public:

	LocalContainerManager(tio::ContainerManager& containerManager) 
		:
	    containerManager_(containerManager)
	{
	}

	virtual IContainerManager* container_manager()
	{
		return this;
	}

protected:
	virtual int create(const char* name, const char* type, void** handle)
	{
		try
		{
			shared_ptr<ITioContainer> container = containerManager_.CreateContainer(type, name);
			*handle = new shared_ptr<ITioContainer>(container);
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int open(const char* name, const char* type, void** handle)
	{
		try
		{
			shared_ptr<ITioContainer> container = containerManager_.OpenContainer(type ? std::string(type) : string(), name);
			*handle = new shared_ptr<ITioContainer>(container);
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int close(void* handle)
	{
		delete ((shared_ptr<ITioContainer>*)handle);
		return 0;
	}

	virtual int group_add(const char* group_name, const char* container_name)
	{
		//
		// TODO: no group subscription support for plugins yet
		//
		ASSERT(false);
		return -666;


	}

	virtual int container_propset(void* handle, const struct TIO_DATA* key, const struct TIO_DATA* value)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();
		
		if(key->data_type != TIO_DATA_TYPE_STRING || value->data_type != TIO_DATA_TYPE_STRING)
			return -1;

		container->SetProperty(key->string_, value->string_);

		return 0;
	}

	virtual int container_push_back(void* handle, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();
		
		try
		{
			container->PushBack(c2cpp(key), c2cpp(value), c2cpp(metadata));
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_push_front(void* handle, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			container->PushFront(c2cpp(key), c2cpp(value), c2cpp(metadata));
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_pop_back(void* handle, struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			container->PopBack(c2cpp(key).outptr(), c2cpp(value).outptr(), c2cpp(metadata).outptr());
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_pop_front(void* handle, struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			container->PopFront(c2cpp(key).outptr(), c2cpp(value).outptr(), c2cpp(metadata).outptr());
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_set(void* handle, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			container->Set(c2cpp(key), c2cpp(value), c2cpp(metadata));
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_insert(void* handle, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			container->Insert(c2cpp(key), c2cpp(value), c2cpp(metadata));
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_clear(void* handle)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			container->Clear();
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_delete(void* handle, const struct TIO_DATA* key)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			container->Delete(c2cpp(key));
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_get(void* handle, const struct TIO_DATA* search_key, struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			container->GetRecord(c2cpp(search_key), c2cpp(key).outptr(), c2cpp(value).outptr(), c2cpp(metadata).outptr());
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_propget(void* handle, const struct TIO_DATA* search_key, struct TIO_DATA* value)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();
		std::string ret;

		try
		{
			ret = container->GetProperty(static_cast<TioData>(c2cpp(search_key)).AsSz());
		}
		catch(std::exception&)
		{
			return -1;
		}

		tiodata_set_string_and_size(value, ret.c_str(), ret.size());

		return 0;
	}

	virtual int container_get_count(void* handle, int* count)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			*count = container->GetRecordCount();
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_query(void* handle, int start, int end, query_callback_t query_callback, void* cookie)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			shared_ptr<ITioResultSet> resultset = container->Query(start, end, TIONULL);

			TioData key, value, metadata;

			while(resultset->GetRecord(&key, &value, &metadata))
			{
				query_callback(0, handle, cookie, 0, container->GetName().c_str(), cpp2c(key), cpp2c(value), cpp2c(metadata));
				resultset->MoveNext();
			}
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	static void SubscribeBridge(void* cookie, event_callback_t event_callback, const string& eventName, const TioData& key, const TioData& value, const TioData& metadata)
	{
		//
		// TODO: need to change code to use the new callback signature
		//
		//event_callback(cookie, 10, 0, cpp2c(key), cpp2c(value), cpp2c(metadata));
	}

	static void WaitAndPopNextBridge(void* cookie, event_callback_t event_callback, const string& eventName, const TioData& key, const TioData& value, const TioData& metadata)
	{
		//
		// TODO: need to change code to use the new callback signature
		//
		//event_callback(cookie, 10, 0, cpp2c(key), cpp2c(value), cpp2c(metadata));
	}

	virtual int container_subscribe(void* handle, struct TIO_DATA* start, event_callback_t event_callback, void* cookie)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		string startString;

		if(start && start->data_type != TIO_DATA_TYPE_STRING)
			return -1;

		if(start)
			startString = start->string_;

		try
		{
			// adding "this" due to a bug in gcc...
			/*cppHandle = container->Subscribe(
				[this, cookie, event_callback](const string& eventName, const TioData& key, const TioData& value, const TioData& metadata)
				{
					LocalContainerManager::SubscribeBridge(cookie, event_callback, eventName, key, value, metadata);
				},
				startString);

			subscriptionHandles_[handle] = cppHandle;*/
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_unsubscribe(void* handle)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			//container->Unsubscribe(subscriptionHandles_[handle]);
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual int container_wait_and_pop_next(void* handle, event_callback_t event_callback, void* cookie)
	{
		ITioContainer* container = ((shared_ptr<ITioContainer>*)handle)->get();

		try
		{
			/*container->WaitAndPopNext(
				[this, cookie, event_callback](const string& eventName, const TioData& key, const TioData& value, const TioData& metadata)
				{
					LocalContainerManager::SubscribeBridge(cookie, event_callback, eventName, key, value, metadata);
				});*/
		}
		catch(std::exception&)
		{
			return -1;
		}

		return 0;
	}

	virtual bool connected()
	{
		return true;
	}
};

class PluginThread
{
	tio_plugin_start_t func_;
	tio::IContainerManager* containerManager_;
	queue<std::function<void()> > eventQueue_;
	boost::condition_variable hasWork_;

public:
	PluginThread(tio_plugin_start_t func, tio::IContainerManager* containerManager) : func_(func), containerManager_(containerManager)
	{

	}

	void AnyThreadCallback(EventSink sink, const string& eventName, const TioData& key, const TioData& value, const TioData& metadata)
	{

	}

	void start()
	{
		
		
	}
};

#ifdef _WIN32	
void LoadPlugin(const string path, tio::IContainerManager* containerManager, const map<string, string>& pluginParameters)
{
	tio_plugin_start_t pluginStartFunction = NULL;

	HMODULE hdll = LoadLibrary(path.c_str());
	
	if(!hdll)
	{
		stringstream str;
		str << "error loading plugin \"" << path << "\": Win32 error " << GetLastError();
		throw std::runtime_error(str.str());
	}

	pluginStartFunction = (tio_plugin_start_t)GetProcAddress(hdll, "tio_plugin_start");

	if(!pluginStartFunction)
	{
		throw std::runtime_error("plugin doesn't export the \"tio_plugin_start\"");
	}

	scoped_array<KEY_AND_VALUE> kv(new KEY_AND_VALUE[pluginParameters.size() + 1]);

	int a = 0;
	for(map<string, string>::const_iterator i = pluginParameters.begin() ; i != pluginParameters.end() ; ++i, a++)
	{
		kv[a].key = i->first.c_str();
		kv[a].value = i->second.c_str();
	}

	// last one must have key = NULL
	kv[pluginParameters.size()].key = NULL;

	pluginStartFunction(containerManager, kv.get());
}
#else
void LoadPlugin(const string path, tio::IContainerManager* containerManager, const map<string, string>& pluginParameters)
{
//nada
//
}

#endif //_WIN32

void LoadPlugins(const std::vector<std::string>& plugins, const map<string, string>& pluginParameters, tio::IContainerManager* containerManager)
{
	BOOST_FOREACH(const string& pluginPath, plugins)
	{
		LoadPlugin(pluginPath, containerManager, pluginParameters);
	}
}

int main(int argc, char* argv[])
{
	namespace po = boost::program_options;
    
	cout << "Tio, The Information Overlord. Copyright Rodrigo Strauss (www.1bit.com.br)" << endl;

	try
	{
		po::options_description desc("Options");

		desc.add_options()
			("alias", po::value< vector<string> >(), "set an alias for a container type, using syntax alias=container_type")
			("user", po::value< vector<string> >(), "add user, using syntax user:password")
#if TIO_PYTHON_PLUGIN_SUPPORT
			("python-plugin", po::value< vector<string> >(), "load and run a python plugin")
#endif
			("plugin", po::value< vector<string> >(), "load and run a plugin")
			("plugin-parameter", po::value< vector<string> >(), "parameters to be passed to plugins. name=value")
			("port", po::value<unsigned short>(), "listening port. If not informed, 2605")
			("threads", po::value<unsigned short>(), "number of running threads")
			("log-path", po::value<string>(), "transaction log file path. It must be a full file path, not just the directory. Ex: c:\\data\\tio.log")
			("data-path", po::value<string>(), "sets data path")
			("io_service-poll", "sets poll method to run the io_context object's event processing loop. Default method is run")
			("poll-sleep-time", po::value<unsigned short>(), "time (in microseconds) to sleep after io_service poll executes ready handlers. Default sleep time is 10000");

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		vector< pair<string, string> > aliases;

		if(vm.count("alias") != 0)
		{
			BOOST_FOREACH(const string& alias, vm["alias"].as< vector<string> >())
			{
				string::size_type sep = alias.find('=', 0);

				if(sep == string::npos)
				{
					cout << "invalid alias: \"" << alias << "\"" << endl;
					return 1;
				}

				aliases.push_back(make_pair(alias.substr(0, sep), alias.substr(sep+1)));
			}
		}

		vector< pair<string, string> > users;

		if(vm.count("user") != 0)
		{
			BOOST_FOREACH(const string& user, vm["user"].as< vector<string> >())
			{
				string::size_type sep = user.find(':', 0);

				if(sep == string::npos)
				{
					cout << "invalid user syntax: \"" << user << "\"" << endl;
					return 1;
				}

				users.push_back(make_pair(user.substr(0, sep), user.substr(sep+1)));
			}
		}

		unsigned short threadCount = 32;

		if (vm.count("threads"))
		{
			threadCount = vm["threads"].as<unsigned short>();
		}

		if(threadCount == 1)
		{
			cout << "Running as singlethread, disabling all mutexes" << endl;
			tio_recursive_mutex::set_single_threaded();
		}

		{
			cout << "Starting infrastructure... " << endl;
			tio::ContainerManager containerManager;
			LocalContainerManager localContainerManager(containerManager);

			string dataPath =
				vm.count("data-path") == 0 ?
				boost::filesystem::temp_directory_path().generic_string() :
				vm["data-path"].as<string>();

			cout << "Saving files to " << dataPath << endl;
			
			SetupContainerManager(&containerManager, dataPath, aliases);

			//
			// Parse plugin parameters
			//
			map<string, string> pluginParameters;

			if(vm.count("plugin-parameter") != 0)
			{
				BOOST_FOREACH(const string& parameter, vm["plugin-parameter"].as< vector<string> >())
				{
					string::size_type sep = parameter.find('=', 0);

					if(sep == string::npos)
					{
						cout << "invalid plugin parameter syntax: \"" << parameter << "\"" << endl;
						return 1;
					}

					pluginParameters[parameter.substr(0, sep)] = parameter.substr(sep+1);
				}
			}

			if(vm.count("plugin"))
			{
				cout << "Loading plugins... " << endl;
				LoadPlugins(vm["plugin"].as< vector<string> >(), pluginParameters, &localContainerManager);
			}

#if TIO_PYTHON_PLUGIN_SUPPORT
			if(vm.count("python-plugin"))
			{
				cout << "Starting Python support... " << endl;
				InitializePythonSupport(argv[0], &containerManager);

				cout << "Loading Python plugins... " << endl;
				LoadPythonPlugins(vm["python-plugin"].as< vector<string> >(), pluginParameters);
			}
#endif

			unsigned short port = 2605;

			if(vm.count("port"))
				port = vm["port"].as<unsigned short>();

			cout << "Listening on port " 
				<< port 
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
				<< " (and locally on /var/run/tio_" << port << ")"
#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS
				<< endl;

			string logFilePath;

			if (vm.count("log-path"))
			{
				logFilePath = vm["log-path"].as<string>();

				cout << "Saving transaction log to " << logFilePath << endl;
			}

			SERVER_OPTIONS options;
			options.ioServiceMethod = IoServiceMethod::run;

			if (vm.count("io_service-poll"))
			{
				options.ioServiceMethod = IoServiceMethod::poll;
				
				options.ioServicePollSleepTime = 10000;

				if (vm.count("poll-sleep-time"))
				{
					options.ioServicePollSleepTime = vm["poll-sleep-time"].as<unsigned short>();
				}

				cout << "Using io_service::poll method (sleep for " << options.ioServicePollSleepTime << " microseconds)" <<  endl;
			}
		
			RunServer(
				&containerManager,
				port,
				users,
				logFilePath,
				threadCount, 
				options);
		}
	}
	catch(std::exception& ex)
	{
		cout << "error: " << ex.what() << endl;
	}

	return 0;
}

