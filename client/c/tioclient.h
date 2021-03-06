#pragma once

/*
	This is the C LANGUAGE Tio client.
	If you're looking for the C++ version, pick the tioclient.hpp header
*/

#ifdef _MSC_VER

#define _CRT_SECURE_NO_WARNINGS
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <assert.h>
#include <time.h>
#pragma comment(lib ,"ws2_32.lib")
#else
	#include <unistd.h>
	#include <stdlib.h>
	#include <assert.h>
	#include <stdio.h>
	#include <time.h>
	#include <string.h>
	#include <sys/types.h> 
	#include <sys/socket.h>
	#include <sys/ioctl.h>
	#include <netinet/in.h>
	#include <netdb.h>
    #include <errno.h>
	#define closesocket close
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TIO_DEFAULT_PORT                2605

#define TIO_DATA_TYPE_NONE	 			0x1
#define TIO_DATA_TYPE_STRING 			0x2
#define TIO_DATA_TYPE_INT 				0x3
#define TIO_DATA_TYPE_DOUBLE 			0x4

#include "tioerr.h"

#define TIO_COMMAND_PING				0x10
#define TIO_COMMAND_OPEN				0x11
#define TIO_COMMAND_CREATE				0x12
#define TIO_COMMAND_CLOSE				0x13
#define TIO_COMMAND_SET					0x14
#define TIO_COMMAND_INSERT				0x15
#define TIO_COMMAND_DELETE				0x16
#define TIO_COMMAND_PUSH_BACK			0x17
#define TIO_COMMAND_PUSH_FRONT			0x18
#define TIO_COMMAND_POP_BACK			0x19
#define TIO_COMMAND_POP_FRONT			0x1A
#define TIO_COMMAND_CLEAR				0x1B
#define TIO_COMMAND_COUNT				0x1C
#define TIO_COMMAND_GET					0x1D
#define TIO_COMMAND_SUBSCRIBE			0x1E
#define TIO_COMMAND_UNSUBSCRIBE			0x1F
#define TIO_COMMAND_QUERY				0x20
#define TIO_COMMAND_WAIT_AND_POP_NEXT	0x21
#define TIO_COMMAND_WAIT_AND_POP_KEY	0x22

#define TIO_EVENT_SNAPSHOT_END			0x23

#define TIO_COMMAND_PROPGET 			0x30
#define TIO_COMMAND_PROPSET 			0x31

#define TIO_COMMAND_GROUP_ADD 			0x33
#define TIO_COMMAND_GROUP_SUBSCRIBE		0x34

#define TIO_FAILED(x) (x < 0)


	typedef void(*DUMP_PROTOCOL_MESSAGES_FUNCTION)(const char*);
	void tio_set_dump_message_function(DUMP_PROTOCOL_MESSAGES_FUNCTION dump_protocol_messages);


//#ifndef SOCKET
//#define SOCKET int
//#endif

struct TIO_DATA
{	
	unsigned int data_type;
	int int_;
	char* string_;
	unsigned int string_size_;
	double double_;
};


typedef void (*event_callback_t)(int /*result*/, void* /*handle*/, void* /*cookie*/,  unsigned int /*event_code*/, 
								 const char* /*group_name*/, const char* /*container_name*/, const struct TIO_DATA*, const struct TIO_DATA*, const struct TIO_DATA*);

typedef void (*query_callback_t)(int /*result*/, void* /*handle*/, void* /*cookie*/, unsigned int /*queryid*/, 
								 const char* /*container_name*/, const struct TIO_DATA*, const struct TIO_DATA*, const struct TIO_DATA*);

struct TIO_CONNECTION;
struct TIO_CONTAINER;


//
// TIO_DATA related functions
//
void tiodata_init(struct TIO_DATA* tiodata);
unsigned int tiodata_get_type(struct TIO_DATA* tiodata);
void tiodata_set_as_none(struct TIO_DATA* tiodata);
void tiodata_free(struct TIO_DATA* tiodata);

//
// It works the same way MFC (argh) string. You ask for the buffer to
// write and must call release() after using so it can set the size of the
// string accordingly. If you don't do this, the string will have mim_size bytes
// and will contain the garbage on unused bytes
//
char* tiodata_string_get_buffer(struct TIO_DATA* tiodata, unsigned int min_size);
void  tiodata_string_release_buffer(struct TIO_DATA* tiodata);

//void tiodata_set_string(struct TIO_DATA* tiodata, const char* value);
void tiodata_set_string_and_size(struct TIO_DATA* tiodata, const void* buffer, unsigned int len);
void tiodata_set_int(struct TIO_DATA* tiodata, int value);
void tiodata_set_double(struct TIO_DATA* tiodata, double value);

void tiodata_copy(const struct TIO_DATA* source, struct TIO_DATA* destination);
void tiodata_convert_to_string(struct TIO_DATA* tiodata);

//
// tioclient functions
//

// MUST call it before using tio
void tio_initialize();
void tio_set_debug_flags(int flags);

int tio_connect(const char* host, short port, struct TIO_CONNECTION** connection);
void tio_disconnect(struct TIO_CONNECTION* connection);

void tio_begin_network_batch(struct TIO_CONNECTION* connection);
void tio_finish_network_batch(struct TIO_CONNECTION* connection);
    void check_not_on_network_batch(struct TIO_CONNECTION* connection);

int tio_create(struct TIO_CONNECTION* connection, const char* name, const char* type, struct TIO_CONTAINER** container);
int tio_open(struct TIO_CONNECTION* connection, const char* name, const char* type, struct TIO_CONTAINER** container);
int tio_close(struct TIO_CONTAINER* container);

int tio_receive_next_pending_event(struct TIO_CONNECTION* connection, const unsigned* timeout_in_seconds);
int tio_dispatch_pending_events(struct TIO_CONNECTION* connection, unsigned int max_events);


int tio_ping(struct TIO_CONNECTION* connection, char* payload);

const char* tio_container_name(struct TIO_CONTAINER* container);

int tio_container_propset(struct TIO_CONTAINER* container, const struct TIO_DATA* key, const struct TIO_DATA* value);
int tio_container_propget(struct TIO_CONTAINER* container, const struct TIO_DATA* search_key, struct TIO_DATA* value);

int tio_container_push_back(struct TIO_CONTAINER* container, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata);
int tio_container_push_front(struct TIO_CONTAINER* container, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata);
int tio_container_pop_back(struct TIO_CONTAINER* container, struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata);
int tio_container_pop_front(struct TIO_CONTAINER* container, struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata);
int tio_container_set(struct TIO_CONTAINER* container, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata);
int tio_container_insert(struct TIO_CONTAINER* container, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata);
int tio_container_clear(struct TIO_CONTAINER* container);
int tio_container_delete(struct TIO_CONTAINER* container, const struct TIO_DATA* key);
int tio_container_get(struct TIO_CONTAINER* container, const struct TIO_DATA* search_key, struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata);
int tio_container_get_count(struct TIO_CONTAINER* container, int* count);
int tio_container_query(struct TIO_CONTAINER* container, int start, int end, const char* regex, query_callback_t query_callback, void* cookie);
int tio_container_subscribe(struct TIO_CONTAINER* container, struct TIO_DATA* start, event_callback_t event_callback, void* cookie);
int tio_container_unsubscribe(struct TIO_CONTAINER* container);
    int tio_container_wait_and_pop_next(struct TIO_CONTAINER* container, event_callback_t event_callback, void* cookie);

int tio_group_add(struct TIO_CONNECTION* connection, const char* group_name, const char* container_name);
int tio_group_subscribe(struct TIO_CONNECTION* connection, const char* group_name, const char* start);
int tio_group_set_subscription_callback(struct TIO_CONNECTION* connection,  event_callback_t callback, void* cookie);

const char* tio_get_last_error_description();

const char* tio_event_code_to_string(int event_code);const char* tio_event_code_to_string(int event_code);


#include "tioplugin.h"

#ifdef __cplusplus
} // extern "C" 
#endif
