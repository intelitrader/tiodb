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
#pragma once

#include "pch.h"
#include "buffer.h"
#include "Command.h"
#include "ContainerManager.h"
#include "TioTcpProtocol.h"
#include "TioTcpSession.h"
#include "auth.h"
#include "logdb.h"
#include "HttpParser.h"
#include "tiomutex.h"
#include "BinaryProtocolLogger.h"

namespace tio
{
	
	using std::shared_ptr;
	using std::make_shared;
	using boost::system::error_code;
	namespace asio = boost::asio;
	using namespace boost::asio::ip;

	using std::string;
	using std::vector;
	using std::map;
	using std::deque;
	using std::atomic;
	using std::thread;
	
	std::string Serialize(const std::list<const TioData*>& fields);

	class GroupManager : boost::noncopyable
	{
		struct GroupSubscriberInfo
		{
			weak_ptr<TioTcpSession> session;
			string start;

			GroupSubscriberInfo(){}

			GroupSubscriberInfo(shared_ptr<TioTcpSession> session, string start)
				: session(session)
				, start(start)
			{
			}
		};

		class GroupInfo : boost::noncopyable
		{
		public:
			string groupName;
			string containerListName;
			shared_ptr<ITioContainer> containerListContainer;

			// name, container
			map<string, shared_ptr<ITioContainer>> containersMap;


		public:

			GroupInfo(ContainerManager* containerManager, const string& name)
				: groupName(name)
				
			{
				containerListName = "__meta__/groups/";
				containerListName += name;

				containerListContainer = containerManager->CreateContainer("volatile_map", containerListName);
			}

			GroupInfo(GroupInfo&& rhv)
			{
				std::swap(groupName, rhv.groupName);
				std::swap(containerListName, rhv.containerListName);
				std::swap(containerListContainer, rhv.containerListContainer);
				std::swap(containersMap, rhv.containersMap);
			}
			GroupInfo(const GroupInfo& rhv) = delete;
		};

		map<string, GroupInfo> groups_;
		tio_recursive_mutex mutex_;

		GroupInfo& GetGroup(ContainerManager* containerManager, const string& groupName)
		{
			tio_lock_guard lock(mutex_);

			auto i = groups_.find(groupName);

			if(i == groups_.end())
				i = groups_.insert(std::move(pair<string, GroupInfo>(groupName, GroupInfo(containerManager, groupName)))).first;
			
			return i->second;
		}

	public:
		void AddContainer(ContainerManager* containerManager, const string& groupName, shared_ptr<ITioContainer> container)
		{
			tio_lock_guard lock(mutex_);

			GroupInfo& groupInfo = GetGroup(containerManager, groupName);

			if(groupInfo.containersMap.find(container->GetName()) != groupInfo.containersMap.end())
			{
				return;
			}

			groupInfo.containersMap[container->GetName()] = container;			
		}

		bool RemoveContainer(const string& groupName, const string& containerName)
		{
			return false;
		}

		map<string, shared_ptr<ITioContainer>> GetGroupContainers(const string& groupName)
		{
			tio_lock_guard lock(mutex_);

			auto i = groups_.find(groupName);

			if(i == groups_.end())
				return map<string, shared_ptr<ITioContainer>>();
			
			return i->second.containersMap;
		}
	};
	
	class TioTcpServer
	{
	public:
		typedef std::function<void (Command&, ostream&, size_t*, shared_ptr<TioTcpSession>)> CommandFunction;

	private:

		struct SubscriptionInfo
		{
			unsigned handle;
			bool snapshotPending;
			string groupName;
			string start;

			weak_ptr<TioTcpSession> session;
			shared_ptr<ITioContainer> container;
			uint64_t lastRevNum;

			SubscriptionInfo() {}

			SubscriptionInfo(const string& groupName, const shared_ptr<ITioContainer>& container, unsigned handle, const shared_ptr<TioTcpSession>& session, const string& start)
				: groupName(groupName)
				, container(container)
				, handle(handle)
				, session(session)
				, start(start)
				, lastRevNum(0)
				, snapshotPending(true)
			{}
		};

		struct EventInfo
		{
			uint64_t storageId;
			ContainerEventCode eventCode;
			TioData k;
			TioData v;
			TioData m;
		};

		enum AcceptType
		{
			atRemote,
			atLocal,
			atNeither,
		};

		//
		// we use a shared_ptr so we can use the SubscriptionInfo
		// without locking the entire container. The vector memory can
		// be moved on a resize. If we have a shared_ptr reference
		// we are safe even if the vector memory was moved or if
		// the struct was removed from the container
		//
		map<uint64_t, vector<shared_ptr<SubscriptionInfo>>> subscribers_;
		std::mutex subscribersMutex_;

		shared_ptr<thread> publisherThread_;
		vector<EventInfo> eventQueue_;
		std::mutex eventQueueMutex_;
		std::condition_variable eventQueueConditionVar_;

		struct MetaContainers
		{
			shared_ptr<ITioContainer> users;
			shared_ptr<ITioContainer> sessions;
		};
		
		atomic<unsigned int> lastSessionID_;
		atomic<unsigned int> lastQueryID_;

		typedef void (tio::TioTcpServer::* CommandCallbackFunction)(tio::Command &,std::ostream &,size_t *,std::shared_ptr<TioTcpSession>);

		typedef std::map<string, CommandCallbackFunction> CommandFunctionMap;
		CommandFunctionMap dispatchMap_;

		Auth auth_;

		bool serverPaused_;
		
		typedef tcp::acceptor acceptor_t;
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
		typedef boost::asio::local::stream_protocol::acceptor local_acceptor_t;
#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS

		acceptor_t acceptor_;
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
		local_acceptor_t local_acceptor_;
#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS
		asio::io_service& io_service_;
		
		typedef std::set< shared_ptr<TioTcpSession> > SessionsSet;
		SessionsSet sessions_;
		tio_recursive_mutex sessionsMutex_;

		ContainerManager& containerManager_;

		MetaContainers metaContainers_;

		BinaryProtocolLogger logger_;

		// group_name, session
		map<string, vector<weak_ptr<TioTcpSession>>> groupSubscriptions_;
		GroupManager groupManager_;

		void PublisherThread();
		void SessionAsyncSubscribe(const shared_ptr<SubscriptionInfo>& subscriptionInfo);

		unsigned int GenerateSessionId();
				
		void DoAccept(AcceptType lastAcceptType = atNeither);
		void OnAccept(shared_ptr<TioTcpSession> client, const error_code& err, bool remoteSocket = true);
		
		pair<shared_ptr<ITioContainer>, int> GetRecordBySpec(const string& spec, shared_ptr<TioTcpSession> session);

		size_t ParseDataCommand(Command& cmd, string* containerType, string* containerName, shared_ptr<ITioContainer>* container, 
			TioData* key, TioData* value, TioData* metadata, shared_ptr<TioTcpSession> session, unsigned int* handle = NULL);

		void LoadDispatchMap();

		void OnCommand_Query(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		void OnCommand_QueryEx(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		void OnCommand_GroupAdd(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);
		void OnCommand_GroupSubscribe(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		void OnCommand_Ping(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);
		void OnCommand_Version(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);
		
		void OnCommand_CreateContainer_OpenContainer(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);
		void OnCommand_CloseContainer(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		void OnCommand_DeleteContainer(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		void OnCommand_ListHandles(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);
		
		void OnCommand_Pop(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);
		
		void OnCommand_GetRecordCount(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		void OnCommand_SubscribeUnsubscribe(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		string GetFullQualifiedName(shared_ptr<ITioContainer> container);

		void OnCommand_PauseResume(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		void OnCommand_Auth(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);
		void OnCommand_SetPermission(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		void OnAnyDataCommand(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);
		
		void OnModify(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		void OnCommand_CustomCommand(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);
		void OnCommand_Clear(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);

		void RemoveClient(shared_ptr<TioTcpSession> client);
		bool CheckCommandAccess(const string& command, ostream& answer, shared_ptr<TioTcpSession> session);
		bool CheckObjectAccess(const string& objectType, const string& objectName, const string& command, ostream& answer, shared_ptr<TioTcpSession> session);

		string GetConfigValue(const string& key);

		void InitializeMetaContainers();

		shared_ptr<ITioContainer> GetContainerAndParametersFromRequest(const PR1_MESSAGE* message, shared_ptr<TioTcpSession> session, TioData* key, TioData* value, TioData* metadata);

		void OnAnyContainerEvent(
			uint64_t storageId,
			ContainerEventCode eventCode,
			const TioData& k, const TioData& v, const TioData& m);

	public:

		typedef tcp::endpoint endpoint_t;
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
		typedef boost::asio::local::stream_protocol::endpoint local_endpoint_t;
#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS

		TioTcpServer(ContainerManager& containerManager,
			asio::io_service& io_service, 
			const endpoint_t& endpoint, 
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
			const local_endpoint_t& local_endpoint, 
#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS
			const std::string& logFilePath);

		~TioTcpServer();
		void OnClientFailed(shared_ptr<TioTcpSession> client, const error_code& err);

		void PostCallback(function<void()> callback);

		void OnTextCommand(Command& cmd, ostream& answer, size_t* moreDataSize, shared_ptr<TioTcpSession> session);
		void OnBinaryCommand(shared_ptr<TioTcpSession> session, PR1_MESSAGE* message);
		void OnHttpCommand(const HTTP_MESSAGE& httpMessage, const shared_ptr<TioTcpSession>& session);

		void Start();

		Auth& GetAuth();
		unsigned CreateNewQueryId();

	};

	void StartServer();
}



