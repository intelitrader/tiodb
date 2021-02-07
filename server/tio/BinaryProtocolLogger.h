#include "pch.h"
#include "logdb.h"

#ifndef _WIN32
#define ASYNCHRONOUS_WRITE_CIRCULAR_QUEUE	//or ASYNCHRONOUS_WRITE_LIST
#define ASYNCHRONOUS_DATETIME
#endif


namespace tio
{
#ifdef ASYNCHRONOUS_WRITE_CIRCULAR_QUEUE
	static const int RAW_LOG_QUEUE_SIZE = 250000;
	static std::array<std::string, RAW_LOG_QUEUE_SIZE> rawLogQueue;
#endif

    class BinaryProtocolLogger
	{
		typedef tio_recursive_mutex mutex_t;
		typedef lock_guard<mutex_t> lock_guard_t;

		map<string, unsigned> globalContainerHandle_;
		mutex_t globalContainerHandleLock_;
		int lastGlobalHandle_;
		logdb::File f_;


#ifdef ASYNCHRONOUS_DATETIME
		char timeString_[64];
		std::mutex updateTimeLock_;
		std::condition_variable updateTimeCondition_;
		std::unique_ptr<std::thread> updateTimeThread_;
#endif


#ifdef ASYNCHRONOUS_WRITE_CIRCULAR_QUEUE

		int logQueueHead_;
		int logQueueTail_;
		mutex_t logQueueLock_;
		std::unique_ptr<std::thread> writeRawLogThread_;


		inline bool IsLogQueueFull()
		{
			return ((rawLogQueue.max_size() - logQueueTail_ + logQueueHead_) % rawLogQueue.max_size()) == (rawLogQueue.max_size() - 1);
		}

		void RawLog(const string& what)
		{
			lock_guard_t lock(logQueueLock_);

			rawLogQueue[logQueueHead_] = what;

			while(IsLogQueueFull())
				std::this_thread::sleep_for(std::chrono::milliseconds(10));

			logQueueHead_++;
			if (logQueueHead_ >= rawLogQueue.max_size())
				logQueueHead_ = 0;
		}

		void WriteRawLogThread()
		{
			bool flushData = false;

			for (;;)
			{
				if (logQueueTail_ != logQueueHead_)
				{
					f_.Write(rawLogQueue[logQueueTail_].c_str(), rawLogQueue[logQueueTail_].size());
					rawLogQueue[logQueueTail_].clear();
					logQueueTail_++;

					if (logQueueTail_ >= rawLogQueue.max_size())
						logQueueTail_ = 0;

					flushData = true;
				}
				else
				{
					if (flushData)
					{
						f_.FlushMetadata();
						flushData = false;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
		}

#elif defined ASYNCHRONOUS_WRITE_LIST

		std::list<string> logMessageQueue;
		std::mutex pendingMessagesMutex_;
		std::condition_variable pendingMessagesCondition_;
		std::unique_ptr<std::thread> writeRawLogThread_;


		void RawLog(const string& what)
		{
			{
				std::lock_guard<std::mutex> lock(pendingMessagesMutex_);
				logMessageQueue.emplace_back(what);
			}
			pendingMessagesCondition_.notify_one();
		}

		void WriteRawLogThread()
		{
			string logMessage;

			for (;;)
			{
				{
					std::unique_lock<std::mutex> lock(pendingMessagesMutex_);

					if (logMessageQueue.empty())
						pendingMessagesCondition_.wait(lock);

					if (logMessageQueue.empty())
						continue;

					logMessage = logMessageQueue.front();
					logMessageQueue.pop_front();
				}

				f_.Write(logMessage.c_str(), logMessage.size());
			}
		}

#else

		void RawLog(const string& what)
		{
			f_.Write(what.c_str(), what.size());
		}

#endif 
		

#ifdef ASYNCHRONOUS_DATETIME

		void UpdateTimeThread()
		{
			for (;;)
			{
				std::unique_lock<std::mutex> unique_lock(updateTimeLock_);

				if (updateTimeCondition_.wait_for(unique_lock, std::chrono::seconds(1)) == std::cv_status::timeout)
				{
					tm* now;
					time_t t = time(NULL);
					now = localtime(&t);

					snprintf(
						timeString_,
						sizeof(timeString_),
						"%04d-%02d-%02d %02d:%02d:%02d.%03d",
						now->tm_year + 1900,
						now->tm_mon + 1,
						now->tm_mday,
						now->tm_hour,
						now->tm_min,
						now->tm_sec,
						0);
				}
			}
		}

#endif


	public:

		BinaryProtocolLogger()
			: lastGlobalHandle_(0)
			, f_(0)
		{

#if defined ASYNCHRONOUS_WRITE_CIRCULAR_QUEUE || defined ASYNCHRONOUS_WRITE_LIST

#ifdef ASYNCHRONOUS_WRITE_CIRCULAR_QUEUE
			logQueueHead_ = 0;
			logQueueTail_ = 0;
#endif
			writeRawLogThread_ = std::make_unique<std::thread>(std::thread(
				[&]()
				{
					this->WriteRawLogThread();
					return;
				}));
#endif
			
#ifdef ASYNCHRONOUS_DATETIME
			updateTimeThread_ = std::make_unique<std::thread>(std::thread(
				[&]()
				{
					this->UpdateTimeThread();
					return;
				}));
#endif
		}


		void Start(const std::string& logFilePath)
		{
			f_.Create(logFilePath.c_str());
			f_.SetPointer(f_.GetFileSize());
		}

		void SerializeTioData(string* lineLog, const TioData& data)
		{
			string serialized;

			switch(data.GetDataType())
			{
			case TioData::None:
				lineLog->append(",n,");
				break;
			case TioData::String:
				lineLog->append(",s");
				lineLog->append(lexical_cast<string>(data.GetSize()));
				lineLog->append(",");
				lineLog->append(data.AsSz(), data.GetSize());
				break;
			case TioData::Int:
				lineLog->append(",i");
				serialized = lexical_cast<string>(data.AsInt());
				lineLog->append(lexical_cast<string>(serialized.length()));
				lineLog->append(",");
				lineLog->append(serialized);
				break;
			case TioData::Double:
				lineLog->append(",d");
				serialized = lexical_cast<string>(data.AsDouble());
				lineLog->append(lexical_cast<string>(serialized.length()));
				lineLog->append(",");
				lineLog->append(serialized);
				break;
			}
		}


		void LogMessage(ITioContainer* container, PR1_MESSAGE* message)
		{
			if(!f_.IsValid())
				return;

			bool b;
			int command;

			b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_COMMAND, &command);

			if(!b)
				return;

			string logLine;
			logLine.reserve(100);

			unsigned currentHandle;
			bool handleAlreadyOpen = false;
			{
				lock_guard_t lock(globalContainerHandleLock_);
				unsigned& globalHandle = globalContainerHandle_[container->GetName()];
				handleAlreadyOpen = globalHandle != 0;
				
				if (globalHandle == 0 && (command == TIO_COMMAND_CREATE || command == TIO_COMMAND_OPEN))
					globalHandle = ++lastGlobalHandle_;
				
				currentHandle = globalHandle;
			}

#ifdef ASYNCHRONOUS_DATETIME
			{
				std::lock_guard<std::mutex> lock(updateTimeLock_);
				logLine.append(timeString_);
			}
#else
			char timeString[64];

			SYSTEMTIME now;
			GetLocalTime(&now);

			_snprintf_s(
				timeString,
				sizeof(timeString),
				_TRUNCATE,
				"%04d-%02d-%02d %02d:%02d:%02d.%03d",
				now.wYear,
				now.wMonth,
				now.wDay,
				now.wHour,
				now.wMinute,
				now.wSecond,
				now.wMilliseconds);

			logLine.append(timeString);
#endif

			bool logCommand = true;

			switch(command)
			{
			case TIO_COMMAND_CREATE:
			case TIO_COMMAND_OPEN:
				if(!handleAlreadyOpen)
				{
					string containerName = container->GetName();
					string containerType = container->GetType();

					logLine.append(",create,");
					logLine.append(lexical_cast<string>(currentHandle));

					logLine.append(",s");
					logLine.append(lexical_cast<string>(containerName.size()));
					logLine.append(",");

					logLine.append(containerName);

					logLine.append(",s");
					logLine.append(lexical_cast<string>(containerType.size()));
					logLine.append(",");

					logLine.append(containerType);
					logLine.append("\n");

					RawLog(logLine);
				}
				return;

				break;
			case TIO_COMMAND_PUSH_BACK:
				logLine.append(",push_back");
				break;
			case TIO_COMMAND_PUSH_FRONT:
				logLine.append(",push_front");
				break;
			case TIO_COMMAND_POP_BACK:
				logLine.append(",pop_back");
				break;
			case TIO_COMMAND_POP_FRONT:
				logLine.append(",pop_front");
				break;
			case TIO_COMMAND_SET:
				logLine.append(",set");
				break;
			case TIO_COMMAND_INSERT:
				logLine.append(",insert");
				break;
			case TIO_COMMAND_DELETE:
				logLine.append(",delete");
				break;
			case TIO_COMMAND_CLEAR:
				logLine.append(",clear");
				break;
			case TIO_COMMAND_PROPSET:
				logLine.append(",propset");
				break;

			case TIO_COMMAND_GROUP_ADD:
				{
					string groupName, containerName;
					bool b;

					b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_GROUP_NAME, &groupName);
					if(!b)
						break;

					b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_CONTAINER_NAME, &containerName);
					if(!b)
						break;

					logLine.append(",group_add,");
					logLine.append(lexical_cast<string>(currentHandle));

					logLine.append(",s");
					logLine.append(lexical_cast<string>(groupName.size()));
					logLine.append(",");
					logLine.append(groupName);

					logLine.append(",s");
					logLine.append(lexical_cast<string>(containerName.size()));
					logLine.append(",");
					logLine.append(containerName);

					logLine.append("\n");

					RawLog(logLine);
				}
				return;

			default:
				logCommand = false;
			}

			if(!logCommand)
				return;

			int handle;
			TioData key, value, metadata;

			Pr1MessageGetHandleKeyValueAndMetadata(message, &handle, &key, &value, &metadata);

			logLine.append(",");
			logLine.append(lexical_cast<string>(currentHandle));

			SerializeTioData(&logLine, key);
			SerializeTioData(&logLine, value);
			SerializeTioData(&logLine, metadata);
			logLine.append("\n");

			RawLog(logLine);
		}
	};
}
