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
#include "TioTcpSession.h"
#include "TioTcpServer.h"

namespace tio
{

	using std::to_string;
	using std::shared_ptr;
	using boost::system::error_code;

	using boost::conversion::try_lexical_convert;

	using boost::lexical_cast;
	using boost::bad_lexical_cast;

	using boost::split;
	using boost::is_any_of;

	using std::tuple;

	using std::setfill;
	using std::setw;
	using std::hex;


	using std::cout;
	using std::endl;

	namespace asio = boost::asio;
	using namespace boost::asio::ip;

	enum ParameterCountCheckType
	{
		exact,
		at_least
	};

	bool CheckParameterCount(Command& cmd, size_t requiredParameters, ParameterCountCheckType type = exact)
	{
		if (type == exact && cmd.GetParameters().size() == requiredParameters)
			return true;
		else if (type == at_least && cmd.GetParameters().size() >= requiredParameters)
			return true;

		return false;
	}

	void TioTcpServer::PostCallback(function<void()> callback)
	{
		io_service_.post(callback);
	}


	void TioTcpServer::OnClientFailed(shared_ptr<TioTcpSession> client, const error_code& err)
	{
		//
		// we'll post, so we'll not invalidate the client now
		// it will probably be called from a TioTcpSession callback
		// and removing now will delete the session's pointer
		//
		PostCallback([this, client]()
		{
			RemoveClient(client);
		});
	}

	void TioTcpServer::RemoveClient(shared_ptr<TioTcpSession> client)
	{
		{
			tio_lock_guard lock(sessionsMutex_);

			SessionsSet::iterator i = sessions_.find(client);

			//
			// it can happen if we receive more than one error notification
			// for this connection
			//
			if (i == sessions_.end())
				return;

			//std::cout << "disconnect" << std::endl;

			sessions_.erase(i);
		}

		//
		// don't need to sync this, shared_ptr is already sync'ed, container are sync'ed too
		// 
		metaContainers_.sessions->Delete(lexical_cast<string>(client->id()), TIONULL, TIONULL);
	}

	void TioTcpServer::PublisherThread()
	{
		size_t previousPendingEventsCount = 0;

		for(;;)
		{
			vector<EventInfo> localEventQueue;

			{
				std::unique_lock<std::mutex> lock(eventQueueMutex_);

				while (eventQueue_.empty())
					eventQueueConditionVar_.wait(lock);

				eventQueue_.swap(localEventQueue);
			}

			auto pendingEventsCount = localEventQueue.size();

			if (pendingEventsCount != previousPendingEventsCount &&
				pendingEventsCount % 5000 == 0)
			{
				previousPendingEventsCount = pendingEventsCount;
				cout << "TOO MUCH PENDING EVENTS: " << pendingEventsCount << endl;
			}

			for (const auto& eventInfo : localEventQueue)
			{
				//
				// we will hold a copy of this vector, so we don't need
				// to keep it locked
				//
				vector<shared_ptr<SubscriptionInfo>> interestedSubscribers;

				{
					std::unique_lock<std::mutex> lock_(subscribersMutex_);

					auto i = subscribers_.find(eventInfo.storageId);

					if (i == subscribers_.end())
						continue;

					interestedSubscribers = i->second;
				}

				if (eventInfo.eventCode == EVENT_CODE_SNAPSHOT_END)
				{
					for (auto& s : interestedSubscribers)
					{
						if (!s->snapshotPending)
							continue;

						//
						// start empty = no snapshot
						//
						if (s->start.empty())
						{
							s->snapshotPending = false;
							continue;
						}

						auto session = s->session.lock();

						if (!session)
							continue;

						int numericStart = 0;

						try_lexical_convert(s->start, numericStart);

						try
						{
							auto resultSet = s->container->Query(numericStart, 0, nullptr);

							ContainerEventCode eventCode =
								IsMapContainer(s->container) ? EVENT_CODE_SET : EVENT_CODE_PUSH_BACK;

							if(!s->groupName.empty())
							{
								session->PublishNewGroupContainer(s->groupName, s->handle, s->container);
							}

							session->SendSubscriptionSnapshot(resultSet, s->handle, eventCode);
						}
						catch (std::exception&)
						{
							BOOST_ASSERT(false && "session is not supposed to throw exceptions");
							cout << "EXCEPTION on SendSubscriptionSnapshot." << endl;
						}

						s->snapshotPending = false;
					}
				}
				else
				{
					for (auto& s : interestedSubscribers)
					{
						if (s->snapshotPending)
							continue;

						auto session = s->session.lock();

						if (!session)
							continue;

						try
						{
							session->PublishEvent(
								s->handle,
								eventInfo.eventCode,
								eventInfo.k,
								eventInfo.v,
								eventInfo.m);
						}
						catch (std::exception&)
						{
							BOOST_ASSERT(false && "session->PublishEvent is not supposed to throw exceptions");
							cout << "EXCEPTION on session->PublishEvent." << endl;
						}
					}
				}
			}
		}
	}

	void TioTcpServer::OnAnyContainerEvent(
		uint64_t storageId,
		ContainerEventCode eventCode,
		const TioData& k, const TioData& v, const TioData& m)
	{
		{
			std::unique_lock<std::mutex> lockSubscribers(subscribersMutex_);
			std::unique_lock<std::mutex> lock(eventQueueMutex_);

			if (subscribers_.find(storageId) == subscribers_.end())
				return;

			eventQueue_.push_back({ storageId, eventCode, k, v, m });
		}

		eventQueueConditionVar_.notify_one();
	}

	void TioTcpServer::SessionAsyncSubscribe(const shared_ptr<SubscriptionInfo>& subscriptionInfo)
	{
		{
			std::unique_lock<std::mutex> lockSubscribers(subscribersMutex_);
			std::unique_lock<std::mutex> lockEvents(eventQueueMutex_);

			EventInfo ei;

			auto storageId = subscriptionInfo->container->GetStorageId();

			ei.eventCode = EVENT_CODE_SNAPSHOT_END;
			ei.storageId = storageId;

			eventQueue_.push_back(ei);
			subscribers_[storageId].push_back(subscriptionInfo);
		}

		eventQueueConditionVar_.notify_one();
	}

	TioTcpServer::~TioTcpServer()
	{
		containerManager_.RemoveSubscriber();
	}
	
	TioTcpServer::TioTcpServer(ContainerManager& containerManager,
		asio::io_service& io_service,
		const endpoint_t& endpoint,
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
		const local_endpoint_t& local_endpoint,
#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS
		const std::string& logFilePath) :
		containerManager_(containerManager),
		acceptor_(io_service, endpoint),
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
		local_acceptor_(io_service, local_endpoint),
#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS
		io_service_(io_service),
		lastSessionID_(0),
		lastQueryID_(0),
		serverPaused_(false)
	{
		containerManager_.SetSubscriber(
			[this](uint64_t storageId, ContainerEventCode eventCode, const TioData& k, const TioData& v, const TioData& m)
			{
				this->OnAnyContainerEvent(storageId, eventCode, k, v, m);
			}
		);

		LoadDispatchMap();
		InitializeMetaContainers();

		if (!logFilePath.empty())
		{
			string finalFilePath = logFilePath;

			auto now = boost::posix_time::second_clock::local_time();

			//
			// Given c:\tio.log this will change it
			// to something like c:\tio_20120526.log
			//

			std::string::reverse_iterator ri;
			std::stringstream dateString;

			dateString << "_" << std::setfill('0') <<
				std::setw(4) << now.date().year() <<
				std::setw(2) << now.date().month() <<
				std::setw(2) << now.date().day();

			ri = std::find(finalFilePath.rbegin(), finalFilePath.rend(), '.');

			if (ri != finalFilePath.rend())
				finalFilePath.insert(finalFilePath.rend() - ri - 1, dateString.str());
			else
				finalFilePath += dateString.str();

			logger_.Start(finalFilePath);
		}
	}

	void TioTcpServer::InitializeMetaContainers()
	{
		//
		// users
		//
		metaContainers_.users = containerManager_.CreateContainer("volatile_map", "__meta__/users");

		try
		{
			string schema = metaContainers_.users->GetProperty("schema");
		}
		catch (std::exception&)
		{
			metaContainers_.users->SetProperty("schema", "password type|password");
		}

		string userContainerType = containerManager_.ResolveAlias("users");

		auth_.SetObjectDefaultRule(userContainerType, "__meta__/users", Auth::deny);
		auth_.AddObjectRule(userContainerType, "__meta__/users", "*", "__admin__", Auth::allow);

		//
		// sessions
		//
		metaContainers_.sessions = containerManager_.CreateContainer("volatile_map", "__meta__/sessions");
	}

	Auth& TioTcpServer::GetAuth()
	{
		return auth_;
	}

	unsigned int TioTcpServer::GenerateSessionId()
	{
		return ++lastSessionID_;
	}

	void TioTcpServer::DoAccept(AcceptType lastAcceptType)
	{
#if BOOST_ASIO_HAS_LOCAL_SOCKETS

		if (lastAcceptType == atNeither || lastAcceptType == atLocal )
		{
			shared_ptr<TioTcpSession> localSession(new TioTcpSession(io_service_, *this, GenerateSessionId()));
			local_acceptor_.async_accept(localSession->GetLocalSocket(),
				[this, localSession](const error_code& err)
				{
					OnAccept(localSession, err, false);
				}
			);
		}
#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS

		if (lastAcceptType == atNeither || lastAcceptType == atRemote)
		{
			shared_ptr<TioTcpSession> remoteSession(new TioTcpSession(io_service_, *this, GenerateSessionId()));
			acceptor_.async_accept(remoteSession->GetSocket(),
				[this, remoteSession](const error_code& err)
				{
					OnAccept(remoteSession, err);
				}
			);
		}
	}

	void TioTcpServer::OnAccept(shared_ptr<TioTcpSession> session, const error_code& err, bool remoteSocket)
	{
		if (!!err)
		{
			throw err;
		}

		if (serverPaused_)
		{
			DoAccept(remoteSocket ? atRemote : atLocal);
			return;
		}

		{
			tio_lock_guard lock(sessionsMutex_);
			sessions_.insert(session);
		}

		metaContainers_.sessions->Insert(lexical_cast<string>(session->id()), TIONULL, TIONULL);

		session->OnAccept(remoteSocket);

		DoAccept(remoteSocket ? atRemote : atLocal);
	}

	shared_ptr<ITioContainer> TioTcpServer::GetContainerAndParametersFromRequest(const PR1_MESSAGE* message, shared_ptr<TioTcpSession> session, TioData* key, TioData* value, TioData* metadata)
	{
		int handle;

		Pr1MessageGetHandleKeyValueAndMetadata(message, &handle, key, value, metadata);

		if (!handle)
			throw std::runtime_error("handle?");

		return session->GetRegisteredContainer(handle);
	}


	string TranslateBinaryCommand(int command)
	{
		if (command == TIO_COMMAND_PING) return "TIO_COMMAND_PING";
		if (command == TIO_COMMAND_OPEN) return "TIO_COMMAND_OPEN";
		if (command == TIO_COMMAND_CREATE) return "TIO_COMMAND_CREATE";
		if (command == TIO_COMMAND_CLOSE) return "TIO_COMMAND_CLOSE";
		if (command == TIO_COMMAND_SET) return "TIO_COMMAND_SET";
		if (command == TIO_COMMAND_INSERT) return "TIO_COMMAND_INSERT";
		if (command == TIO_COMMAND_DELETE) return "TIO_COMMAND_DELETE";
		if (command == TIO_COMMAND_PUSH_BACK) return "TIO_COMMAND_PUSH_BACK";
		if (command == TIO_COMMAND_PUSH_FRONT) return "TIO_COMMAND_PUSH_FRONT";
		if (command == TIO_COMMAND_POP_BACK) return "TIO_COMMAND_POP_BACK";
		if (command == TIO_COMMAND_POP_FRONT) return "TIO_COMMAND_POP_FRONT";
		if (command == TIO_COMMAND_CLEAR) return "TIO_COMMAND_CLEAR";
		if (command == TIO_COMMAND_COUNT) return "TIO_COMMAND_COUNT";
		if (command == TIO_COMMAND_GET) return "TIO_COMMAND_GET";
		if (command == TIO_COMMAND_SUBSCRIBE) return "TIO_COMMAND_SUBSCRIBE";
		if (command == TIO_COMMAND_UNSUBSCRIBE) return "TIO_COMMAND_UNSUBSCRIBE";
		if (command == TIO_COMMAND_QUERY) return "TIO_COMMAND_QUERY";
		if (command == TIO_COMMAND_PROPGET) return "TIO_COMMAND_PROPGET ";
		if (command == TIO_COMMAND_PROPSET) return "TIO_COMMAND_PROPSET";

		return "UNKNOWN";
	}

	string HttpGetStatusMessage(int status)
	{
		switch (status)
		{
		case 200: return "OK";
		case 404: return "Not Found";
		case 400: return "Bad Request";
		case 500: return "Internal Server Error";
		default: return "Nevermind";
		}
	}

	void SendHttpResponse(
		const shared_ptr<TioTcpSession>& session,
		const map<string, string>& headers,
		int status,
		const string& message)
	{
		//
		// TODO: adapt format to "Accept" header (json, xml, etc)
		//
		session->SendHttpResponseAndClose(status, HttpGetStatusMessage(status), "text/plain", nullptr, message);
	}

	struct TIO_HTTP_RESPONSE
	{
		map<string, string> headers;
		int status;
		string mimeType;
		string body;

		TIO_HTTP_RESPONSE() : status(500)
		{}
	};

	static const string HTML_HEADER = R"(
<!DOCTYPE html>
<html lang="en">
 <head>
 <link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css" integrity="sha384-BVYiiSIFeK1dGmJRAkycuHAHRg32OmUcww7on3RYdg4Va+PmSTsz/K68vbdEjh4u" crossorigin="anonymous">
 <script src="https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js" integrity="sha384-Tc5IQib027qvyjSMfHjOMaLkfuWVxZxUPnCJA7l2mCWNIpG9mGCD8wGNIcPD7Txa" crossorigin="anonymous"></script>
 <meta name="viewport" content="width=device-width, initial-scale=1">
</head><body><div class="container-fluid">
		)";

	static const string HTML_FOOTER = R"(
	</div></body></html>
		)";

	TIO_HTTP_RESPONSE CreateContainerHttpResponse(const map<string, string>& headers, const shared_ptr<ITioContainer>& container)
	{
		TIO_HTTP_RESPONSE ret;

		auto i = headers.find("Accept");

		if (i == headers.end() ||
			i->second.find("application/json") != string::npos ||
			i->second == "*/*")
		{
			ret.body.clear();
			ret.body += "{\""  "_meta"   "\":{";
			ret.body += "\""   "container_name"   "\":";
			ret.body += "\"" + container->GetName() + "\""  ",";
			ret.body += "\""   "container_type"   "\":";
			ret.body += "\"" + container->GetType() + "\"";
			ret.body += "}"   ",";
			ret.body += "\""  "value"   "\":[";

			auto resultSet = container->Query(0, 0, nullptr);
			TioData k, v, m;
			while (resultSet->GetRecord(&k, &v, &m))
			{
				ret.body += "{\"key\":\"";
				ret.body += k.AsString();
				ret.body += "\",\"value\":\"";
				ret.body += v.AsString();
				ret.body += "\",\"metadata\":\"";
				ret.body += m.AsString();
				ret.body += "\"},";
				resultSet->MoveNext();
			}

			// remove last comma
			ret.body.resize(ret.body.size() - 1);

			ret.body += "]}";

			ret.mimeType = "application/json";
			ret.status = 200;
		}
		else if (i->second.find("text/html") != string::npos)
		{
			ret.body = HTML_HEADER;
			ret.body += R"(
				<div class="row">
				  <div class="col-sm-4">)";

			ret.body += "<h4>";
			ret.body += container->GetName();
			ret.body += "</h4>";
			ret.body += R"(
                    <table class="table table-bordered">
					  <thead>
						<tr><td>key</td><td>value</td><td>metadata</td></tr>
					  </thead>
					<tbody>
			)";

			auto resultSet = container->Query(0, 0, nullptr);

			TioData k, v, m;

			while (resultSet->GetRecord(&k, &v, &m))
			{
				ret.body += "<tr>";
				ret.body += "<td>";
				ret.body += k.AsString();
				ret.body += "</td><td>";
				ret.body += v.AsString();
				ret.body += "</td><td>";
				ret.body += m.AsString();
				ret.body += "</td></tr>";

				resultSet->MoveNext();
			}

			ret.body += "</tbody></table></div></div>";
			ret.body += HTML_FOOTER;

			ret.mimeType = "text/html";
			ret.status = 200;
		}
		else
		{
			ret.status = 400;
		}

		return ret;
	}


	void TioTcpServer::OnHttpCommand(
		const HTTP_MESSAGE& httpMessage,
		const shared_ptr<TioTcpSession>& session)
	{
		string normalizedPath = httpMessage.url;

		if (!normalizedPath.empty() && normalizedPath[0] == '/')
		{
			normalizedPath.erase(normalizedPath.begin());
		}

		try
		{
			if (httpMessage.method == "GET")
			{
				if (normalizedPath.empty())
				{
					session->SendHttpResponseAndClose(
						200,
						"OK",
						"text/plain",
						nullptr,
						"hello from tiodb. unix time=" + to_string(time(nullptr)));
				}
				else
				{
					shared_ptr<ITioContainer> container;

					try
					{
						container = containerManager_.OpenContainer("", normalizedPath);
					}
					catch (std::runtime_error&)
					{
						SendHttpResponse(session, httpMessage.headers, 404, "container not found");
					}

					auto response = CreateContainerHttpResponse(httpMessage.headers, container);

					session->SendHttpResponseAndClose(
						response.status,
						HttpGetStatusMessage(response.status),
						response.mimeType,
						&response.headers,
						response.body);
				}
			}
			else
			{
				SendHttpResponse(session, httpMessage.headers, 400, "Bad Request");
			}
		}
		catch (std::exception& ex)
		{
			SendHttpResponse(session, httpMessage.headers, 400, "Bad Request");

			SendHttpResponse(session, httpMessage.headers, 500, string("Internal Server Error : ") + ex.what());
		}
	}


	void TioTcpServer::OnBinaryCommand(shared_ptr<TioTcpSession> session, PR1_MESSAGE* message)
	{
		bool b;
		int command;

		pr1_message_parse(message);

		b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_COMMAND, &command);

		if (!b)
		{
			session->SendBinaryErrorAnswer(TIO_ERROR_MISSING_PARAMETER, "missing MESSAGE_FIELD_ID_COMMAND");
			return;
		}

		try
		{
#if 0
			stringstream str;
			string parameter;
			int handle;

			str << session.get() << " BINARY COMMAND: " << TranslateBinaryCommand(command);

			if (Pr1MessageGetField(message, MESSAGE_FIELD_ID_HANDLE, &handle))
				str << ", handle=" << handle;

			if (Pr1MessageGetField(message, MESSAGE_FIELD_ID_NAME, &parameter))
				str << ", name=" << parameter;

			if (Pr1MessageGetField(message, MESSAGE_FIELD_ID_KEY, &parameter))
				str << ", key=" << parameter;

			if (Pr1MessageGetField(message, MESSAGE_FIELD_ID_VALUE, &parameter))
				str << ", value=" << parameter;

			cout << str.str() << endl;
#endif

			switch (command)
			{
			case TIO_COMMAND_PING:
			{
				string payload;
				b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_VALUE, &payload);

				if (!b)
				{
					session->SendBinaryErrorAnswer(TIO_ERROR_MISSING_PARAMETER, "missing MESSAGE_FIELD_ID_VALUE");
					break;
				}

				shared_ptr<PR1_MESSAGE> answer = Pr1CreateMessage();

				pr1_message_add_field_int(answer.get(), MESSAGE_FIELD_ID_COMMAND, TIO_COMMAND_ANSWER);
				pr1_message_add_field_string(answer.get(), MESSAGE_FIELD_ID_VALUE, payload.c_str());


				session->SendBinaryMessage(answer);
			}
			break;
			case TIO_COMMAND_OPEN:
			case TIO_COMMAND_CREATE:
			{
				string name, type;
				b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_NAME, &name);

				if (!b)
				{
					session->SendBinaryErrorAnswer(TIO_ERROR_MISSING_PARAMETER, "missing container name (MESSAGE_FIELD_ID_NAME)");
					break;
				}

				Pr1MessageGetField(message, MESSAGE_FIELD_ID_TYPE, &type);

				shared_ptr<ITioContainer> container;

				if (command == TIO_COMMAND_CREATE)
				{
					if (type.empty())
					{
						session->SendBinaryErrorAnswer(TIO_ERROR_MISSING_PARAMETER, "must inform type on container creation (MESSAGE_FIELD_ID_TYPE)");
						break;
					}

					container = containerManager_.CreateContainer(type, name);
				}
				else if (command == TIO_COMMAND_OPEN)
				{
					container = containerManager_.OpenContainer(type, name);
				}

				unsigned int handle = session->RegisterContainer(name, container);

				shared_ptr<PR1_MESSAGE> answer = Pr1CreateMessage();

				pr1_message_add_field_int(answer.get(), MESSAGE_FIELD_ID_COMMAND, TIO_COMMAND_ANSWER);
				pr1_message_add_field_int(answer.get(), MESSAGE_FIELD_ID_HANDLE, handle);

				logger_.LogMessage(container.get(), message);

				session->SendBinaryMessage(answer);
			}
			break;
			case TIO_COMMAND_CLOSE:
			{
				bool b;
				int handle;
				string start;
				b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_HANDLE, &handle);

				if (!b)
				{
					session->SendBinaryErrorAnswer(TIO_ERROR_MISSING_PARAMETER, "missing handle (MESSAGE_FIELD_ID_HANDLE)");
					break;
				}

				session->CloseContainerHandle(handle);

				session->SendBinaryAnswer();
			}
			break;

			case TIO_COMMAND_GET:
			case TIO_COMMAND_PROPGET:
			{
				TioData searchKey;

				shared_ptr<ITioContainer> container = GetContainerAndParametersFromRequest(message, session, &searchKey, NULL, NULL);

				TioData key, value, metadata;

				if (command == TIO_COMMAND_GET)
					container->GetRecord(searchKey, &key, &value, &metadata);
				else if (command == TIO_COMMAND_PROPGET)
				{
					value = container->GetProperty(searchKey.AsSz());
					key = searchKey;
				}
				else
					throw std::runtime_error("INTERNAL ERROR");


				session->SendBinaryAnswer(&key, &value, &metadata);
			}
			break;

			case TIO_COMMAND_POP_FRONT:
			case TIO_COMMAND_POP_BACK:
			{
				shared_ptr<ITioContainer> container = GetContainerAndParametersFromRequest(message, session, NULL, NULL, NULL);

				TioData key, value, metadata;

				if (command == TIO_COMMAND_POP_BACK)
					container->PopBack(&key, &value, &metadata);
				else if (command == TIO_COMMAND_POP_FRONT)
					container->PopFront(&key, &value, &metadata);
				else
					throw std::runtime_error("INTERNAL ERROR");

				logger_.LogMessage(container.get(), message);

				session->SendBinaryAnswer(&key, &value, &metadata);
			}
			break;

			case TIO_COMMAND_PUSH_BACK:
			case TIO_COMMAND_PUSH_FRONT:
			case TIO_COMMAND_SET:
			case TIO_COMMAND_INSERT:
			case TIO_COMMAND_DELETE:
			case TIO_COMMAND_CLEAR:
			case TIO_COMMAND_PROPSET:
			{
				TioData key, value, metadata;

				shared_ptr<ITioContainer> container = GetContainerAndParametersFromRequest(message, session, &key, &value, &metadata);

				if (command == TIO_COMMAND_PUSH_BACK)
					container->PushBack(key, value, metadata);
				else if (command == TIO_COMMAND_PUSH_FRONT)
					container->PushFront(key, value, metadata);
				else if (command == TIO_COMMAND_SET)
					container->Set(key, value, metadata);
				else if (command == TIO_COMMAND_INSERT)
					container->Insert(key, value, metadata);
				else if (command == TIO_COMMAND_DELETE)
					container->Delete(key, value, metadata);
				else if (command == TIO_COMMAND_CLEAR)
					container->Clear();
				else if (command == TIO_COMMAND_PROPSET)
				{
					if (key.GetDataType() != TioData::String || value.GetDataType() != TioData::String)
						throw std::runtime_error("properties key and value should be strings");

					container->SetProperty(key.AsSz(), value.AsSz());
				}
				else
					throw std::runtime_error("INTERNAL ERROR");

				logger_.LogMessage(container.get(), message);

				session->SendBinaryAnswer();
			}
			break;

			case TIO_COMMAND_COUNT:
			{
				shared_ptr<ITioContainer> container = GetContainerAndParametersFromRequest(message, session, NULL, NULL, NULL);

				int count = container->GetRecordCount();

				shared_ptr<PR1_MESSAGE> answer = Pr1CreateAnswerMessage(NULL, NULL, NULL);
				pr1_message_add_field_int(answer.get(), MESSAGE_FIELD_ID_VALUE, count);

				session->SendBinaryMessage(answer);
			}
			break;

			case TIO_COMMAND_QUERY:
			{
				int start, end, maxRecords;

				shared_ptr<ITioContainer> container = GetContainerAndParametersFromRequest(message, session, NULL, NULL, NULL);

				if (!Pr1MessageGetField(message, MESSAGE_FIELD_ID_START_RECORD, &start))
					start = 0;

				if (!Pr1MessageGetField(message, MESSAGE_FIELD_ID_END, &end))
					end = 0;

				maxRecords = end;

				function<bool(const TioData& key)> filterFunction;
				shared_ptr<boost::regex> e;

				string queryExpression;

				Pr1MessageGetField(message, MESSAGE_FIELD_ID_QUERY_EXPRESSION, &queryExpression);

				if (!queryExpression.empty())
				{
					e.reset(new boost::regex(queryExpression));

					filterFunction = [e](const TioData& key) -> bool
					{
						if (key.GetDataType() != TioData::String)
							return false;

						return regex_match(key.AsSz(), *e);
					};

					end = start = 0;
				}

				shared_ptr<ITioResultSet> resultSet = container->Query(start, end, TIONULL);

				session->SendBinaryResultSet(resultSet, CreateNewQueryId(), filterFunction, maxRecords);
			}
			break;

			case TIO_COMMAND_GROUP_ADD:
			{
				string groupName, containerName;
				bool b;

				b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_GROUP_NAME, &groupName);

				if (!b)
				{
					session->SendBinaryErrorAnswer(TIO_ERROR_MISSING_PARAMETER, "missing handle (MESSAGE_FIELD_ID_GROUP_NAME)");
					break;
				}

				b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_CONTAINER_NAME, &containerName);

				if (!b)
				{
					session->SendBinaryErrorAnswer(TIO_ERROR_MISSING_PARAMETER, "missing handle (MESSAGE_FIELD_ID_CONTAINER_NAME)");
					break;
				}

				shared_ptr<ITioContainer> container;

				container = containerManager_.OpenContainer("", containerName);

				groupManager_.AddContainer(&containerManager_, groupName, container);

				session->SendBinaryAnswer();

				for(auto& weakSession: groupSubscriptions_[groupName])
				{
					auto subscriberSession = weakSession.lock();

					if(!subscriberSession || !subscriberSession->IsValid())
					{
						continue;
					}
				
					unsigned handle = subscriberSession->RegisterNewGroupContainer(groupName, containerName, container);

					SessionAsyncSubscribe(make_shared<SubscriptionInfo>(groupName, container, handle, subscriberSession, "0"));
				}

				logger_.LogMessage(container.get(), message);
			}
			break;

			case TIO_COMMAND_GROUP_SUBSCRIBE:
			{
				string groupName, start;
				bool b;

				b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_GROUP_NAME, &groupName);

				if (!b)
				{
					session->SendBinaryErrorAnswer(TIO_ERROR_MISSING_PARAMETER, "missing handle (MESSAGE_FIELD_ID_GROUP_NAME)");
					break;
				}

				Pr1MessageGetField(message, MESSAGE_FIELD_ID_START_RECORD, &start);

				auto groupContainers = groupManager_.GetGroupContainers(groupName);

				groupSubscriptions_[groupName].push_back(session);

				session->SendBinaryAnswer();

				for(auto& p : groupContainers)
				{
					const string& containerName = p.first;
					const shared_ptr<ITioContainer>& container = p.second;

					unsigned handle = session->RegisterNewGroupContainer(groupName, containerName, container);

					SessionAsyncSubscribe(make_shared<SubscriptionInfo>(groupName, container, handle, session, start));
				}
			}
			break;

			case TIO_COMMAND_SUBSCRIBE:
			{
				bool b;
				int handle;
				string start_string;
				int start_int;
				b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_HANDLE, &handle);

				if (!b)
				{
					session->SendBinaryErrorAnswer(TIO_ERROR_MISSING_PARAMETER, "missing handle (MESSAGE_FIELD_ID_HANDLE)");
					break;
				}

				b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_KEY, &start_string);
				if (!b)
					if (Pr1MessageGetField(message, MESSAGE_FIELD_ID_KEY, &start_int))
						start_string = lexical_cast<string>(start_int);

				auto container = GetContainerAndParametersFromRequest(message, session, nullptr, nullptr, nullptr);

				session->SendBinaryAnswer();

				SessionAsyncSubscribe(make_shared<SubscriptionInfo>("", container, handle, session, start_string));
			}
			break;

			case TIO_COMMAND_UNSUBSCRIBE:
			{
				bool b;
				int handle;
				b = Pr1MessageGetField(message, MESSAGE_FIELD_ID_HANDLE, &handle);

				if (!b)
				{
					session->SendBinaryErrorAnswer(TIO_ERROR_MISSING_PARAMETER, "missing handle (MESSAGE_FIELD_ID_HANDLE)");
					break;
				}

				session->Unsubscribe(handle);

				session->SendBinaryAnswer();
			}
			break;

			default:
				session->SendBinaryErrorAnswer(TIO_ERROR_PROTOCOL, "invalid command");
				break;
			}
		}
		catch (std::exception& ex)
		{
			session->SendBinaryErrorAnswer(TIO_ERROR_PROTOCOL, ex.what());
		}
	}


	void TioTcpServer::OnTextCommand(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		CommandFunctionMap::iterator i = dispatchMap_.find(cmd.GetCommand());

		if (i != dispatchMap_.end())
		{
			CommandCallbackFunction& f = i->second;

			try
			{
				(this->*f)(cmd, answer, moreDataSize, session);
			}
			catch (std::exception& ex)
			{
				BOOST_ASSERT(false && "handler functions not supposed to throw exceptions");
				MakeAnswer(error, answer, string("internal error, handler functions not supposed to throw exceptions but I've got this one: ") + ex.what());
			}
		}
		else
		{
			MakeAnswer(error, answer, "invalid command");
		}
	}


	//
	// commands
	//
	void TioTcpServer::LoadDispatchMap()
	{
		dispatchMap_["ping"] = &TioTcpServer::OnCommand_Ping;
		dispatchMap_["ver"] = &TioTcpServer::OnCommand_Version;

		dispatchMap_["create"] = &TioTcpServer::OnCommand_CreateContainer_OpenContainer;
		dispatchMap_["open"] = &TioTcpServer::OnCommand_CreateContainer_OpenContainer;
		dispatchMap_["close"] = &TioTcpServer::OnCommand_CloseContainer;

		dispatchMap_["delete_container"] = &TioTcpServer::OnCommand_DeleteContainer;

		dispatchMap_["list_handles"] = &TioTcpServer::OnCommand_ListHandles;

		dispatchMap_["push_back"] = &TioTcpServer::OnAnyDataCommand;
		dispatchMap_["push_front"] = &TioTcpServer::OnAnyDataCommand;

		dispatchMap_["pop_back"] = &TioTcpServer::OnCommand_Pop;
		dispatchMap_["pop_front"] = &TioTcpServer::OnCommand_Pop;

		dispatchMap_["modify"] = &TioTcpServer::OnModify;

		dispatchMap_["set"] = &TioTcpServer::OnAnyDataCommand;
		dispatchMap_["insert"] = &TioTcpServer::OnAnyDataCommand;
		dispatchMap_["delete"] = &TioTcpServer::OnAnyDataCommand;
		dispatchMap_["clear"] = &TioTcpServer::OnCommand_Clear;

		dispatchMap_["get_property"] = &TioTcpServer::OnAnyDataCommand;
		dispatchMap_["set_property"] = &TioTcpServer::OnAnyDataCommand;

		dispatchMap_["get"] = &TioTcpServer::OnAnyDataCommand;

		dispatchMap_["get_count"] = &TioTcpServer::OnCommand_GetRecordCount;

		dispatchMap_["subscribe"] = &TioTcpServer::OnCommand_SubscribeUnsubscribe;
		dispatchMap_["unsubscribe"] = &TioTcpServer::OnCommand_SubscribeUnsubscribe;

		dispatchMap_["command"] = &TioTcpServer::OnCommand_CustomCommand;

		dispatchMap_["auth"] = &TioTcpServer::OnCommand_Auth;

		dispatchMap_["pause"] = &TioTcpServer::OnCommand_PauseResume;
		dispatchMap_["resume"] = &TioTcpServer::OnCommand_PauseResume;

		dispatchMap_["set_permission"] = &TioTcpServer::OnCommand_SetPermission;

		dispatchMap_["query"] = &TioTcpServer::OnCommand_Query;

		dispatchMap_["queryex"] = &TioTcpServer::OnCommand_QueryEx;

		dispatchMap_["group_add"] = &TioTcpServer::OnCommand_GroupAdd;
		dispatchMap_["group_subscribe"] = &TioTcpServer::OnCommand_GroupSubscribe;

	}

	std::string Serialize(const std::list<const TioData*>& fields)
	{
		stringstream header, values;

		// message identification
		header << "X1";

		// field count
		header << setfill('0') << setw(4) << hex << fields.size() << "C";

		BOOST_FOREACH(const TioData* field, fields)
		{
			unsigned int currentStreamSize = values.str().size();
			unsigned int size = 0;
			char dataTypeCode = 'X';

			if (field && field->GetDataType() != TioData::None)
			{
				switch (field->GetDataType())
				{
				case TioData::String:
					values << field->AsSz();
					dataTypeCode = 'S';
					break;
				case TioData::Int:
					values << field->AsInt();
					dataTypeCode = 'I';
					break;
				case TioData::Double:
					values << field->AsDouble();
					dataTypeCode = 'D';
					break;
				}

				size = values.str().size() - currentStreamSize;

				values << " ";
			}
			else
			{
				size = 0;
			}

			header << setfill('0') << setw(4) << hex << size << dataTypeCode;
		}

		header << " " << values.str();

		return header.str();
	}


	void TioTcpServer::OnCommand_QueryEx(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		if (!CheckParameterCount(cmd, 2, at_least))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		shared_ptr<ITioContainer> container;
		unsigned int handle;

		try
		{
			handle = lexical_cast<unsigned int>(cmd.GetParameters()[0]);
			container = session->GetRegisteredContainer(handle);
		}
		catch (std::exception&)
		{
			MakeAnswer(error, answer, "invalid handle");
			return;
		}

		unsigned maxRecords = 0xFFFFFFFF;

		if (cmd.GetParameters().size() > 2)
		{
			try
			{
				maxRecords = lexical_cast<unsigned>(cmd.GetParameters()[2]);
			}
			catch (std::exception&)
			{
				MakeAnswer(error, answer, "invalid record limit parameter");
				return;
			}
		}

		string queryRegex = cmd.GetParameters()[1];

		const boost::regex e(queryRegex);

		shared_ptr<ITioResultSet> resultSet = container->Query(0, 0, TioData());

		unsigned queryId = CreateNewQueryId();

		session->SendTextResultSetStart(queryId);

		for (unsigned a = 0; a < maxRecords; )
		{
			TioData key, value, metadata;

			bool b = resultSet->GetRecord(&key, &value, &metadata);

			if (!b)
				break;

			b = resultSet->MoveNext();

			if (key.GetDataType() != TioData::String)
				continue;

			if (regex_match(key.AsSz(), e))
			{
				session->SendTextResultSetItem(queryId, key, value, metadata);
				a++;
			}

			if (!b) break;
		}

		session->SendTextResultSetEnd(queryId);

		return;

	}

	void TioTcpServer::OnCommand_Query(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		if (!CheckParameterCount(cmd, 1, at_least))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		shared_ptr<ITioContainer> container;
		unsigned int handle;

		try
		{
			handle = lexical_cast<unsigned int>(cmd.GetParameters()[0]);
			container = session->GetRegisteredContainer(handle);
		}
		catch (std::exception&)
		{
			MakeAnswer(error, answer, "invalid handle");
			return;
		}

		int start = 0, end = 0;
		TioData query;

		try
		{
			if (cmd.GetParameters().size() > 1)
				start = lexical_cast<int>(cmd.GetParameters()[1]);

			if (cmd.GetParameters().size() > 2)
				end = lexical_cast<int>(cmd.GetParameters()[2]);
		}
		catch (bad_lexical_cast&)
		{
			MakeAnswer(error, answer, "invalid parameter");
			return;
		}

		try
		{
			session->SendTextResultSet(container->Query(start, end, query), CreateNewQueryId());
		}
		catch (std::exception& ex)
		{
			MakeAnswer(error, answer, ex.what());
			return;
		}
	}

	void TioTcpServer::OnCommand_GroupAdd(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		//
		// group_add group_name container_name
		//
		if (!CheckParameterCount(cmd, 2, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		const string& groupName = cmd.GetParameters()[0];
		const string& containerName = cmd.GetParameters()[1];

		try
		{
			shared_ptr<ITioContainer> container;

			container = containerManager_.OpenContainer("", containerName);

			groupManager_.AddContainer(&containerManager_, groupName, container);

			session->SendAnswer(GenerateSimpleOkAnswer());

			for(auto& weakSession: groupSubscriptions_[groupName])
			{
				auto subscriberSession = weakSession.lock();

				if(!subscriberSession || !subscriberSession->IsValid())
				{
					continue;
				}

				auto handle = subscriberSession->RegisterNewGroupContainer(groupName, containerName, container);

				SessionAsyncSubscribe(make_shared<SubscriptionInfo>(groupName, container, handle, subscriberSession, "0"));
			}
		}
		catch (std::exception& ex)
		{
			MakeAnswer(error, answer, ex.what());
		}

	}

	void TioTcpServer::OnCommand_GroupSubscribe(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		//
		// group_subscribe group_name [start]
		//
		if (!CheckParameterCount(cmd, 1, exact) &&
			!CheckParameterCount(cmd, 2, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		const string& groupName = cmd.GetParameters()[0];
		string start;

		if (cmd.GetParameters().size() == 2)
		{
			MakeAnswer(error, answer, "start parameters not supported");
			return;
		}

		try
		{
			auto containers = groupManager_.GetGroupContainers(groupName);

			groupSubscriptions_[groupName].push_back(session);

			session->SendAnswer(GenerateSimpleOkAnswer());

			for(auto& p : containers)
			{
				const string& containerName = p.first;
				const shared_ptr<ITioContainer>& container = p.second;

				auto handle = session->RegisterNewGroupContainer(groupName, container->GetName(), container);				

				SessionAsyncSubscribe(make_shared<SubscriptionInfo>(groupName, container, handle, session, "0"));
			}
		}
		catch (std::exception& ex)
		{
			MakeAnswer(error, answer, ex.what());
		}
	}


	unsigned TioTcpServer::CreateNewQueryId()
	{
		return ++lastQueryID_;
	}

	void TioTcpServer::OnCommand_CustomCommand(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		if (!CheckParameterCount(cmd, 2, at_least))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		shared_ptr<ITioContainer> container;

		try
		{
			unsigned int handle;

			handle = lexical_cast<unsigned int>(cmd.GetParameters()[0]);
			container = session->GetRegisteredContainer(handle);
		}
		catch (std::exception&)
		{
			MakeAnswer(error, answer, "invalid handle");
			return;
		}

		string command;

		for (Command::Parameters::const_iterator i = cmd.GetParameters().begin() + 1; i != cmd.GetParameters().end(); ++i)
			command += *i + " ";

		command.erase(command.end() - 1);

		string result = container->Command(command);

		MakeAnswer(success, answer, result);

	}


	void TioTcpServer::OnCommand_Clear(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		if (!CheckParameterCount(cmd, 1, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		shared_ptr<ITioContainer> container;

		try
		{
			unsigned int handle;

			handle = lexical_cast<unsigned int>(cmd.GetParameters()[0]);
			container = session->GetRegisteredContainer(handle);
		}
		catch (std::exception&)
		{
			MakeAnswer(error, answer, "invalid handle");
			return;
		}

		container->Clear();

		MakeAnswer(success, answer);
	}


	void TioTcpServer::OnCommand_GetRecordCount(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		if (!CheckParameterCount(cmd, 1, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		shared_ptr<ITioContainer> container;

		try
		{
			unsigned int handle;

			handle = lexical_cast<unsigned int>(cmd.GetParameters()[0]);
			container = session->GetRegisteredContainer(handle);
		}
		catch (std::exception&)
		{
			MakeAnswer(error, answer, "invalid handle");
			return;
		}

		MakeAnswer(success, answer, "count", lexical_cast<string>(container->GetRecordCount()).c_str());
	}

	void TioTcpServer::OnCommand_SubscribeUnsubscribe(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		if (!CheckParameterCount(cmd, 1, exact) &&
			!CheckParameterCount(cmd, 2, exact) &&
			!CheckParameterCount(cmd, 3, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		unsigned int handle;
		int filterEnd = -1;

		try
		{
			handle = lexical_cast<unsigned int>(cmd.GetParameters()[0]);
		}
		catch (boost::bad_lexical_cast&)
		{
			MakeAnswer(error, answer, "invalid handle");
			return;
		}

		try
		{
			string containerName, containerType;

			auto container = session->GetRegisteredContainer(handle, &containerName, &containerType);

			if (!CheckObjectAccess(containerType, containerName, cmd.GetCommand(), answer, session))
				return;

			string start;

			if (cmd.GetParameters().size() >= 2)
			{
				start = cmd.GetParameters()[1];

				if (start == "__none__")
					start.clear();
			}

			if (cmd.GetParameters().size() == 3)
			{
				filterEnd = lexical_cast<int>(cmd.GetParameters()[2]);
			}

			if (cmd.GetCommand() == "subscribe")
			{
				SessionAsyncSubscribe(make_shared<SubscriptionInfo>("", container, handle, session, start));

				MakeAnswer(success, answer);
			}
			else if (cmd.GetCommand() == "unsubscribe")
			{
				session->Unsubscribe(handle);
				MakeAnswer(success, answer);
			}
			else
				BOOST_ASSERT(false && "only subscribe and unsubscribe, please");
		}
		catch (std::exception& e)
		{
			MakeAnswer(error, answer, e.what());
			return;
		}
	}

	void TioTcpServer::OnCommand_Ping(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		MakeAnswer(cmd.GetParameters().begin(), cmd.GetParameters().end(), success, answer, "pong");
	}

	void TioTcpServer::OnCommand_Version(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		MakeAnswer(success, answer, "0.1");
	}

	bool TioTcpServer::CheckCommandAccess(const string& command, ostream& answer, shared_ptr<TioTcpSession> session)
	{
		if (auth_.CheckCommandAccess(command, session->GetTokens()) == Auth::allow)
			return true;

		MakeAnswer(error, answer, "access denied");
		return false;
	}

	bool TioTcpServer::CheckObjectAccess(const string& objectType, const string& objectName, const string& command, ostream& answer, shared_ptr<TioTcpSession> session)
	{
		if (auth_.CheckObjectAccess(objectType, objectName, command, session->GetTokens()) == Auth::allow)
			return true;

		MakeAnswer(error, answer, "access denied");
		return false;
	}

	string TioTcpServer::GetConfigValue(const string& key)
	{
		if (boost::to_lower_copy(key) == "userdbtype")
			return "logdb/map";

		return string();
	}

	void TioTcpServer::OnCommand_SetPermission(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		//
		// examples:
		// set_permission persistent_map my_map push_back allow user_name
		// set_permission volatile_vector object_name * allow user_name
		//
		if (!CheckParameterCount(cmd, 4, exact) && !CheckParameterCount(cmd, 5, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		string objectType = cmd.GetParameters()[0];
		const string& objectName = cmd.GetParameters()[1];
		const string& command = cmd.GetParameters()[2]; // command_name or *
		const string& allowOrDeny = cmd.GetParameters()[3];

		objectType = containerManager_.ResolveAlias(objectType);

		Auth::RuleResult ruleResult;

		if (allowOrDeny == "allow")
			ruleResult = Auth::allow;
		else if (allowOrDeny == "deny")
			ruleResult = Auth::deny;
		else
		{
			MakeAnswer(error, answer, "invalid permission type");
			return;
		}

		if (!containerManager_.Exists(objectType, objectName))
		{
			MakeAnswer(error, answer, "no such object");
			return;
		}

		//
		// the user_name can only be ommited when sending the __default__ rule
		//
		if (cmd.GetParameters().size() == 5)
		{
			const string& token = cmd.GetParameters()[4];

			auth_.AddObjectRule(objectType, objectName, command, token, ruleResult);
		}
		else
		{
			if (command != "__default__")
			{
				MakeAnswer(error, answer, "no user specified");
				return;
			}

			auth_.SetObjectDefaultRule(objectType, objectName, ruleResult);
		}

		MakeAnswer(success, answer);
	}

	void TioTcpServer::OnCommand_PauseResume(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		if (!CheckParameterCount(cmd, 0, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		//
		// This command will pause the server and disconnect everyone, except
		// the current connection. It's useful when you want to load some
		// data to the server without having everyone receiving the events
		//
		if (cmd.GetCommand() == "pause")
		{
			for (auto i = sessions_.begin(); i != sessions_.end(); ++i)
			{
				//
				// We will not drop the current connection
				//
				if ((*i) == session)
					continue;

				(*i)->InvalidateConnection(error_code());
			}

			serverPaused_ = true;
		}
		else if (cmd.GetCommand() == "resume")
		{
			serverPaused_ = false;
		}

		MakeAnswer(success, answer);

	}

	void TioTcpServer::OnCommand_Auth(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		if (!CheckParameterCount(cmd, 3, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		const string& token = cmd.GetParameters()[0];
		const string& passwordType = cmd.GetParameters()[1];
		const string& password = cmd.GetParameters()[2];

		//
		// only clean password now
		//
		if (passwordType != "clean")
		{
			MakeAnswer(error, answer, "unsupported password type");
			return;
		}

		try
		{
			TioData value;

			metaContainers_.users->GetRecord(token, NULL, &value, NULL);

			if (TioData(password) == value)
			{
				session->AddToken(token);
				MakeAnswer(success, answer);
			}
			else
				MakeAnswer(error, answer, "invalid password");
		}
		catch (std::exception&)
		{
			MakeAnswer(error, answer, "error validating password");
		}
	}

	void TioTcpServer::OnCommand_DeleteContainer(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		if (!CheckParameterCount(cmd, 2, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		const string& containerType = cmd.GetParameters()[0];
		const string& containerName = cmd.GetParameters()[1];

		if (!CheckObjectAccess(containerType, containerName, cmd.GetCommand(), answer, session))
			return;

		try
		{
			containerManager_.DeleteContainer(containerType, containerName);

			MakeAnswer(success, answer);
		}
		catch (std::exception& ex)
		{
			MakeAnswer(error, answer, ex.what());
		}


	}

	void TioTcpServer::OnCommand_CreateContainer_OpenContainer(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		//
		// create name type [params]
		// open name [type] [params]
		//
		if (!CheckParameterCount(cmd, 1, at_least))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		const string& containerName = cmd.GetParameters()[0];
		string containerType;

		if (cmd.GetParameters().size() > 1)
			containerType = cmd.GetParameters()[1];

		try
		{
			shared_ptr<ITioContainer> container;

			if (cmd.GetCommand() == "create")
			{
				if (containerName.size() > 1 && containerName[0] == '_' && containerName[1] == '_')
				{
					MakeAnswer(error, answer, "invalid name, names starting with __ are reserved for internal use");
					return;
				}

				if (containerType.empty())
					containerType = "volatile_list";

				if (!CheckCommandAccess(cmd.GetCommand(), answer, session))
					return;

				container = containerManager_.CreateContainer(containerType, containerName);
			}
			else
			{
				if (!CheckObjectAccess(containerType, containerName, cmd.GetCommand(), answer, session))
					return;

				container = containerManager_.OpenContainer(containerType, containerName);
			}

			unsigned int handle = session->RegisterContainer(containerName, container);

			MakeAnswer(success, answer, "handle", lexical_cast<string>(handle), container->GetType());
		}
		catch (std::exception& ex)
		{
			MakeAnswer(error, answer, ex.what());
		}
	}

	void TioTcpServer::OnCommand_CloseContainer(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		using boost::bad_lexical_cast;

		if (!CheckParameterCount(cmd, 1, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		try
		{
			session->CloseContainerHandle(lexical_cast<unsigned int>(cmd.GetParameters()[0]));
		}
		catch (std::exception&)
		{
			MakeAnswer(error, answer, "Invalid handle");
			return;
		}

		MakeAnswer(success, answer);
	}


	pair<shared_ptr<ITioContainer>, int> TioTcpServer::GetRecordBySpec(const string& spec, shared_ptr<TioTcpSession> session)
	{
		vector<string> spl;
		pair<shared_ptr<ITioContainer>, int> p;

		//
		// format: handle:rec_number
		//

		split(spl, spec, is_any_of(":"));

		if (spl.size() != 2)
			throw std::invalid_argument("invalid record spec");

		try
		{
			unsigned int handle;

			handle = lexical_cast<unsigned int>(spl[0]);

			p.second = lexical_cast<int>(spl[1]);

			p.first = session->GetRegisteredContainer(handle);
		}
		catch (bad_lexical_cast&)
		{
			throw std::invalid_argument("invalid record spec");
		}
		catch (std::invalid_argument)
		{
			throw;
		}

		return p;
	}

	void TioTcpServer::OnCommand_ListHandles(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		if (!CheckParameterCount(cmd, 0, exact))
		{
			MakeAnswer(error, answer, "invalid parameter count");
			return;
		}

		MakeAnswer(error, answer, "not implemented");

		/*
		vector<string> v;

		v.reserve(handles_.size() * 2);

		BOOST_FOREACH(HandleMap::value_type i, handles_)
		{
			unsigned int handle = i.first;
			shared_ptr<ITioContainer> container = i.second;

			v.push_back("\r\n");
			v.push_back(lexical_cast<string>(handle));
			v.push_back(container->GetName());
		}

		MakeAnswer(success, answer, "", v.begin(), v.end());
		return;
		*/
	}

	inline size_t ExtractFieldSet(
		vector<string>::const_iterator begin,
		vector<string>::const_iterator end,
		const void* buffer,
		size_t bufferSize,
		TioData* key,
		TioData* value,
		TioData* metadata)
	{
		size_t fieldsTotalSize;
		vector<FieldInfo> fields;

		pair_assign(fields, fieldsTotalSize) = ExtractFieldSet(begin, end);

		if (fieldsTotalSize > bufferSize)
			return fieldsTotalSize;

		ExtractFieldsFromBuffer(fields, buffer, bufferSize, key, value, metadata);

		return 0;
	}


	size_t TioTcpServer::ParseDataCommand(
		Command& cmd,
		string* containerType,
		string* containerName,
		shared_ptr<ITioContainer>* container,
		TioData* key,
		TioData* value,
		TioData* metadata,
		shared_ptr<TioTcpSession> session,
		unsigned int* handle)
	{
		const Command::Parameters& parameters = cmd.GetParameters();

		if (!CheckParameterCount(cmd, 4, at_least))
			throw std::invalid_argument("Invalid parameter count");

		try
		{
			unsigned int h;

			h = lexical_cast<unsigned int>(cmd.GetParameters()[0]);
			*container = session->GetRegisteredContainer(h, containerName, containerType);

			if (handle)
				*handle = h;
		}
		catch (boost::bad_lexical_cast&)
		{
			throw std::invalid_argument("invalid handle");
		}
		catch (std::invalid_argument&)
		{
			throw std::invalid_argument("invalid handle");
		}

		return ExtractFieldSet(
			parameters.begin() + 1,
			parameters.end(),
			cmd.GetDataBuffer()->GetRawBuffer(),
			cmd.GetDataBuffer()->GetSize(),
			key,
			value,
			metadata);
	}

	void TioTcpServer::OnCommand_Pop(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		try
		{
			if (!CheckParameterCount(cmd, 1, exact))
			{
				MakeAnswer(error, answer, "invalid parameter count");
				return;
			}

			BOOST_ASSERT(cmd.GetCommand() == "pop_back" || cmd.GetCommand() == "pop_front");

			string containerName, containerType;

			shared_ptr<ITioContainer> container =
				session->GetRegisteredContainer(
					lexical_cast<unsigned int>(cmd.GetParameters()[0]),
					&containerName,
					&containerType);

			if (!CheckObjectAccess(containerType, containerName, cmd.GetCommand(), answer, session))
				return;

			TioData key, value, metadata;

			if (cmd.GetCommand() == "pop_back")
				container->PopBack(&key, &value, &metadata);
			else if (cmd.GetCommand() == "pop_front")
				container->PopFront(&key, &value, &metadata);

			MakeDataAnswer(key, value, metadata, answer);

		}
		catch (std::exception& e)
		{
			MakeAnswer(error, answer, e.what());
			return;
		}
	}

	void TioTcpServer::OnModify(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		try
		{
			string containerName, containerType;
			shared_ptr<ITioContainer> container;
			TioData key, value, metadata;

			size_t dataSize = ParseDataCommand(
				cmd,
				&containerType,
				&containerName,
				&container,
				&key,
				&value,
				&metadata,
				session);

			if (!CheckObjectAccess(containerType, containerName, cmd.GetCommand(), answer, session))
				return;

			if (dataSize != 0)
			{
				*moreDataSize = dataSize;
				return;
			}


			if (!key || !value)
			{
				MakeAnswer(error, answer, "need key and data");
				return;
			}

			if (value.GetDataType() != TioData::String && value.GetSize() < 2)
			{
				MakeAnswer(error, answer, "invalid data");
				return;
			}

			const char* sz = value.AsSz();

			//
			// only sum yet
			//
			if (sz[0] != '+')
			{
				MakeAnswer(error, answer, "invalid operation");
				return;
			}

			int toAdd = lexical_cast<int>(&sz[1]);

			TioData currentData;

			container->GetRecord(key, NULL, &currentData, NULL);

			if (currentData.GetDataType() != TioData::Int)
			{
				MakeAnswer(error, answer, "modified data must be integer");
				return;
			}

			currentData.Set(currentData.AsInt() + toAdd);

			container->Set(key, currentData, TIONULL);

			MakeDataAnswer(TIONULL, currentData, TIONULL, answer);

		}
		catch (std::exception& e)
		{
			MakeAnswer(error, answer, e.what());
			return;
		}

		MakeAnswer(success, answer);

		return;
	}




	void TioTcpServer::OnAnyDataCommand(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session)
	{
		try
		{
			string containerName, containerType;
			shared_ptr<ITioContainer> container;
			TioData key, value, metadata;

			size_t dataSize = ParseDataCommand(
				cmd,
				&containerType,
				&containerName,
				&container,
				&key,
				&value,
				&metadata,
				session);

			if (!CheckObjectAccess(containerType, containerName, cmd.GetCommand(), answer, session))
				return;

			if (dataSize != 0)
			{
				*moreDataSize = dataSize;
				return;
			}

			if (cmd.GetCommand() == "insert")
				container->Insert(key, value, metadata);
			else if (cmd.GetCommand() == "set")
				container->Set(key, value, metadata);
			else if (cmd.GetCommand() == "pop_back")
				container->PopBack(&key, &value, &metadata);
			else if (cmd.GetCommand() == "pop_front")
				container->PopFront(&key, &value, &metadata);
			else if (cmd.GetCommand() == "push_back")
				container->PushBack(key, value, metadata);
			else if (cmd.GetCommand() == "push_front")
				container->PushFront(key, value, metadata);
			else if (cmd.GetCommand() == "delete")
				container->Delete(key, value, metadata);
			else if (cmd.GetCommand() == "get")
			{
				TioData realKey;
				container->GetRecord(key, &realKey, &value, &metadata);
				MakeDataAnswer(realKey.GetDataType() == TioData::None ? key : realKey, value, metadata, answer);
				return;
			}
			else if (cmd.GetCommand() == "set_property")
			{
				try
				{
					container->SetProperty(key.AsSz(), value.AsSz());
				}
				catch (std::exception&)
				{
					MakeAnswer(error, answer, "key and value must be strings");
					return;
				}
			}
			else if (cmd.GetCommand() == "get_property")
			{
				try
				{
					TioData value;
					string propertyValue = container->GetProperty(key.AsSz());

					value.Set(propertyValue.c_str(), propertyValue.size());

					MakeDataAnswer(key, value, TIONULL, answer);
				}
				catch (std::exception&)
				{
					MakeAnswer(error, answer, "invalid key");
					return;
				}

				return;
			}
		}
		catch (std::exception& e)
		{
			MakeAnswer(error, answer, e.what());
			return;
		}

		MakeAnswer(success, answer);
	}

	void TioTcpServer::Start()
	{
		publisherThread_ = make_shared<thread>(
			[this]()
			{
				this->PublisherThread();
			}
		);

		DoAccept(atNeither);
	}

	string TioTcpServer::GetFullQualifiedName(shared_ptr<ITioContainer> container)
	{
		return containerManager_.ResolveAlias(container->GetType())
			+ "/" + container->GetName();
	}


} // namespace tio


