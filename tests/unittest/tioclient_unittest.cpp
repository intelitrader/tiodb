/** Testes unitários e funcionais do tioclient.

Os testes deste módulo podem se comportar como unitários ou funcionais, sendo que o padrão é eles serem unitários. Para trocar de unitário para funcional mude o valor do define INCL_WINSOCK_API_PROTOTYPES de 0 para 1 no arquivo cmake do projeto de unit tests.

No modo teste unitário as funções API de socket são mockadas internamente e podem ser modificadas através dos ponteiros de função contidos em tioclient_mock. Já existe um mecanismo de fluxo de rede neste módulo criado com duas listas de strings que representam os caches de send e recv do socket sendo testado. Ele pode ser modificado diretamente ou através de alguns helpers abaixo para construir strings no formato entendido pelo protocolo do tio.

No modo teste funcional espera-se que um ou mais tios (dependendo do teste) estejam rodando nas portas 2605 em diante (2606 para o segundo, 2607 para o terceiro...). Uma vez que estejam sendo executados o setup dos testes se encarrega de alimentá-los conforme os requisitos dos testes. É importante que eles possam ser reentrantes, ou seja, a segunda execução do teste com as mesmas instâncias de tios no ar (já alimentados pela primeira execução de testes) deve funcionar da mesma forma como se fosse a primeira execução.

Existe um setup para os testes (unitário e funcional) no início de cada teste. Se não existir um setup para o teste unitário ou funcional uma variável booleana indica que o teste não pode ser executado e ele será ignorado.

@author Wanderley Caloni <wanderley.caloni@intelitrader.com.br>
@date 2020-04
*/
#define _CRT_SECURE_NO_WARNINGS
#include "gtest/gtest.h"
extern "C" {
#include "tioclient_mock.h"
#define SOCKET SOCKET // tioclient_internals entra em conflito com a tipagem de SOCKET
#include "../../client/c/tioclient_internals.h"
}

#include <string>
#include <vector>
#include <list>
#include <chrono>
#include <thread>

using namespace std;


static list<string> send_history;
static list<string> recv_history;

/// Set this to true to debug and test.
bool g_functionalTests = getenv("TIODB_FUNCTIONAL_TESTS_ENABLED") ? true : false;


extern "C" {

	SOCKET socket_default(int af, int type, int protocol)
	{
		static SOCKET st_lastSocket = 0;
		return ++st_lastSocket;
	}

	int getaddrinfo_default(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult)
	{
		static struct addrinfo addr_result = {};
		static struct sockaddr sock_addr = { };
		addr_result.ai_addr = &sock_addr;
		*ppResult = &addr_result;
		return 0;
	}

	int send_default(SOCKET s, const char FAR* buf, int len, int flags)
	{
		string msg(buf, len);
		send_history.push_back(msg);
		return len;
	}

	int recv_default(SOCKET s, char FAR* buf, int len, int flags)
	{
		if (recv_history.size())
		{
			string& last_recv(recv_history.front());
			int sz = min(len, (int)last_recv.size());
			memcpy(buf, last_recv.data(), sz);
			if (last_recv.size() > sz)
				last_recv.erase(0, sz);
			else
				recv_history.pop_front();
			return sz;
		}
		return 0;
	}

	int select_default(int nfds, fd_set FAR* readfds, fd_set FAR* writefds, fd_set FAR* exceptfds, const struct timeval FAR* timeout)
	{
		while (recv_history.empty())
			std::this_thread::sleep_for(std::chrono::seconds(1));
		return 1;
	}

}


class TioClientTest : public ::testing::Test {
       protected:
	TioClientTest() {
        // initialize your stuff
	};

	~TioClientTest() override{}

	void SetUp() override
	{
		socket_mock = socket_default;
		getaddrinfo_mock = getaddrinfo_default;
		send_mock = send_default;
		recv_mock = recv_default;
		select_mock = select_default;
	}

	void TearDown() override{}

    // your stuff
};

void add_arg(PR1_MESSAGE* message) { }

template<typename... Targs>
void add_arg(PR1_MESSAGE* message, unsigned short arg_id, double arg_value, Targs... other_args)
{
	pr1_message_add_field_double(message, arg_id, arg_value);
	add_arg(message, other_args...);
}

template<typename... Targs>
void add_arg(PR1_MESSAGE* message, unsigned short arg_id, const char* arg_value, Targs... other_args)
{
	pr1_message_add_field_string(message, arg_id, arg_value);
	add_arg(message, other_args...);
}

template<typename... Targs>
void add_arg(PR1_MESSAGE* message, unsigned short arg_id, int arg_value, Targs... other_args)
{
	pr1_message_add_field_int(message, arg_id, arg_value);
	add_arg(message, other_args...);
}

template<typename T, typename... Targs>
string create_message(unsigned short arg_id, T arg_value, Targs... other_args)
{
	PR1_MESSAGE* message = pr1_message_new();
	add_arg(message, arg_id, arg_value, other_args...);
	void* buffer;
	unsigned int bufferSize;
	pr1_message_get_buffer(message, &buffer, &bufferSize, 0);
	string finalMessage((char*)buffer, bufferSize);
	pr1_message_delete(message);
	return finalMessage;
}

string answer_not_found()
{
	return create_message(MESSAGE_FIELD_ID_COMMAND, TIO_COMMAND_ANSWER, 
		MESSAGE_FIELD_ID_ERROR_CODE, TIO_ERROR_NO_SUCH_OBJECT);
}

string answer_handle(int handle = 1)
{
	return create_message(MESSAGE_FIELD_ID_COMMAND, TIO_COMMAND_ANSWER, MESSAGE_FIELD_ID_HANDLE, handle);
}

string answer_key_value_meta(string key = "key", string value = "value", string meta = "meta")
{
	return create_message(MESSAGE_FIELD_ID_COMMAND, TIO_COMMAND_ANSWER,
		MESSAGE_FIELD_ID_KEY, key.c_str(),
		MESSAGE_FIELD_ID_VALUE, value.c_str(),
		MESSAGE_FIELD_ID_METADATA, meta.c_str());
}


void increment_int_callback(int /*result*/, void* /*handle*/, void* cookie, unsigned int /*event_code*/, 
	const char* /*group_name*/, const char* /*container_name*/, 
	const struct TIO_DATA*, const struct TIO_DATA*, const struct TIO_DATA*)
{
	int* result = (int*)cookie;
	*result += 1;
}


int attach_tio_master_to_cluster(const string& masterName, int masterPort, const string& clusterName, int clusterPort, const string& containerName)
{
	TIO_CONNECTION* connection = NULL, * connection2 = NULL;

	int result = tio_connect(masterName.c_str(), masterPort, &connection);
	if (result != TIO_SUCCESS) return result;
	result = tio_connect(clusterName.c_str(), clusterPort, &connection2);
	if (result != TIO_SUCCESS) return result;

	TIO_CONTAINER* container = NULL;
	result = tio_create(connection, "__meta__/clusters", "volatile_map", &container);
	if (result != TIO_SUCCESS) return result;

	TIO_DATA key;
	tiodata_init(&key);
	tiodata_set_string_and_size(&key, containerName.c_str(), (unsigned int)containerName.size());
	TIO_DATA value;
	tiodata_init(&value);
	string clusterAddress = clusterName + ":" + to_string(clusterPort);
	tiodata_set_string_and_size(&value, clusterAddress.c_str(), (unsigned int)clusterAddress.size());
	result = tio_container_set(container, &key, &value, NULL);
	if (result != TIO_SUCCESS) return result;
	result = tio_close(container);
	if (result != TIO_SUCCESS) return result;

	result = tio_create(connection2, containerName.c_str(), "volatile_map", &container);
	if (result != TIO_SUCCESS) return result;
	result = tio_close(container);
	if (result != TIO_SUCCESS) return result;

	tiodata_free(&key);
	tiodata_free(&value);

	tio_disconnect(connection);
	tio_disconnect(connection2);

	return result;
}


int set_container_key_value(const string& host, int port, const string& containerName, const string& key, const string& value)
{
	TIO_CONNECTION* connection = NULL;

	int result = tio_connect(host.c_str(), port, &connection);
	if (result != TIO_SUCCESS) return result;

	TIO_CONTAINER* container = NULL;
	result = tio_create(connection, containerName.c_str(), "volatile_map", &container);
	if (result != TIO_SUCCESS) return result;

	TIO_DATA _key;
	tiodata_init(&_key);
	tiodata_set_string_and_size(&_key, key.c_str(), (unsigned int)key.size());
	TIO_DATA _value;
	tiodata_init(&_value);
	tiodata_set_string_and_size(&_value, value.c_str(), (unsigned int)value.size());
	result = tio_container_set(container, &_key, &_value, NULL);
	if (result != TIO_SUCCESS) return result;
	result = tio_close(container);
	if (result != TIO_SUCCESS) return result;

	tiodata_free(&_key);
	tiodata_free(&_value);

	tio_disconnect(connection);

	return result;
}



TEST_F(TioClientTest, OpenContainerMasterRedirectToCluster)
{
	bool setup = false;
	string masterName = "localhost";
	int masterPort = 2605;
	string clusterName = "localhost";
	int clusterPort = 2606;
	string clusterAddress = clusterName + ":" + to_string(clusterPort);
	string containerTest = ::testing::UnitTest::GetInstance()->current_test_info()->name();
	string keyTest = "key";
	string valueTest = "value";

	// setup begin
#if !INCL_WINSOCK_API_PROTOTYPES
	{
		recv_history.push_back("going binary"); // connect tio...
		recv_history.push_back(answer_handle(1)); // ... and open cluster container
		recv_history.push_back(answer_not_found()); // error opening container
		recv_history.push_back(answer_key_value_meta(containerTest, clusterAddress)); // get cluster address for container
		recv_history.push_back("going binary"); // connect tio...
		recv_history.push_back(answer_not_found()); // ... and ERROR opening cluster container
		recv_history.push_back(answer_handle()); // success opening test container
		recv_history.push_back(answer_key_value_meta(keyTest.c_str(), valueTest.c_str())); // get value for name in container opened in cluster
		setup = true;
	}
#else
	if( g_functionalTests )
	{
		int result = attach_tio_master_to_cluster(masterName, masterPort, clusterName, clusterPort, containerTest);
		ASSERT_EQ(result, TIO_SUCCESS);
		result = set_container_key_value(clusterName, clusterPort, containerTest, keyTest, valueTest);
		ASSERT_EQ(result, TIO_SUCCESS);
		setup = true;
	}
#endif
	// setup end


	if (setup)
	{
		TIO_CONNECTION* connection = NULL;
		int result = tio_connect(masterName.c_str(), masterPort, &connection);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_CONTAINER* container = NULL;

		result = tio_open(connection, containerTest.c_str(), NULL, &container);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_DATA search_key;
		tiodata_init(&search_key);
		TIO_DATA value;
		tiodata_init(&value);
		tiodata_set_string_and_size(&search_key, keyTest.c_str(), (unsigned int) keyTest.size());
		result = tio_container_get(container, &search_key, NULL, &value, NULL);
		ASSERT_EQ(result, TIO_SUCCESS);
		ASSERT_EQ(string(value.string_), valueTest);
		tiodata_free(&search_key);
		tiodata_free(&value);
		tio_disconnect(connection);
	}
}


TEST_F(TioClientTest, SubscribeToContainerInMasterAndReceiveNotification)
{
	bool setup = false;
	string containerTest = ::testing::UnitTest::GetInstance()->current_test_info()->name();


	// setup begin
#if INCL_WINSOCK_API_PROTOTYPES
	if( g_functionalTests )
	{
		TIO_CONNECTION* connection = NULL, * connection2 = NULL;

		int result = tio_connect("localhost", 2605, &connection);
		ASSERT_EQ(result, TIO_SUCCESS);
		result = tio_connect("localhost", 2606, &connection2);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_CONTAINER* container = NULL;
		result = tio_create(connection, "__meta__/clusters", "volatile_map", &container);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_DATA key;
		tiodata_init(&key);
		tiodata_set_string_and_size(&key, containerTest.c_str(), (unsigned int) containerTest.size());
		TIO_DATA value;
		tiodata_init(&value);
		string valueS = "localhost:2606";
		tiodata_set_string_and_size(&value, valueS.c_str(), (unsigned int) valueS.size());
		result = tio_container_set(container, &key, &value, NULL);
		ASSERT_EQ(result, TIO_SUCCESS);
		result = tio_close(container);
		ASSERT_EQ(result, TIO_SUCCESS);

		result = tio_create(connection2, containerTest.c_str(), "volatile_map", &container);
		ASSERT_EQ(result, TIO_SUCCESS);
		result = tio_close(container);
		ASSERT_EQ(result, TIO_SUCCESS);

		tiodata_free(&key);
		tiodata_free(&value);

		tio_disconnect(connection);
		tio_disconnect(connection2);
		setup = true;
	}
#else
#endif
	// setup end


	if (setup)
	{
		TIO_CONNECTION* connection = NULL;

		int result = tio_connect("localhost", 2605, &connection);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_CONTAINER* container = NULL;
		result = tio_create(connection, containerTest.c_str(), "volatile_map", &container);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_DATA start;
		tiodata_init(&start);
		tiodata_set_int(&start, 0);
		int async_result = 0;
		result = tio_container_subscribe(container, &start, increment_int_callback, &async_result);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_DATA key;
		tiodata_init(&key);
		tiodata_set_string_and_size(&key, "key", sizeof("key"));
		TIO_DATA value;
		tiodata_init(&value);
		tiodata_set_string_and_size(&value, "value", sizeof("value"));
		result = tio_container_set(container, &key, &value, NULL);
		ASSERT_EQ(result, TIO_SUCCESS);

		unsigned int timeout = 1;
		int dispatch = tio_receive_next_pending_event(connection, &timeout);
		ASSERT_GT(dispatch, 0);
		ASSERT_GT(async_result, 0);

		tio_disconnect(connection);
	}
}


TEST_F(TioClientTest, SubscribeToContainerInClusterAndReceiveNotification)
{
	bool setup = false;
	string containerTest = ::testing::UnitTest::GetInstance()->current_test_info()->name();

	// setup begin
#if INCL_WINSOCK_API_PROTOTYPES
	if( g_functionalTests )
	{
		TIO_CONNECTION* connection = NULL, * connection2 = NULL;

		int result = tio_connect("localhost", 2605, &connection);
		ASSERT_EQ(result, TIO_SUCCESS);
		result = tio_connect("localhost", 2606, &connection2);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_CONTAINER* container = NULL;
		result = tio_create(connection, "__meta__/clusters", "volatile_map", &container);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_DATA key;
		tiodata_init(&key);
		tiodata_set_string_and_size(&key, containerTest.c_str(), (unsigned int) containerTest.size());
		TIO_DATA value;
		tiodata_init(&value);
		string valueS = "localhost:2606";
		tiodata_set_string_and_size(&value, valueS.c_str(), (unsigned int) valueS.size());
		result = tio_container_set(container, &key, &value, NULL);
		ASSERT_EQ(result, TIO_SUCCESS);
		result = tio_close(container);
		ASSERT_EQ(result, TIO_SUCCESS);

		result = tio_create(connection2, containerTest.c_str(), "volatile_map", &container);
		ASSERT_EQ(result, TIO_SUCCESS);
		result = tio_close(container);
		ASSERT_EQ(result, TIO_SUCCESS);

		tiodata_free(&key);
		tiodata_free(&value);

		tio_disconnect(connection);
		tio_disconnect(connection2);
		setup = true;
	}
#else
#endif
	// setup end


	if (setup)
	{
		TIO_CONNECTION* connection = NULL, * connection2 = NULL;
		TIO_DATA key, value;
		TIO_CONTAINER* container = NULL;

		int result = tio_connect("localhost", 2605, &connection);
		ASSERT_EQ(result, TIO_SUCCESS);
		result = tio_connect("localhost", 2606, &connection2);
		ASSERT_EQ(result, TIO_SUCCESS);
		result = tio_open(connection, containerTest.c_str(), NULL, &container);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_DATA start;
		tiodata_init(&start);
		tiodata_set_int(&start, 0);
		int async_result = 0;
		result = tio_container_subscribe(container, &start, increment_int_callback, &async_result);
		ASSERT_EQ(result, TIO_SUCCESS);

		TIO_CONTAINER* container2;
		result = tio_open(connection2, containerTest.c_str(), NULL, &container2);
		ASSERT_EQ(result, TIO_SUCCESS);
		tiodata_init(&key);
		tiodata_set_string_and_size(&key, "key", sizeof("key"));
		tiodata_init(&value);
		tiodata_set_string_and_size(&value, "value", sizeof("value"));
		result = tio_container_set(container2, &key, &value, NULL);
		ASSERT_EQ(result, TIO_SUCCESS);

		unsigned int timeout = 1;
		int dispatch = tio_receive_next_pending_event(connection, &timeout);
		ASSERT_GT(dispatch, 0);
		ASSERT_GT(async_result, 0);

		result = tio_close(container);
		ASSERT_EQ(result, TIO_SUCCESS);
		result = tio_close(container2);
		ASSERT_EQ(result, TIO_SUCCESS);

		tio_disconnect(connection);
		tio_disconnect(connection2);
	}
}

