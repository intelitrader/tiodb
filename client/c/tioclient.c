#define _WINSOCK_DEPRECATED_NO_WARNINGS /* we use gethostbyname */
#include "tioclient_internals.h"
#ifndef _WIN32 /* Linux and Mac OS */
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/un.h>
#include <pthread.h>
#endif
#include <stdio.h>
#include <ctype.h>

//#define TIO_ERROR_NETWORK				  -2
#define MAX_ERROR_DESCRIPTION_SIZE 255

#ifndef BOOL
#define BOOL int
#endif


#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#ifndef FD_SET
#define FD_SET fd_set
#endif

int g_initialized = FALSE;

typedef void(*DUMP_PROTOCOL_MESSAGES_FUNCTION)(const char*);

volatile DUMP_PROTOCOL_MESSAGES_FUNCTION g_dump_protocol_messages = NULL;

char g_last_error_description[MAX_ERROR_DESCRIPTION_SIZE];


void tio_set_dump_message_function(DUMP_PROTOCOL_MESSAGES_FUNCTION dump_protocol_messages)
{
	g_dump_protocol_messages = dump_protocol_messages;
}

void test_function()
{
    printf("Hello from tioclient.so\n");
}

unsigned strlen32(const char* str)
{
	size_t size = strlen(str);
	assert(size < 0xFFFFFFFF);
	return (unsigned)size;
}

char* to_lower(char* p)
{
	for(; *p; ++p)
	{
		*p = (char)tolower(*p);
	}
	return p;
}

char* duplicate_string(const char* src)
{
	char* buf;

	if(!src)
		return NULL;

	buf = (char*)malloc(strlen32(src) + 1);

	strcpy(buf, src);
	return buf;
}

void x1_free(struct X1_FIELD* fields)
{
	struct X1_FIELD* field;

	if(!fields)
		return;

	for(field = fields; field->value != NULL; field++)
		free(field->value);

	free(fields);
}


struct X1_FIELD* x1_decode(const char* buffer, unsigned int size)
{
	struct X1_FIELD* ret = NULL;
	struct X1_FIELD* field = NULL;
	unsigned offset;
	unsigned a;
	unsigned ret_buffer_size;
    char* bufferCopy = NULL;
	unsigned fieldCount;
	// header + fields + space before data
	unsigned fieldSpecStartOffset = 7;
	char* currentData;
	unsigned fieldSpecOffset;
	unsigned fieldDataSize;

	if(memcmp(buffer, "X1", 2) != 0)
		return NULL;

	// copy the buffer so we can add the \0s that will make our lives easier
	bufferCopy = (char*)malloc(size);
	memcpy(bufferCopy, buffer, size);


	offset = 2 + 4;
	if(offset > size)
        goto clean_up_and_return_error;

	bufferCopy[offset] = '\0';

	// + 2: skip "X1"
	//
	// going to use sscanf instead of atoi because atoi doesn't support hex
	//	
	sscanf(bufferCopy + 2, "%X", &fieldCount);

	offset = fieldSpecStartOffset + (5 * fieldCount) + 1;
	if(offset > size)
        goto clean_up_and_return_error;

	currentData = &bufferCopy[offset];

	ret_buffer_size = (fieldCount + 1) * sizeof(struct X1_FIELD);
	ret = (struct X1_FIELD*) malloc(ret_buffer_size);
	memset(ret, 0, ret_buffer_size);

	for(a = 0; a < fieldCount; a++)
	{
		fieldSpecOffset = fieldSpecStartOffset + (5 * a);

		if(fieldSpecOffset + 4 > size)
            goto clean_up_and_return_error;

		ret[a].data_type = bufferCopy[fieldSpecOffset + 4];

		bufferCopy[fieldSpecOffset + 4] = '\0';

		sscanf(&bufferCopy[fieldSpecOffset], "%X", &fieldDataSize);

		currentData[fieldDataSize] = '\0';

		ret[a].value = (char*)malloc(strlen32(currentData) + 1);
		strcpy(ret[a].value, currentData);

		currentData += fieldDataSize + 1;
	}

	free(bufferCopy);

	return ret;

clean_up_and_return_error:
	free(bufferCopy);
	if (ret)
		x1_free(ret);
	return NULL;
}

const char* pr1_get_last_error_description()
{
	return g_last_error_description;
}

void pr1_set_last_error_description(const char* description)
{
	strncpy(g_last_error_description, description, MAX_ERROR_DESCRIPTION_SIZE);
	g_last_error_description[MAX_ERROR_DESCRIPTION_SIZE-1] = '\0';
}


int socket_pending_bytes(SOCKET socket, unsigned* count)
{
	int ret;
#ifdef _WIN32
	u_long pending_bytes = 0;

	ret = ioctlsocket(socket, FIONREAD, &pending_bytes);
	if(ret == SOCKET_ERROR)
		return ret;

	*count = (unsigned) pending_bytes;

	return 0;
#else
	int pending_bytes = 0;

	ret = ioctl(socket, FIONREAD, &pending_bytes);
	if(ret == -1)
		return ret;

	*count = (unsigned)pending_bytes;

	return 0;
	
#endif
}

int socket_send(SOCKET socket, const void* buffer, unsigned int len)
{
	int ret = send(socket, (const char*)buffer, len, 0);

	if (ret <= 0)
	{
#ifdef _WIN32
		if( ret < 0 )
			errno = WSAGetLastError();
#endif
		ret = TIO_ERROR_NETWORK;
	}
	
	return ret;
}

//
// Contract: 
//		if no timeout, it will hang until all bytes are returned
//		if timeout is set, we can return less bytes than requested
//
int socket_receive(SOCKET socket, void* buffer, int len, const unsigned* timeout_in_seconds)
{
	int ret = 0;
	char* char_buffer = (char*)buffer;
	int received = 0;
	time_t start = 0;
	int time_left;

	fd_set recvset;
	struct timeval tv;

#ifdef _DEBUG
	memset(char_buffer, 0xFF, len);
#endif

	if(timeout_in_seconds)
		start = time(NULL);

	while(received < len)
	{
		if(timeout_in_seconds)
		{
			time_left = *timeout_in_seconds - (int)(time(NULL) - start);

			if(time_left < 0)
			{
				return TIO_ERROR_TIMEOUT;
			}

			tv.tv_sec = *timeout_in_seconds;
			tv.tv_usec = 0;

			FD_ZERO(&recvset);
			FD_SET(socket, &recvset);

			ret = select(FD_SETSIZE, &recvset, NULL, NULL, (timeout_in_seconds ? &tv : NULL));

			if(ret == SOCKET_ERROR)
			{
#ifdef _WIN32
				errno = WSAGetLastError();
#endif
				pr1_set_last_error_description("Error reading data from server. Server is down or there is a network problem.");
				return TIO_ERROR_NETWORK;
			}

			if(ret == 0)
			{
				return TIO_ERROR_TIMEOUT;
			}

			assert(ret == 1);
		}
		
		ret = recv(socket, char_buffer + received, len - received, 0);

		if(ret <= 0)
		{
#ifdef _WIN32
				errno = WSAGetLastError();
#endif
			pr1_set_last_error_description("Error reading data from server. Server is down or there is a network problem.");
			return TIO_ERROR_NETWORK;
		}

		received += ret;
	}
		
	return received;
}

/*
int socket_receive_if_available(SOCKET socket, void* buffer, unsigned int len)
{
	int ret;
#ifdef _WIN32
	u_long pending_bytes = 0;

	ret = ioctlsocket(socket, FIONREAD, &pending_bytes);
	if(ret == SOCKET_ERROR)
		return ret;

	if(pending_bytes < len)
		return 0;

	return socket_receive(socket, buffer, len, NULL); 
#else
	ret = recv(socket, (char*)buffer, len, MSG_DONTWAIT);

	// nothing pending
	if(ret == EAGAIN)
		return 0;

	return ret;
#endif
}
*/




struct STREAM_BUFFER* stream_buffer_new()
{
	struct STREAM_BUFFER* message_buffer = (struct STREAM_BUFFER*)malloc(sizeof(struct STREAM_BUFFER));

	message_buffer->buffer_size = 1024 * 4;
	message_buffer->buffer = (char*) malloc(message_buffer->buffer_size);
	memset(message_buffer->buffer, 0xFF, message_buffer->buffer_size);
	message_buffer->current = message_buffer->buffer;

	return message_buffer;
}

unsigned int stream_buffer_space_used(struct STREAM_BUFFER* stream_buffer)
{
	unsigned ret = (unsigned)(stream_buffer->current - stream_buffer->buffer);
	return ret;
}

unsigned int stream_buffer_space_left(struct STREAM_BUFFER* stream_buffer)
{
	return (unsigned)(stream_buffer->buffer_size - stream_buffer_space_used(stream_buffer));
}

void stream_buffer_ensure_space_left(struct STREAM_BUFFER* stream_buffer, unsigned int size)
{
	unsigned int new_size;
	char* new_buffer;
	unsigned int used = stream_buffer_space_used(stream_buffer);

	if(stream_buffer->buffer_size - used >= size)
		return;

	// If no room to the new data, we'll raise the the stream size by
	// (new data size) * 2. Not sure if it's the best heuristic, but
	// surely works
	new_size = stream_buffer->buffer_size + (size * 2);
	new_buffer = (char*)malloc(new_size);
	memset(new_buffer, 0xFF, new_size);
		
	memcpy(new_buffer, stream_buffer->buffer, stream_buffer->buffer_size);

	stream_buffer->buffer = new_buffer;
	stream_buffer->buffer_size = new_size;
	stream_buffer->current = stream_buffer->buffer + used;
}

unsigned int stream_buffer_seek(struct STREAM_BUFFER* stream_buffer, unsigned int position)
{
	if(position >= stream_buffer->buffer_size)
		position = stream_buffer->buffer_size - 1;

	stream_buffer->current = stream_buffer->buffer + position;

	return position;
}


//
// this function will reserve the space on stream and return the pointer. It's useful for
// when you want to add a struct to the stream but you should fill the variables. This will
// avoid creating the struct on stack and copying the memory to the stream. Something like:
//
// struct MY_STRUCT* struct = (struct MY_STRCT*)stream_buffer_get_write_pointer(sb, sizeof(struct MY_STRUCT);
// struct->my_int = 10;
//
void* stream_buffer_get_write_pointer(struct STREAM_BUFFER* stream_buffer, unsigned int size)
{
	void* write_pointer;

	stream_buffer_ensure_space_left(stream_buffer, size);

	write_pointer = stream_buffer->current;

	// reserve the space
	stream_buffer->current += size;

	return write_pointer;
}

void stream_buffer_write(struct STREAM_BUFFER* stream_buffer, const void* buffer, unsigned int size)
{
	stream_buffer_ensure_space_left(stream_buffer, size);

	memcpy(stream_buffer->current, buffer, size);

	stream_buffer->current += size;
}

unsigned int stream_buffer_read(struct STREAM_BUFFER* stream_buffer, void* buffer, unsigned int size)
{
	unsigned int left = stream_buffer_space_left(stream_buffer);
	
	if(size > left)
		size = left;

	memcpy(buffer, stream_buffer->buffer, size);

	return size;
}


void stream_buffer_delete(struct STREAM_BUFFER* message_buffer)
{
	free(message_buffer->buffer);
	free(message_buffer);
}







struct PR1_MESSAGE* pr1_message_new()
{
	struct PR1_MESSAGE_HEADER* header;
	struct PR1_MESSAGE* pr1_message = (struct PR1_MESSAGE*)malloc(sizeof(struct PR1_MESSAGE));
	
	pr1_message->stream_buffer = stream_buffer_new();
	pr1_message->field_array = NULL;

	header = (struct PR1_MESSAGE_HEADER*)stream_buffer_get_write_pointer(pr1_message->stream_buffer, sizeof(struct PR1_MESSAGE_HEADER));

	// mark size on message header as invalid
	header->message_size = 0xFFFFFFFF;
	header->debug_session_seq_nun = 0;

	pr1_message->field_count = 0;

	return pr1_message;
}

void pr1_message_delete(struct PR1_MESSAGE* pr1_message)
{
	if(!pr1_message)
		return;

	stream_buffer_delete(pr1_message->stream_buffer);

	free(pr1_message->field_array);

	free(pr1_message);
}

void pr1_message_add_field(struct PR1_MESSAGE* pr1_message, unsigned short field_id, unsigned short data_type, const void* buffer, unsigned int buffer_size)
{
	struct PR1_MESSAGE_FIELD_HEADER* field_header = 
		(struct PR1_MESSAGE_FIELD_HEADER*)
			stream_buffer_get_write_pointer(pr1_message->stream_buffer, 
				sizeof(struct PR1_MESSAGE_FIELD_HEADER));

	field_header->data_type = data_type;
	field_header->field_id = field_id;
	field_header->data_size = buffer_size;

	stream_buffer_write(pr1_message->stream_buffer, buffer, buffer_size);

	pr1_message->field_count++;
}

void pr1_message_add_field_int(struct PR1_MESSAGE* pr1_message, unsigned short field_id, int value)
{
	pr1_message_add_field(pr1_message, field_id, MESSAGE_FIELD_TYPE_INT, &value, sizeof(value));
}

void pr1_message_add_field_double(struct PR1_MESSAGE* pr1_message, unsigned short field_id, double value)
{
	pr1_message_add_field(pr1_message, field_id, MESSAGE_FIELD_TYPE_DOUBLE, &value, sizeof(value));
}

void pr1_message_add_field_string(struct PR1_MESSAGE* pr1_message, unsigned short field_id, const char* value)
{
	pr1_message_add_field(pr1_message, field_id, MESSAGE_FIELD_TYPE_STRING, value, strlen32(value));
}

int pr1_message_field_get_int(const struct PR1_MESSAGE_FIELD_HEADER* field)
{
	assert(field->data_size == sizeof(int));
	field++;
	return *(int*)field;
}

double pr1_message_field_get_double(const struct PR1_MESSAGE_FIELD_HEADER* field)
{
	assert(field->data_size == sizeof(double));
	field++;
	return *(double*)field;
}

const void* pr1_message_field_get_buffer(const struct PR1_MESSAGE_FIELD_HEADER* field)
{
	field++;
	return field;
}


void pr1_message_field_get_string(const struct PR1_MESSAGE_FIELD_HEADER* field, char* buffer, unsigned int buffer_size)
{
	unsigned int copy_size = buffer_size < field->data_size ? buffer_size : field->data_size;

	field++;

	memcpy(buffer, field, copy_size);

	//
	// If there's some space left, we will use the \0 just in case
	//
	if(buffer_size > copy_size)
		buffer[copy_size] = '\0';
}

void pr1_message_field_get_string_malloc(const struct PR1_MESSAGE_FIELD_HEADER* field, char** buffer)
{
	char* internal_buffer;
	int size;
	
	size = field->data_size + 1;

	internal_buffer = malloc(size);

	pr1_message_field_get_string(field, internal_buffer, size);

	*buffer = internal_buffer;
}


int pr1_message_get_data_size(struct PR1_MESSAGE* pr1_message)
{
	return stream_buffer_space_used(pr1_message->stream_buffer);
}


void pr1_message_fill_header_info(struct PR1_MESSAGE* pr1_message, unsigned short debug_seq_num)
{
	struct PR1_MESSAGE_HEADER* header = (struct PR1_MESSAGE_HEADER*)pr1_message->stream_buffer->buffer;
	header->message_size = stream_buffer_space_used(pr1_message->stream_buffer) - sizeof(struct PR1_MESSAGE_HEADER);
	header->field_count = pr1_message->field_count;
	header->debug_session_seq_nun = debug_seq_num;
}

void pr1_message_get_buffer(struct PR1_MESSAGE* pr1_message, 
	void** buffer, 
	unsigned int* size, 
	unsigned short debug_seq_num)
{
	pr1_message_fill_header_info(pr1_message, debug_seq_num);

	*buffer = pr1_message->stream_buffer->buffer;
	*size = stream_buffer_space_used(pr1_message->stream_buffer);
}

const char* message_field_id_to_string(int i)
{
	if(i == MESSAGE_FIELD_ID_COMMAND) return "MESSAGE_FIELD_ID_COMMAND";
	if(i == MESSAGE_FIELD_ID_HANDLE) return "MESSAGE_FIELD_ID_HANDLE";
	if(i == MESSAGE_FIELD_ID_KEY) return "MESSAGE_FIELD_ID_KEY";
	if(i == MESSAGE_FIELD_ID_VALUE) return "MESSAGE_FIELD_ID_VALUE";
	if(i == MESSAGE_FIELD_ID_METADATA) return "MESSAGE_FIELD_ID_METADATA";
	if(i == MESSAGE_FIELD_ID_NAME) return "MESSAGE_FIELD_ID_NAME";
	if(i == MESSAGE_FIELD_ID_TYPE) return "MESSAGE_FIELD_ID_TYPE";
	if(i == MESSAGE_FIELD_ID_ERROR_CODE) return "MESSAGE_FIELD_ID_ERROR_CODE";
	if(i == MESSAGE_FIELD_ID_ERROR_DESC) return "MESSAGE_FIELD_ID_ERROR_DESC";
	if(i == MESSAGE_FIELD_ID_EVENT_CODE) return "MESSAGE_FIELD_ID_EVENT_CODE";
	if(i == MESSAGE_FIELD_ID_START_RECORD) return "MESSAGE_FIELD_ID_START_RECORD";
	if(i == MESSAGE_FIELD_ID_END) return "MESSAGE_FIELD_ID_END";
	if(i == MESSAGE_FIELD_ID_QUERY_ID) return "MESSAGE_FIELD_ID_QUERY_ID";
	if(i == MESSAGE_FIELD_ID_GROUP_NAME) return "MESSAGE_FIELD_ID_GROUP_NAME";
	if(i == MESSAGE_FIELD_ID_CONTAINER_TYPE) return "MESSAGE_FIELD_ID_CONTAINER_TYPE";
	if(i == MESSAGE_FIELD_ID_CONTAINER_NAME) return "MESSAGE_FIELD_ID_CONTAINER_NAME";

	return "*UNKNOWN*";
}

const char* tio_command_to_string(int i)
{
	if(i == TIO_COMMAND_ANSWER) return "TIO_COMMAND_ANSWER";
	if(i == TIO_COMMAND_EVENT) return "TIO_COMMAND_EVENT";				
	if(i == TIO_COMMAND_QUERY_ITEM) return "TIO_COMMAND_QUERY_ITEM";
	if(i == TIO_COMMAND_PING) return "TIO_COMMAND_PING";
	if(i == TIO_COMMAND_OPEN) return "TIO_COMMAND_OPEN";
	if(i == TIO_COMMAND_CREATE) return "TIO_COMMAND_CREATE";
	if(i == TIO_COMMAND_CLOSE) return "TIO_COMMAND_CLOSE";
	if(i == TIO_COMMAND_SET) return "TIO_COMMAND_SET";
	if(i == TIO_COMMAND_INSERT) return "TIO_COMMAND_INSERT";
	if(i == TIO_COMMAND_DELETE) return "TIO_COMMAND_DELETE";
	if(i == TIO_COMMAND_PUSH_BACK) return "TIO_COMMAND_PUSH_BACK";
	if(i == TIO_COMMAND_PUSH_FRONT) return "TIO_COMMAND_PUSH_FRONT";
	if(i == TIO_COMMAND_POP_BACK) return "TIO_COMMAND_POP_BACK";
	if(i == TIO_COMMAND_POP_FRONT) return "TIO_COMMAND_POP_FRONT";
	if(i == TIO_COMMAND_CLEAR) return "TIO_COMMAND_CLEAR";
	if(i == TIO_COMMAND_COUNT) return "TIO_COMMAND_COUNT";
	if(i == TIO_COMMAND_GET) return "TIO_COMMAND_GET";
	if(i == TIO_COMMAND_SUBSCRIBE) return "TIO_COMMAND_SUBSCRIBE";
	if(i == TIO_COMMAND_UNSUBSCRIBE) return "TIO_COMMAND_UNSUBSCRIBE";
	if(i == TIO_COMMAND_QUERY) return "TIO_COMMAND_QUERY";
    if (i == TIO_COMMAND_WAIT_AND_POP_NEXT) return "TIO_COMMAND_WAIT_AND_POP_NEXT";
    if (i == TIO_COMMAND_WAIT_AND_POP_KEY) return "TIO_COMMAND_WAIT_AND_POP_KEY";
	if(i == TIO_COMMAND_PROPGET ) return "TIO_COMMAND_PROPGET";
	if(i == TIO_COMMAND_PROPSET ) return "TIO_COMMAND_PROPSET";
	if(i == TIO_COMMAND_GROUP_ADD ) return "TIO_COMMAND_GROUP_ADD";
	if(i == TIO_COMMAND_GROUP_SUBSCRIBE) return "TIO_COMMAND_GROUP_SUBSCRIBE";
	if(i == TIO_COMMAND_NEW_GROUP_CONTAINER) return "TIO_COMMAND_NEW_GROUP_CONTAINER";
	

	return "*UNKNOWN*";
}

const char* tio_event_code_to_string(int event_code)
{

	switch (event_code)
	{
	case TIO_COMMAND_PUSH_BACK: return "push_back";
	case TIO_COMMAND_PUSH_FRONT: return "push_front";
	case TIO_COMMAND_SET: return "set";
	case TIO_COMMAND_DELETE: return "delete";
	case TIO_COMMAND_INSERT: return "insert";
	case TIO_COMMAND_CLEAR: return "clear";
	case TIO_EVENT_SNAPSHOT_END: return "snapshot_end";
	default:
		return "INVALID_EVENT";
	}
	return 0;

}

void dump_pr1_message(const char* prefix, struct PR1_MESSAGE* pr1_message)
{
	static int currentMessage = 0;
	unsigned int a;
	char buffer[1024 * 64];
	char myBuffer[1024 * 64];
	char* messageBuffer = myBuffer;
	struct PR1_MESSAGE_HEADER* header = (struct PR1_MESSAGE_HEADER*)pr1_message->stream_buffer->buffer;
	struct PR1_MESSAGE_FIELD_HEADER* field_header;

	if(!g_dump_protocol_messages)
		return;

	pr1_message_parse(pr1_message);

	messageBuffer += sprintf(messageBuffer, "%s pr1_message: message_size=%d, field_count=%d, debug_session_seq_nun=%d, #=%d\n",
		prefix,
		header->message_size,
		header->field_count,
		header->debug_session_seq_nun,
		++currentMessage
		);

	for(a = 0 ; a < pr1_message->field_count ; a++)
	{
		field_header = pr1_message->field_array[a];

		messageBuffer += sprintf(messageBuffer, "  field_id=%d (%s), data_type=%d, data_size=%d, value=",
			field_header->field_id,
			message_field_id_to_string(field_header->field_id),
			field_header->data_type,
			field_header->data_size);
		
		switch(field_header->data_type)
		{
		case MESSAGE_FIELD_TYPE_NONE:
			messageBuffer += sprintf(messageBuffer, "(NONE)");
			break;
		case MESSAGE_FIELD_TYPE_STRING:
			pr1_message_field_get_string(field_header, buffer, sizeof(buffer));
			messageBuffer += sprintf(messageBuffer, "\"%s\"", buffer);
			break;
		case MESSAGE_FIELD_TYPE_INT:
			messageBuffer += sprintf(messageBuffer, "%d", pr1_message_field_get_int(field_header));
			break;
		default:
			messageBuffer += sprintf(messageBuffer, "(UNKNOWN TYPE)");
			break;
		}

		if(field_header->field_id == MESSAGE_FIELD_ID_COMMAND)
			messageBuffer += sprintf(messageBuffer, ", command=%s", tio_command_to_string(pr1_message_field_get_int(field_header)));

		if(field_header->field_id == MESSAGE_FIELD_ID_EVENT_CODE)
			messageBuffer += sprintf(messageBuffer, ", event_code=%s", tio_event_code_to_string(pr1_message_field_get_int(field_header)));

		messageBuffer += sprintf(messageBuffer, "\n");
	}

	g_dump_protocol_messages(myBuffer);
}

void pr1_message_parse(struct PR1_MESSAGE* pr1_message)
{
	unsigned int a;
	struct PR1_MESSAGE_HEADER* header = (struct PR1_MESSAGE_HEADER*)pr1_message->stream_buffer->buffer;
	char* current = (char*)&header[1];
	size_t debugTotalDataSize = 0;

	assert(header->message_size != 0xFFFFFFFF);

	pr1_message->field_count = header->field_count;

	free(pr1_message->field_array);
	
	pr1_message->field_array = (struct PR1_MESSAGE_FIELD_HEADER**) malloc(header->field_count * sizeof(void*));

	for(a = 0 ; a < pr1_message->field_count ; a++)
	{
		pr1_message->field_array[a] = (struct PR1_MESSAGE_FIELD_HEADER*)current;

		size_t offset = pr1_message->field_array[a]->data_size + sizeof(struct PR1_MESSAGE_FIELD_HEADER);
	
		current += offset;
		debugTotalDataSize += offset;

		//
		// sanity checks
		//
		assert(pr1_message->field_array[a]->field_id != 0);
		assert(pr1_message->field_array[a]->field_id <= MESSAGE_FIELD_ID_MAX_VALUE);

		assert(pr1_message->field_array[a]->data_type != 0);
		assert(pr1_message->field_array[a]->data_type <= MESSAGE_FIELD_TYPE_MAX_VALUE);

		assert(pr1_message->field_array[a]->data_size <= header->message_size + sizeof(struct PR1_MESSAGE_HEADER));
		assert(debugTotalDataSize <= header->message_size);
	}

	assert(debugTotalDataSize == header->message_size);
}

struct PR1_MESSAGE_FIELD_HEADER* pr1_message_field_find_by_id(const struct PR1_MESSAGE* pr1_message, unsigned int field_id)
{
	unsigned int a;

	for(a = 0 ; a < pr1_message->field_count ; a++)
		if(pr1_message->field_array[a]->field_id == field_id)
			return pr1_message->field_array[a];

	return NULL;
}



int pr1_message_send(SOCKET socket, struct PR1_MESSAGE* pr1_message, unsigned short debug_seq_num)
{
	void* buffer;
	unsigned int size;

	pr1_message_get_buffer(pr1_message, &buffer, &size, debug_seq_num);

	dump_pr1_message("SEND", pr1_message);

	return socket_send(socket, buffer, size);
}

//
// this function will delete the message EVEN IF IT WAS NOT SEND
//
int pr1_message_send_and_delete(SOCKET socket, struct PR1_MESSAGE* pr1_message, unsigned short debug_seq_num)
{
	int result;
	result = pr1_message_send(socket, pr1_message, debug_seq_num);

	pr1_message_delete(pr1_message);

	return result;
}

struct PR1_MESSAGE* pr1_message_new_get_buffer_for_receive(struct PR1_MESSAGE_HEADER* message_header, void** buffer)
{
	struct PR1_MESSAGE* pr1_message = pr1_message_new();

	stream_buffer_seek(pr1_message->stream_buffer, 0);

	stream_buffer_write(pr1_message->stream_buffer, message_header, sizeof(struct PR1_MESSAGE_HEADER));

	*buffer = stream_buffer_get_write_pointer(pr1_message->stream_buffer, message_header->message_size);

	return pr1_message;
}

int pr1_message_receive(SOCKET socket, struct PR1_MESSAGE** pr1_message, 
	const unsigned* message_header_timeout_in_seconds, const unsigned* message_payload_timeout_in_seconds)
{
	int result;
	struct PR1_MESSAGE_HEADER pr1_message_header;
	void* receive_buffer;
	
	result = socket_receive(socket, &pr1_message_header, sizeof(struct PR1_MESSAGE_HEADER), message_header_timeout_in_seconds);

	if(TIO_FAILED(result))
		return result;

	if(result < sizeof(struct PR1_MESSAGE_HEADER))
	{
		//
		// The header fragmented. Since it's very small (12 bytes now), if it fragments
		// we will consider it to be an error and the connection in invalid state from now on
		//
		return TIO_ERROR_NETWORK;
	}

	*pr1_message = pr1_message_new_get_buffer_for_receive(&pr1_message_header, &receive_buffer);

	result = socket_receive(
		socket, 
		receive_buffer,
		pr1_message_header.message_size,
		message_payload_timeout_in_seconds);

	if((unsigned)result < pr1_message_header.message_size) 
	{
		//
		// We didn't receive the payload before the timeout.
		// We will consider it to be an error and the connection in invalid state from now on
		//
		pr1_message_delete(*pr1_message);
		*pr1_message = NULL;
		return TIO_ERROR_NETWORK;
	}

	if(TIO_FAILED(result))
	{
		pr1_message_delete(*pr1_message);
		*pr1_message = NULL;
		return result;
	}

	assert(result == pr1_message_header.message_size);
	pr1_message_parse(*pr1_message);

	dump_pr1_message("RCEV", *pr1_message);

	return result;
}

/*
int pr1_message_receive_if_available(SOCKET socket, struct PR1_MESSAGE** pr1_message)
{
	int result;
	struct PR1_MESSAGE_HEADER pr1_message_header;
	void* receive_buffer;

	result = socket_receive_if_available(socket, &pr1_message_header, sizeof(struct PR1_MESSAGE_HEADER));

	if(result == 0)
		return 0;

	if(TIO_FAILED(result))
		return result;

	*pr1_message = pr1_message_new_get_buffer_for_receive(&pr1_message_header, &receive_buffer);

	result = socket_receive(
		socket, 
		receive_buffer,
		pr1_message_header.message_size,
		NULL);

	if(TIO_FAILED(result))
	{
		pr1_message_delete(*pr1_message);
		*pr1_message = NULL;
		return result;
	}

	pr1_message_parse(*pr1_message);

	return result;
}
*/


int pr1_message_receive_from_any_cluster(struct TIO_CONNECTION* connection, struct PR1_MESSAGE** pr1_message,
	const unsigned* message_header_timeout_in_seconds, const unsigned* message_payload_timeout_in_seconds,
	struct TIO_CONNECTION** cluster_connection)
{
	int result, i;
	SOCKET socket = 0;
	fd_set recvset;
	struct timeval tv;

	if (message_header_timeout_in_seconds)
	{
		tv.tv_sec = *message_header_timeout_in_seconds;
		tv.tv_usec = 0;
	}

	FD_ZERO(&recvset);
	FD_SET(connection->socket, &recvset);
	for (i = 0; i < connection->clusters_connections_count; ++i)
	{
		FD_SET(connection->clusters_connections[i]->socket, &recvset);
	}

	result = select(FD_SETSIZE, &recvset, NULL, NULL, (message_header_timeout_in_seconds ? &tv : NULL));

	if (result == SOCKET_ERROR)
	{
#ifdef _WIN32
		errno = WSAGetLastError();
#endif
		pr1_set_last_error_description("Error reading data from clusters. Server is down or there is a network problem.");
		return TIO_ERROR_NETWORK;
	}

	if (result == 0)
	{
		return TIO_ERROR_TIMEOUT;
	}

	if (FD_ISSET(connection->socket, &recvset))
	{
		socket = connection->socket;
	}
	else for (i = 0; i < connection->clusters_connections_count; ++i)
	{
		if (FD_ISSET(connection->clusters_connections[i]->socket, &recvset))
		{
			socket = connection->clusters_connections[i]->socket;
			if (cluster_connection)
			{
				*cluster_connection = connection->clusters_connections[i];
			}
			break;
		}
	}

	assert(socket != 0);
	result = pr1_message_receive(socket, pr1_message, message_header_timeout_in_seconds, message_payload_timeout_in_seconds);
	if (TIO_FAILED(result))
		connection->last_error = errno;
	return result;
}

































void tiodata_init(struct TIO_DATA* tiodata)
{
	memset(tiodata, 0, sizeof(struct TIO_DATA));
	tiodata->data_type = TIO_DATA_TYPE_NONE;
}

unsigned int tiodata_get_type(struct TIO_DATA* tiodata)
{
	return tiodata->data_type;
}

void tiodata_set_as_none(struct TIO_DATA* tiodata)
{
	if(!tiodata)
		return;

	if(tiodata_get_type(tiodata) == TIO_DATA_TYPE_STRING)
	{
		free(tiodata->string_);
		tiodata->string_ = NULL;
		tiodata->string_size_ = 0;
	}

	tiodata->data_type = TIO_DATA_TYPE_NONE;
}

void tiodata_free(struct TIO_DATA* tiodata)
{
	tiodata_set_as_none(tiodata);
}



char* tiodata_string_get_buffer(struct TIO_DATA* tiodata, unsigned int min_size)
{
	if(tiodata->data_type == TIO_DATA_TYPE_STRING && tiodata->string_size_ >= min_size)
		return tiodata->string_;

	tiodata_set_as_none(tiodata);
	tiodata->data_type = TIO_DATA_TYPE_STRING;
	tiodata->string_size_ = min_size;
	tiodata->string_ = (char*)malloc(tiodata->string_size_ + 1); // leave room for a \0 just in case

	return tiodata->string_;
}

/*
void tiodata_string_release_buffer(struct TIO_DATA* tiodata)
{
	unsigned int a;
	
	if(tiodata->data_type != TIO_DATA_TYPE_STRING)
		return;

	for(a = 0 ; a < tiodata->string_size_ ; a++) 
	{
		if(tiodata->string_[a] == '\0')
		{
			tiodata->string_size_ = a;
			break;
		}
	}
}
*/


/*void tiodata_set_string(struct TIO_DATA* tiodata, const char* value)
{
	tiodata_set_string_and_size(tiodata, value, strlen(value));
}*/

void tiodata_set_string_and_size(struct TIO_DATA* tiodata, const void* buffer, unsigned int len)
{
	tiodata_set_as_none(tiodata);

	tiodata->data_type = TIO_DATA_TYPE_STRING;

	tiodata->string_size_ = len;
	tiodata->string_ = (char*)malloc(tiodata->string_size_ + 1);
	memcpy(tiodata->string_, buffer, tiodata->string_size_);

	//
	// Tio string can have a \0 inside. But we're going to add a \0 to
	// the end, just in case
	//
	tiodata->string_[tiodata->string_size_] = '\0';
}

void tiodata_set_int(struct TIO_DATA* tiodata, int value)
{
	tiodata_set_as_none(tiodata);

	tiodata->data_type = TIO_DATA_TYPE_INT;

	tiodata->int_ = value;
}

void tiodata_set_double(struct TIO_DATA* tiodata, double value)
{
	tiodata_set_as_none(tiodata);

	tiodata->data_type = TIO_DATA_TYPE_DOUBLE;

	tiodata->double_ = value;
}

void tiodata_copy(const struct TIO_DATA* source, struct TIO_DATA* destination)
{
	tiodata_set_as_none(destination);

	switch(source->data_type)
	{
	case TIO_DATA_TYPE_NONE:
	case TIO_DATA_TYPE_INT:
	case TIO_DATA_TYPE_DOUBLE:
		*destination = *source;
		break;
	case TIO_DATA_TYPE_STRING:
		tiodata_set_string_and_size(destination, source->string_, source->string_size_);
		break;
	};
}

void tiodata_convert_to_string(struct TIO_DATA* tiodata)
{
	char buffer[64]; // 64 bytes will ALWAYS be enough. I'm sure.

	if(!tiodata)
		return;

	if(tiodata->data_type == TIO_DATA_TYPE_STRING)
		return;

	switch(tiodata->data_type)
	{
	case TIO_DATA_TYPE_NONE:
		strcpy(buffer, "[NONE]");
		break;
	case TIO_DATA_TYPE_INT:
		sprintf(buffer, "%d", tiodata->int_);
		break;
	case TIO_DATA_TYPE_DOUBLE:
		sprintf(buffer, "%g", tiodata->double_);
		break;
	};

	buffer[sizeof(buffer)-1] = '\0';

	tiodata_set_string_and_size(tiodata, buffer, strlen32(buffer));
}


void check_correct_thread(struct TIO_CONNECTION* connection)
{
#ifdef _WIN32
	unsigned int current_thread_id = GetCurrentThreadId();
#else
	unsigned int current_thread_id = pthread_self();
#endif

	if(connection->thread_id == 0)
	{
		connection->thread_id = current_thread_id;
		return;
	}

	//
	// TODO: o programa cai nesse assert no macOS mesmo
	// que o programa use somente uma thread. Já revirei a internetcha
	// e não descobro porque isso acontece
	//
	// TODO2: em Windows também, mas talvez porque a implementação
	// da pthread lib não considere o thread id como identificador
	// de uma pthread.
	//
	// TODO3: em Linux (WSL) o mesmo problema foi detectado, apenas
	// agora que foi executado com o client go e p9mdi_client debug.
	//
#if !defined(__APPLE__) && !defined(_WIN32)
	assert(pthread_equal(connection->thread_id, current_thread_id));
#endif
}



int tio_connect(const char* host, short port, struct TIO_CONNECTION** connection)
{
	SOCKET sockfd = -1;
	struct sockaddr_in serv_addr;
#ifndef _WIN32 /* Linux and Mac OS */
	struct sockaddr_un serv_name;
#endif /* Linux and Mac OS */
	char port_string[20];
	struct hostent *server = NULL;
	struct addrinfo* addr_result = NULL, hints;
	int result;
	char buffer[sizeof("going binary") -1];
	char local_endpoint_path[256];
	int isUnixSocket = FALSE, connected = -1;

	isUnixSocket = '/' == host[0];

	if(!g_initialized)
		tio_initialize();

	*connection = NULL;

	if (isUnixSocket)
	{
#ifndef _WIN32 /* Linux and Mac OS */
		sockfd = (SOCKET)socket(AF_LOCAL, SOCK_STREAM, 0);
#else
		pr1_set_last_error_description("Local sockets not available on this platform.");
		errno = WSA_INVALID_PARAMETER;
		result =  TIO_ERROR_NETWORK;
		goto clean_up_and_return;
#endif /* Linux and Mac OS */
	}
	else 
	{
		sockfd = (SOCKET)socket(AF_INET, SOCK_STREAM, 0);
	}

	if (sockfd < 0)
	{
#ifdef _WIN32
		errno = WSAGetLastError();
#endif
		pr1_set_last_error_description("Can't create socket");
		result = TIO_ERROR_NETWORK;
		goto clean_up_and_return;
	}

	sprintf(port_string, "%d", port);

	if (isUnixSocket)
	{
		strcpy(local_endpoint_path, host);
		// ex result: /var/run/tio_2605
		strcat(local_endpoint_path, port_string);

#ifndef _WIN32 /* Linux and Mac OS */
		serv_name.sun_family = AF_LOCAL;
		strncpy(serv_name.sun_path, local_endpoint_path, sizeof(serv_name.sun_path));
		serv_name.sun_path[sizeof(serv_name.sun_path) - 1] = '\0';
#endif
	}
	else 
    {
	    memset(&hints, 0, sizeof(hints));
	    hints.ai_family = AF_INET;
	    hints.ai_socktype = SOCK_STREAM;
	    hints.ai_protocol = IPPROTO_TCP;
	    result = getaddrinfo(host, port_string, &hints, &addr_result);

	    if (addr_result == NULL ) {
#ifdef _WIN32
		errno = WSAGetLastError();
#endif
	    	pr1_set_last_error_description("Can't resolve server name.");
	    	result = TIO_ERROR_NETWORK;
				goto clean_up_and_return;
	    }
	    serv_addr = *(struct sockaddr_in*) addr_result->ai_addr;
	    freeaddrinfo(addr_result);
			addr_result = NULL;
	}

	if (isUnixSocket)
	{
#ifndef _WIN32 /* Linux and Mac OS */
		connected = connect(sockfd, (struct sockaddr*) & serv_name, sizeof(serv_name));
#endif
	}
	else 
    {
		connected = connect(sockfd, (struct sockaddr*) & serv_addr, sizeof(serv_addr));
	}

	if (connected < 0)
	{
#ifdef _WIN32
		errno = WSAGetLastError();
#endif
		pr1_set_last_error_description("Error while trying to connect to server");
		result = TIO_ERROR_NETWORK;
		goto clean_up_and_return;
	}

	result = socket_send(sockfd, "protocol binary\r\n", sizeof("protocol binary\r\n") -1);
	if(TIO_FAILED(result)) 
	{
		pr1_set_last_error_description("Error sending data to server during protocol negotiation");
		result = TIO_ERROR_NETWORK;
		goto clean_up_and_return;
	}

	result = socket_receive(sockfd, buffer, sizeof(buffer), NULL);
	if(TIO_FAILED(result)) 
	{
		pr1_set_last_error_description("Error receiving data to server during protocol negotiation");
		result = TIO_ERROR_NETWORK;
		goto clean_up_and_return;
	}

	// invalid answer
	if(memcmp(buffer, "going binary", sizeof("going binary")-1) !=0)
	{
		pr1_set_last_error_description("Invalid answer from server during protocol negotiation");
		result = TIO_ERROR_PROTOCOL;
		goto clean_up_and_return;
	}

#ifdef _WIN32
	result = 1;
	setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&result, 4);
#endif

	*connection = (struct TIO_CONNECTION*)calloc(1, sizeof(struct TIO_CONNECTION));
	(*connection)->socket = sockfd;
	(*connection)->host = duplicate_string(host);
	(*connection)->port = port;
	(*connection)->event_list_queue_end = NULL;
	(*connection)->open_containers_count = 0;
    (*connection)->containers_buffer_size = 64; // initial buffer size
    (*connection)->containers = malloc(sizeof(void*) * (*connection)->containers_buffer_size);
    memset((*connection)->containers, 0, sizeof(void*) * (*connection)->containers_buffer_size);
	(*connection)->pending_event_count = 0;
	(*connection)->dispatch_events_on_receive = 0;
	(*connection)->thread_id = 0;
	(*connection)->total_messages_received = 0;
	(*connection)->group_event_callback = NULL;
	(*connection)->group_event_cookie = NULL;
	(*connection)->wait_for_answer = TRUE;
	(*connection)->pending_event_count = 0;
	(*connection)->max_pending_event_count = 0;
	(*connection)->pending_answer_count = 0;
	(*connection)->debug_flags = 0;
	result = tio_open(*connection, "__meta__/clusters", "volatile_map", &(*connection)->clusters_container);
	return TIO_SUCCESS;

	clean_up_and_return:
	if(sockfd != -1)
		closesocket(sockfd);
	if(addr_result != NULL)
		freeaddrinfo(addr_result);
	return result;
}

void tio_data_add_to_pr1_message(struct PR1_MESSAGE* pr1_message, unsigned short field_id, const struct TIO_DATA* tio_data)
{
	unsigned short message_field_type = MESSAGE_FIELD_TYPE_NONE;
	const void* buffer = NULL;
	unsigned int data_size = 0;

	if(!tio_data)
		return;

	switch(tio_data->data_type)
	{
	case TIO_DATA_TYPE_NONE:
		message_field_type = MESSAGE_FIELD_TYPE_NONE;
		break;
	case TIO_DATA_TYPE_STRING:
		message_field_type = MESSAGE_FIELD_TYPE_STRING;
		buffer = tio_data->string_;
		data_size = tio_data->string_size_;
		break;
	case TIO_DATA_TYPE_INT:
		message_field_type = MESSAGE_FIELD_TYPE_INT;
		buffer = &tio_data->int_;
		data_size = sizeof(tio_data->int_);
		break;
	case TIO_DATA_TYPE_DOUBLE:
		message_field_type = MESSAGE_FIELD_TYPE_DOUBLE;
		buffer = &tio_data->double_;
		data_size = sizeof(tio_data->double_);
		break;
	};

	pr1_message_add_field(
		pr1_message, 
		field_id, 
		message_field_type,
		buffer, 
		data_size);
}

void pr1_message_field_to_tio_data(const struct PR1_MESSAGE_FIELD_HEADER* field, struct TIO_DATA* tiodata)
{
	if(!tiodata)
		return;

	tiodata_set_as_none(tiodata);

	if(!field)
		return;

	switch(field->data_type)
	{
	case MESSAGE_FIELD_TYPE_NONE:
		break;
	case TIO_DATA_TYPE_STRING:
		tiodata_set_string_and_size(tiodata, &field[1], field->data_size);
		break;
	case TIO_DATA_TYPE_INT:
		tiodata_set_int(tiodata, pr1_message_field_get_int(field));
		break;
	case TIO_DATA_TYPE_DOUBLE:
		tiodata_set_double(tiodata, pr1_message_field_get_double(field));
		break;
	};
}


void pr1_message_field_get_as_tio_data(const struct PR1_MESSAGE* pr1_message, unsigned int field_id, struct TIO_DATA* tiodata)
{
	struct PR1_MESSAGE_FIELD_HEADER* field = NULL;

	tiodata_set_as_none(tiodata);

	field = pr1_message_field_find_by_id(pr1_message, field_id);

	if(!field)
		return;

	pr1_message_field_to_tio_data(field, tiodata);

	return;
}

struct PR1_MESSAGE* tio_generate_data_message(unsigned int command_id, int handle, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
{
	struct PR1_MESSAGE* pr1_message = pr1_message_new();

	pr1_message_add_field_int(pr1_message, MESSAGE_FIELD_ID_COMMAND, command_id);
	pr1_message_add_field_int(pr1_message, MESSAGE_FIELD_ID_HANDLE, handle);

	if(key)
		tio_data_add_to_pr1_message(pr1_message, MESSAGE_FIELD_ID_KEY, key);
	if(value)
		tio_data_add_to_pr1_message(pr1_message, MESSAGE_FIELD_ID_VALUE, value);
	if(metadata)
		tio_data_add_to_pr1_message(pr1_message, MESSAGE_FIELD_ID_METADATA, metadata);

	return pr1_message;
}

int tio_container_send_command(struct TIO_CONTAINER* container, unsigned int command_id, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
{
	SOCKET socket = container->connection->socket;
	int result;

	struct PR1_MESSAGE* pr1_message = 
		tio_generate_data_message(command_id, container->handle, key, value, metadata);

	check_correct_thread(container->connection);

	result = pr1_message_send_and_delete(socket, pr1_message, 0);

	return result;
}


struct EVENT_INFO_NODE
{
	struct EVENT_INFO_NODE* next;
	struct PR1_MESSAGE* message;
};


void events_list_push(struct TIO_CONNECTION* connection, struct PR1_MESSAGE* message)
{
	struct EVENT_INFO_NODE* node = (struct EVENT_INFO_NODE*)malloc(sizeof(struct EVENT_INFO_NODE));

	node->message = message;

	if(connection->event_list_queue_end == NULL)
	{
		node->next = node;
	}
	else
	{
		node->next = connection->event_list_queue_end->next;
		connection->event_list_queue_end->next = node;
	}

	connection->event_list_queue_end = node;

	connection->pending_event_count++;
}

int event_list_is_empty(struct TIO_CONNECTION* connection)
{
	return connection->event_list_queue_end == NULL;
}

struct PR1_MESSAGE* events_list_pop(struct TIO_CONNECTION* connection)
{
	struct EVENT_INFO_NODE* first;
	struct PR1_MESSAGE* pr1_message;

	// empty queue
	if(event_list_is_empty(connection))
		return NULL;

	first = connection->event_list_queue_end->next;

	// last item?
	if(connection->event_list_queue_end->next == connection->event_list_queue_end)
		connection->event_list_queue_end = NULL;
	else
		connection->event_list_queue_end->next = connection->event_list_queue_end->next->next;

	pr1_message = first->message;

	free(first);

	connection->pending_event_count--;

	return pr1_message;
}

int pr1_message_get_error_code(struct PR1_MESSAGE* msg)
{
	struct PR1_MESSAGE_FIELD_HEADER* error_code;
	struct PR1_MESSAGE_FIELD_HEADER* error_description;

	error_code = pr1_message_field_find_by_id(msg, MESSAGE_FIELD_ID_ERROR_CODE);

	if(!error_code)
		return TIO_SUCCESS;

	error_description = pr1_message_field_find_by_id(msg, MESSAGE_FIELD_ID_ERROR_DESC);

	if(error_description)
	{
		pr1_message_field_get_string(error_description, g_last_error_description, MAX_ERROR_DESCRIPTION_SIZE);
	}
	else
		*g_last_error_description = '\0';

	return pr1_message_field_get_int(error_code);
}

void tio_disconnect(struct TIO_CONNECTION* connection)
{
	struct PR1_MESSAGE* pending_event;

	if(!connection)
		return;

	closesocket(connection->socket);

	connection->socket = 0;

	for(pending_event = events_list_pop(connection) ; pending_event != NULL ; pending_event = events_list_pop(connection))
	{
		pr1_message_delete(pending_event);
	}

	while (connection->containers_buffer_size)
	{
		connection->containers_buffer_size--;
		if (connection->containers[connection->containers_buffer_size])
		{
			// we should politely close the handles in tio server with tio_close,
			// but there is no time because a lot of messages is coming
			free(connection->containers[connection->containers_buffer_size]);
			connection->containers[connection->containers_buffer_size] = NULL;
		}
	}

	while (connection->clusters_connections_count)
	{
		connection->clusters_connections_count--;
		if (connection->clusters_connections[connection->clusters_connections_count])
		{
			tio_disconnect(connection->clusters_connections[connection->clusters_connections_count]);
		}
	}

	free(connection->clusters_connections);
	free(connection->containers);
	free(connection->host);
	memset(connection, 0, sizeof(struct TIO_CONNECTION));
	free(connection);
}

const char* tio_get_last_error_description()
{
	return pr1_get_last_error_description();
}

int tio_receive_message(struct TIO_CONNECTION* connection, unsigned int* command_id, struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata)
{
	int result;
	unsigned int a;
	const struct PR1_MESSAGE_FIELD_HEADER* current_field;

	struct PR1_MESSAGE* pr1_message;

	check_correct_thread(connection);
	check_not_on_network_batch(connection);

	result = pr1_message_receive_from_any_cluster(connection, &pr1_message, NULL, NULL, NULL);

	connection->total_messages_received++;
	
	if(TIO_FAILED(result))
		return result;
	
	for(a = 0 ; a < pr1_message->field_count ; a++)
	{
		current_field = pr1_message->field_array[a];
		switch(current_field->field_id)
		{
		case MESSAGE_FIELD_ID_COMMAND:
			*command_id = pr1_message_field_get_int(current_field);
			break;
		case MESSAGE_FIELD_ID_KEY:
			pr1_message_field_to_tio_data(current_field, key);
			break;
		case MESSAGE_FIELD_ID_VALUE:
			pr1_message_field_to_tio_data(current_field, value);
			break;
		case MESSAGE_FIELD_ID_METADATA:
			pr1_message_field_to_tio_data(current_field, metadata);
			break;
		}
	}

	return result;
}

void on_event_receive(struct TIO_CONNECTION* connection, struct PR1_MESSAGE* event_message)
{
	events_list_push(connection, event_message);

	if(connection->pending_event_count >= connection->max_pending_event_count)
		tio_dispatch_pending_events(connection, 0xFFFFFFFF);
}

int register_container(struct TIO_CONNECTION* connection, struct PR1_MESSAGE* message, const char* name, const char* group_name, struct TIO_CONTAINER** container)
{
	struct TIO_CONTAINER* new_container;
	int name_len;
	int group_name_len;

	char* name_copy;
	char* group_name_copy;

	int handle;
	struct PR1_MESSAGE_FIELD_HEADER* handle_field;

	handle_field = pr1_message_field_find_by_id(message, MESSAGE_FIELD_ID_HANDLE);

	if (handle_field == NULL)
		return TIO_ERROR_MISSING_PARAMETER;

	handle = pr1_message_field_get_int(handle_field);

	name_len = strlen32(name) + 1;
	group_name_len = group_name ? strlen32(group_name) + 1 : 0;

	new_container = (struct TIO_CONTAINER*)malloc(sizeof(struct TIO_CONTAINER) + name_len + group_name_len);

	name_copy = (char*)&new_container[1];
	memcpy(name_copy, name, name_len);

	if (group_name_len)
	{
		group_name_copy = name_copy + name_len;
		memcpy(group_name_copy, group_name, group_name_len);
	}
	else
		group_name_copy = NULL;

	new_container->connection = connection;
	new_container->handle = handle;
	new_container->event_callback = NULL;
	new_container->wait_and_pop_next_callback = NULL;
	new_container->wait_and_pop_next_cookie = 0;
	new_container->subscription_cookie = NULL;
	new_container->group_name = group_name_copy;
	new_container->name = name_copy;

	if (handle >= connection->containers_buffer_size)
	{
		int orig_buffer_size = connection->containers_buffer_size;
		connection->containers_buffer_size = handle * 2;
		connection->containers = realloc(connection->containers, sizeof(void*) * connection->containers_buffer_size);
		memset(&connection->containers[orig_buffer_size], 0, (connection->containers_buffer_size - orig_buffer_size) * sizeof(void*));
	}

	connection->open_containers_count++;

	connection->containers[handle] = new_container;

	if (container)
		*container = new_container;

	return TIO_SUCCESS;
}

int on_group_new_container(struct TIO_CONNECTION* connection, struct PR1_MESSAGE* message)
{
	int ret = TIO_SUCCESS;
	struct PR1_MESSAGE_FIELD_HEADER* name_field;
	struct PR1_MESSAGE_FIELD_HEADER* group_name_field;
	char* container_name = NULL;
	char* group_name = NULL;

	name_field = pr1_message_field_find_by_id(message, MESSAGE_FIELD_ID_CONTAINER_NAME);
	group_name_field = pr1_message_field_find_by_id(message, MESSAGE_FIELD_ID_GROUP_NAME);

	if(name_field == NULL || group_name_field ==  NULL)
	{
		ret = TIO_ERROR_MISSING_PARAMETER;
		goto clean_up_and_return;
	}

	pr1_message_field_get_string_malloc(name_field, &container_name);
	pr1_message_field_get_string_malloc(group_name_field, &group_name);
	
	
	ret = register_container(connection, message, container_name, group_name, NULL);

clean_up_and_return:
	if(container_name)
		free(container_name);

	if(group_name)
		free(group_name);

	return ret;
}

//
// return the number of dispatched events
//
int tio_receive_next_pending_event(struct TIO_CONNECTION* connection, const unsigned* timeout_in_seconds)
{
	int result = 0;
	struct PR1_MESSAGE_FIELD_HEADER* command_field;
	struct PR1_MESSAGE* received_message;
	int command;

	check_correct_thread(connection);
	check_not_on_network_batch(connection);

	//
	// In some weird situation (like server sending just the message header and hanging right
	// after that) we can wait for twice the timeout value. I don't think it's an issue...
	//
	result = pr1_message_receive_from_any_cluster(connection, &received_message, timeout_in_seconds, timeout_in_seconds, &connection);

	if(TIO_FAILED(result))
		return result;

	if(result == 0)
	{
		assert(timeout_in_seconds != NULL);
		return 0;
	}

	connection->total_messages_received++;

	command_field = pr1_message_field_find_by_id(received_message, MESSAGE_FIELD_ID_COMMAND);

	if(!command_field) 
	{
		pr1_message_delete(received_message);
		return TIO_ERROR_PROTOCOL;
	}

	command = pr1_message_field_get_int(command_field);

	if(command == TIO_COMMAND_NEW_GROUP_CONTAINER)
	{
		on_group_new_container(connection, received_message);
		pr1_message_delete(received_message);
		return 0;
	}
		
	// MUST be an event
	if(command != TIO_COMMAND_EVENT)
	{
		pr1_message_delete(received_message);
		return TIO_ERROR_PROTOCOL;
	}

	on_event_receive(connection, received_message);

	return 1;
}


int tio_receive_until_not_event(struct TIO_CONNECTION* connection, struct PR1_MESSAGE** response)
{
	int result;
	int command;
	struct PR1_MESSAGE_FIELD_HEADER* command_field;
	struct PR1_MESSAGE* received_message;

	check_correct_thread(connection);
	check_not_on_network_batch(connection);

	// we'll loop until we receive a response (anything that is not an event)
	for(;;)
	{
		result = pr1_message_receive_from_any_cluster(connection, &received_message, NULL, NULL, &connection);
		
		if(TIO_FAILED(result))
			return result;

		connection->total_messages_received++;

		command_field = pr1_message_field_find_by_id(received_message, MESSAGE_FIELD_ID_COMMAND);

		if(!command_field) {
			*response = NULL;
			pr1_message_delete(received_message);
			*response = NULL;
			pr1_set_last_error_description("Missing field MESSAGE_FIELD_ID_COMMAND on the received packet");
			return TIO_ERROR_PROTOCOL;
		}

		command = pr1_message_field_get_int(command_field);

		if(command == TIO_COMMAND_EVENT)
		{
			on_event_receive(connection, received_message);
		}
		else if(command == TIO_COMMAND_NEW_GROUP_CONTAINER)
		{
			on_group_new_container(connection, received_message);
			pr1_message_delete(received_message);
		}
		else
		{
			*response = received_message;
			break;
		}
	}

	return result;
}

int tio_container_send_command_and_get_response(
	struct TIO_CONTAINER* container, unsigned int command_id, 
	const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata,
	struct PR1_MESSAGE** response)
{
	int result;
	
	result = tio_container_send_command(container, command_id, key, value, metadata);
	if(TIO_FAILED(result)) 
		return result;

	result = tio_receive_until_not_event(container->connection, response);
	
	if(TIO_FAILED(result)) 
		return result;

	return result;
}


int tio_container_send_command_and_get_data_response(
	struct TIO_CONTAINER* container, unsigned int command_id, 
	const struct TIO_DATA* input_key,
	struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata)
{
	struct PR1_MESSAGE* response = NULL;
	int result;

	if(key) tiodata_set_as_none(key);
	if(value) tiodata_set_as_none(value);
	if(metadata) tiodata_set_as_none(metadata);

	result = tio_container_send_command(container, command_id, input_key, NULL, NULL);
	if(TIO_FAILED(result)) 
		goto clean_up_and_return;

	result = tio_receive_until_not_event(container->connection, &response);
	if(TIO_FAILED(result)) 
		goto clean_up_and_return;

	result = pr1_message_get_error_code(response);
	if(TIO_FAILED(result)) 
		goto clean_up_and_return;
	
	if(key)
		pr1_message_field_get_as_tio_data(response, MESSAGE_FIELD_ID_KEY, key);

	if(value)
		pr1_message_field_get_as_tio_data(response, MESSAGE_FIELD_ID_VALUE, value);

	if(metadata)
		pr1_message_field_get_as_tio_data(response, MESSAGE_FIELD_ID_METADATA, metadata);

clean_up_and_return:
	pr1_message_delete(response);
	return result;
}

struct PR1_MESSAGE* tio_generate_create_or_open_msg(unsigned int command_id, const char* name, const char* type)
{
	struct PR1_MESSAGE* pr1_message = pr1_message_new();

	pr1_message_add_field_int(pr1_message, MESSAGE_FIELD_ID_COMMAND, command_id);
	pr1_message_add_field_string(pr1_message, MESSAGE_FIELD_ID_NAME, name);

	if(type)
		pr1_message_add_field_string(pr1_message, MESSAGE_FIELD_ID_TYPE, type);

	return pr1_message;
}


int tio_connect_cluster(struct TIO_CONNECTION* connection, const char* host, short port, struct TIO_CONNECTION** cluster_connection)
{
	int ret = TIO_SUCCESS;
	int connected;

	for (connected = 0; connected < connection->clusters_connections_count; ++connected)
	{
		*cluster_connection = connection->clusters_connections[connected];
		if (strcmp((*cluster_connection)->host, host) == 0 && (*cluster_connection)->port == port)
			break;
	}

	if (connected == connection->clusters_connections_count)
	{
		ret = tio_connect(host, port, cluster_connection);

		if (ret == TIO_SUCCESS)
		{
			if (connection->clusters_connections_buffer_size == connection->clusters_connections_count)
			{
				int new_buffer_size = connection->clusters_connections_buffer_size ? 
					connection->clusters_connections_buffer_size * 2 : 64;
				struct TIO_CONNECTION** new_buffer = (struct TIO_CONNECTION**) realloc(connection->clusters_connections, 
					new_buffer_size * sizeof(struct TIO_CONNECTION*));

				if (new_buffer)
				{
					memset(&new_buffer[connection->clusters_connections_buffer_size], 0, 
						(new_buffer_size - connection->clusters_connections_buffer_size) * sizeof(struct TIO_CONNECTION**));
					connection->clusters_connections = new_buffer;
					connection->clusters_connections_buffer_size = new_buffer_size;
				}
			}

			if (connection->clusters_connections_buffer_size > connection->clusters_connections_count)
			{
				connection->clusters_connections[connection->clusters_connections_count++] = *cluster_connection;
			}
			else
			{
				tio_disconnect(*cluster_connection);
				*cluster_connection = NULL;
				ret = TIO_ERROR_OUT_OF_MEMORY;
			}
		}
	}

	return ret;
}


int tio_create_or_open_cluster(struct TIO_CONNECTION* connection, unsigned int command_id, const char* name, const char* type, struct TIO_CONTAINER** container, struct TIO_CONTAINER* clusters)
{
	int tio_create_or_open(struct TIO_CONNECTION* connection, unsigned int command_id, const char* name, const char* type, struct TIO_CONTAINER** container);

	int result;
	char* host = connection->host;
	int port;
	char* port_string;
	struct TIO_DATA search_key;
	struct TIO_DATA value;
	struct TIO_CONNECTION* cluster_connection;

	tiodata_init(&search_key);
	tiodata_set_string_and_size(&search_key, name, (unsigned int) strlen(name));
	tiodata_init(&value);
	connection->last_error = 0;

	result = tio_container_get(connection->clusters_container, &search_key, NULL, &value, NULL);

	if (result == TIO_SUCCESS)
	{
		if (port_string = strchr(value.string_, ':'))
		{
			*port_string++ = 0;
			host = value.string_;
			port = atoi(port_string);
		}
		else
		{
			port = atoi(value.string_);
		}

		result = tio_connect_cluster(connection, host, port, &cluster_connection);

		if (result == TIO_SUCCESS)
		{
			result = tio_create_or_open(cluster_connection, command_id, name, type, container);
		}
	}
	else
	{
		connection->last_error = errno;
	}

	tiodata_free(&search_key);
	tiodata_free(&value);

	return result;
}


int tio_create_or_open(struct TIO_CONNECTION* connection, unsigned int command_id, const char* name, const char* type, struct TIO_CONTAINER** container)
{
	struct PR1_MESSAGE* pr1_message = NULL;
	struct PR1_MESSAGE* response = NULL;
	int result;
	SOCKET socket = connection->socket;

	*container = NULL;
	connection->last_error = 0;

	pr1_message = tio_generate_create_or_open_msg(command_id, name, type);

	result = pr1_message_send_and_delete(socket, pr1_message, 0);
	if (TIO_FAILED(result))
	{
#ifdef _WIN32
		errno = WSAGetLastError();
#endif
		connection->last_error = errno;
	}

	//receive
	result = tio_receive_until_not_event(connection, &response);
	if(TIO_FAILED(result)) 
		goto clean_up_and_return;

	result = pr1_message_get_error_code(response);
	if (TIO_FAILED(result))
	{
		if (connection->clusters_container)
		{
			result = tio_create_or_open_cluster(connection, command_id, name, type, container, connection->clusters_container);
		}
		goto clean_up_and_return;
	}

	result = register_container(connection, response, name, NULL, container);

clean_up_and_return:
	pr1_message_delete(response);
	return result;
}

int tio_create(struct TIO_CONNECTION* connection, const char* name, const char* type, struct TIO_CONTAINER** container)
{
	return tio_create_or_open(connection, TIO_COMMAND_CREATE, name, type, container);
}

int tio_open(struct TIO_CONNECTION* connection, const char* name, const char* type, struct TIO_CONTAINER** container)
{
	return tio_create_or_open(connection, TIO_COMMAND_OPEN, name, type, container);
}

int tio_close(struct TIO_CONTAINER* container)
{
	struct PR1_MESSAGE* request = NULL;
	struct PR1_MESSAGE* response = NULL;
	int handle;
	int result = TIO_SUCCESS;

	if(!container)
		goto clean_up_and_return;

	handle = container->handle;

	request = pr1_message_new();
	pr1_message_add_field_int(request, MESSAGE_FIELD_ID_COMMAND, TIO_COMMAND_CLOSE);
	pr1_message_add_field_int(request, MESSAGE_FIELD_ID_HANDLE, handle);

	result = pr1_message_send_and_delete(container->connection->socket, request, 0);
	if(TIO_FAILED(result)) 
		goto clean_up_and_return;

	result = tio_receive_until_not_event(container->connection, &response);
	if(TIO_FAILED(result)) 
		goto clean_up_and_return;

	result = pr1_message_get_error_code(response);
	if(TIO_FAILED(result))
		goto clean_up_and_return;

	container->connection->containers[handle] = NULL;

clean_up_and_return:
	pr1_message_delete(response);
	free(container);
	return result;
}

/*
	tio_dispatch_pending_events
	returns: number of dispatched events
*/
int tio_dispatch_pending_events(struct TIO_CONNECTION* connection, unsigned int max_events)
{
	unsigned int a;
	struct TIO_CONTAINER* container;
	struct PR1_MESSAGE* event_message;
	struct PR1_MESSAGE_FIELD_HEADER* handle_field;
	struct PR1_MESSAGE_FIELD_HEADER* event_code_field;
	struct TIO_DATA key, value, metadata;
	int handle, event_code;
	void* cookie;
	event_callback_t event_callback;

	tiodata_init(&key);
	tiodata_init(&value);
	tiodata_init(&metadata);

	for(a = 0 ; a < max_events ; a++)
	{
		event_message = events_list_pop(connection);

		if(!event_message)
			break;

		handle_field = pr1_message_field_find_by_id(event_message, MESSAGE_FIELD_ID_HANDLE);
		event_code_field = pr1_message_field_find_by_id(event_message, MESSAGE_FIELD_ID_EVENT_CODE);

		if(handle_field &&
			handle_field->data_type == TIO_DATA_TYPE_INT &&
			event_code_field &&
			event_code_field->data_type == TIO_DATA_TYPE_INT)
		{
			handle = pr1_message_field_get_int(handle_field);
			event_code = pr1_message_field_get_int(event_code_field);

			pr1_message_field_to_tio_data(pr1_message_field_find_by_id(event_message, MESSAGE_FIELD_ID_KEY), &key);
			pr1_message_field_to_tio_data(pr1_message_field_find_by_id(event_message, MESSAGE_FIELD_ID_VALUE), &value);
			pr1_message_field_to_tio_data(pr1_message_field_find_by_id(event_message, MESSAGE_FIELD_ID_METADATA), &metadata);

			container = connection->containers[handle];
			event_callback = NULL;

            if (event_code == TIO_COMMAND_WAIT_AND_POP_NEXT)
            {
                event_callback = container->wait_and_pop_next_callback;
                cookie = container->wait_and_pop_next_cookie;
            }
            else
            {
			if(container->group_name == NULL)
			{
				event_callback = container->event_callback;
				cookie = container->subscription_cookie;
			}
			else
			{
				event_callback = connection->group_event_callback;
				cookie = connection->group_event_cookie;
			}
            }

			if(event_callback)
				event_callback(TIO_SUCCESS, container, cookie, event_code, container->group_name, container->name, &key, &value, &metadata);
		}

		pr1_message_delete(event_message);
	}

	tiodata_set_as_none(&key);
	tiodata_set_as_none(&value);
	tiodata_set_as_none(&metadata);

	return a;
}


int tio_ping(struct TIO_CONNECTION* connection, char* payload)
{
	struct PR1_MESSAGE* pr1_message = NULL;
	struct PR1_MESSAGE* response = NULL;
	struct PR1_MESSAGE_FIELD_HEADER* value_field = NULL;
	int result;
	unsigned int payload_len;
	SOCKET socket = connection->socket;

	payload_len = strlen32(payload);

	pr1_message = pr1_message_new();

	pr1_message_add_field_int(pr1_message, MESSAGE_FIELD_ID_COMMAND, TIO_COMMAND_PING);
	pr1_message_add_field_string(pr1_message, MESSAGE_FIELD_ID_VALUE, payload);

	pr1_message_send_and_delete(socket, pr1_message, 0);

	//receive
	result = tio_receive_until_not_event(connection, &response);
	if(TIO_FAILED(result)) 
		goto clean_up_and_return;

	value_field = pr1_message_field_find_by_id(response, MESSAGE_FIELD_ID_VALUE);

	if(!value_field) {
		result = TIO_ERROR_PROTOCOL;
		goto clean_up_and_return;
	}

	if(payload_len != value_field->data_size)
	{
		result = TIO_ERROR_PROTOCOL;
		goto clean_up_and_return;
	}

	if(memcmp(payload, pr1_message_field_get_buffer(value_field), payload_len) != 0)
	{
		result = TIO_ERROR_PROTOCOL;
		goto clean_up_and_return;
	}

clean_up_and_return:
	pr1_message_delete(response);
	return result;

}


const char* tio_container_name(struct TIO_CONTAINER* container)
{
	return container->name;
}

int tio_container_input_command(struct TIO_CONTAINER* container, unsigned short command_id, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
{
	struct PR1_MESSAGE* response = NULL;
	int result;

	if(container->connection->wait_for_answer)
	{
		result = tio_container_send_command_and_get_response(container, command_id, key, value, metadata, &response);

		if(TIO_FAILED(result)) 
			goto clean_up_and_return;

		result = pr1_message_get_error_code(response);

		if(TIO_FAILED(result)) 
			goto clean_up_and_return;

		result = TIO_SUCCESS;
	}
	else
	{
		result = tio_container_send_command(container, command_id, key, value, metadata);

		if(TIO_FAILED(result)) 
			goto clean_up_and_return;

		container->connection->pending_event_count++;
	}

clean_up_and_return:
	pr1_message_delete(response);
	return result;
}

int tio_container_propset(struct TIO_CONTAINER* container, const struct TIO_DATA* key, const struct TIO_DATA* value)
{
	return tio_container_input_command(container, TIO_COMMAND_PROPSET, key, value, NULL);
}

int tio_container_push_back(struct TIO_CONTAINER* container, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
{
	return tio_container_input_command(container, TIO_COMMAND_PUSH_BACK, key, value, metadata);
}

int tio_container_push_front(struct TIO_CONTAINER* container, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
{
	return tio_container_input_command(container, TIO_COMMAND_PUSH_FRONT, key, value, metadata);
}

int tio_container_set(struct TIO_CONTAINER* container, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
{
	return tio_container_input_command(container, TIO_COMMAND_SET, key, value, metadata);
}

int tio_container_insert(struct TIO_CONTAINER* container, const struct TIO_DATA* key, const struct TIO_DATA* value, const struct TIO_DATA* metadata)
{
	return tio_container_input_command(container, TIO_COMMAND_INSERT, key, value, metadata);
}

int tio_container_clear(struct TIO_CONTAINER* container)
{
	return tio_container_input_command(container, TIO_COMMAND_CLEAR, NULL, NULL, NULL);
}

int tio_container_delete(struct TIO_CONTAINER* container, const struct TIO_DATA* key)
{
	return tio_container_input_command(container, TIO_COMMAND_DELETE, key, NULL, NULL);
}

int tio_container_pop_back(struct TIO_CONTAINER* container, struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata)
{
	return tio_container_send_command_and_get_data_response(
		container,
		TIO_COMMAND_POP_BACK,
		NULL,
		key,
		value,
		metadata);
}

int tio_container_pop_front(struct TIO_CONTAINER* container, struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata)
{
	return tio_container_send_command_and_get_data_response(
		container,
		TIO_COMMAND_POP_FRONT,
		NULL,
		key,
		value,
		metadata);
}

int tio_container_get(struct TIO_CONTAINER* container, const struct TIO_DATA* search_key, struct TIO_DATA* key, struct TIO_DATA* value, struct TIO_DATA* metadata)
{
	return tio_container_send_command_and_get_data_response(
		container,
		TIO_COMMAND_GET,
		search_key,
		key,
		value,
		metadata);
}

int tio_container_propget(struct TIO_CONTAINER* container, const struct TIO_DATA* search_key, struct TIO_DATA* value)
{
	return tio_container_send_command_and_get_data_response(
		container,
		TIO_COMMAND_PROPGET,
		search_key,
		NULL,
		value,
		NULL);
}

int tio_container_get_count(struct TIO_CONTAINER* container, int* count)
{
	int result;
	struct TIO_DATA count_value;

	tiodata_init(&count_value);

	result = tio_container_send_command_and_get_data_response(
		container,
		TIO_COMMAND_COUNT,
		NULL,
		NULL,
		&count_value,
		NULL);

	if(TIO_FAILED(result))
		goto clean_up_and_return;

	assert(count_value.data_type == TIO_DATA_TYPE_INT);

	if(count_value.data_type != TIO_DATA_TYPE_INT)
	{
		pr1_set_last_error_description("Expected count_value.data_type to be INT. It is not.");
		return TIO_ERROR_GENERIC;
	}

	*count = count_value.int_;

	result = TIO_SUCCESS;

clean_up_and_return:
	tiodata_set_as_none(&count_value);
	return result;
}


int tio_container_query(struct TIO_CONTAINER* container, int start, int end, 
						const char* regex,
						query_callback_t query_callback, void* cookie)
{
	int result;
	struct PR1_MESSAGE* request = pr1_message_new();
	struct PR1_MESSAGE* response = NULL;
	struct PR1_MESSAGE* query_item = NULL;
	struct PR1_MESSAGE_FIELD_HEADER* query_id_field = NULL;
	struct PR1_MESSAGE_FIELD_HEADER* command_field = NULL;
	struct TIO_DATA key, value, metadata;
	int query_id;

	pr1_message_add_field_int(request, MESSAGE_FIELD_ID_COMMAND, TIO_COMMAND_QUERY);
	pr1_message_add_field_int(request, MESSAGE_FIELD_ID_HANDLE, container->handle);
	pr1_message_add_field_int(request, MESSAGE_FIELD_ID_START_RECORD, start);
	pr1_message_add_field_int(request, MESSAGE_FIELD_ID_END, end);

	if(regex)
		pr1_message_add_field_string(request, MESSAGE_FIELD_ID_QUERY_EXPRESSION, regex);


	result = pr1_message_send_and_delete(container->connection->socket, request, 0);
	if(TIO_FAILED(result))
		goto clean_up_and_return;

	result = tio_receive_until_not_event(container->connection, &response);
	if(TIO_FAILED(result))
		goto clean_up_and_return;


	query_id_field = pr1_message_field_find_by_id(response, MESSAGE_FIELD_ID_QUERY_ID);

	if(!query_id_field || query_id_field->data_type != MESSAGE_FIELD_TYPE_INT)
	{
		result = TIO_ERROR_PROTOCOL;
		goto clean_up_and_return;
	}

	query_id = pr1_message_field_get_int(query_id_field);

	tiodata_init(&key); tiodata_init(&value); tiodata_init(&metadata);

	for(;;)
	{
		result = tio_receive_until_not_event(container->connection, &query_item);
		
		if(TIO_FAILED(result))
			goto clean_up_and_return;

		command_field = pr1_message_field_find_by_id(query_item, MESSAGE_FIELD_ID_COMMAND);
		if(!command_field || 
		   command_field->data_type != TIO_DATA_TYPE_INT ||
		   pr1_message_field_get_int(command_field) != TIO_COMMAND_QUERY_ITEM)
		{
			result = TIO_ERROR_PROTOCOL;
			goto clean_up_and_return;
		}

		tiodata_set_as_none(&key); tiodata_set_as_none(&value); tiodata_set_as_none(&metadata);

		pr1_message_field_get_as_tio_data(query_item, MESSAGE_FIELD_ID_KEY, &key);
		pr1_message_field_get_as_tio_data(query_item, MESSAGE_FIELD_ID_VALUE, &value);
		pr1_message_field_get_as_tio_data(query_item, MESSAGE_FIELD_ID_METADATA, &metadata);

		//
		// empty field means query is over
		//
		if(key.data_type == TIO_DATA_TYPE_NONE)
			break;

		query_callback(TIO_SUCCESS, container, cookie, query_id, container->name, &key, &value, &metadata);
	}


clean_up_and_return:
	tiodata_set_as_none(&key); tiodata_set_as_none(&value); tiodata_set_as_none(&metadata);
	pr1_message_delete(response);
	pr1_message_delete(query_item);

	return result;
}

int tio_container_subscribe(struct TIO_CONTAINER* container, struct TIO_DATA* start, event_callback_t event_callback, void* cookie)
{
	int result;
	
	result = tio_container_input_command(container, TIO_COMMAND_SUBSCRIBE, start, NULL, NULL);
	if(TIO_FAILED(result)) return result;

	container->event_callback = event_callback;
	container->subscription_cookie = cookie;

	return TIO_SUCCESS;
}

int tio_container_wait_and_pop_next(struct TIO_CONTAINER* container, event_callback_t event_callback, void* cookie)
{
    int result;

    container->wait_and_pop_next_callback = event_callback;
    container->wait_and_pop_next_cookie = cookie;

    result = tio_container_input_command(container, TIO_COMMAND_WAIT_AND_POP_NEXT, NULL, NULL, NULL);
    if (TIO_FAILED(result)) return result;

    return TIO_SUCCESS;
}


int tio_container_unsubscribe(struct TIO_CONTAINER* container)
{
	int result;

	container->event_callback = NULL;
	container->subscription_cookie = NULL;

	result = tio_container_input_command(container, TIO_COMMAND_UNSUBSCRIBE, NULL, NULL, NULL);
	if(TIO_FAILED(result)) return result;

	return TIO_SUCCESS;
}

int tio_group_add(struct TIO_CONNECTION* connection, const char* group_name, const char* container_name)
{
	int result;
	struct PR1_MESSAGE* request = pr1_message_new();
	struct PR1_MESSAGE* response = NULL;

	pr1_message_add_field_int(request, MESSAGE_FIELD_ID_COMMAND, TIO_COMMAND_GROUP_ADD);
	pr1_message_add_field_string(request, MESSAGE_FIELD_ID_GROUP_NAME, group_name);
	pr1_message_add_field_string(request, MESSAGE_FIELD_ID_CONTAINER_NAME, container_name);


	result = pr1_message_send_and_delete(connection->socket, request, 0);
	if(TIO_FAILED(result))
		goto clean_up_and_return;

	result = tio_receive_until_not_event(connection, &response);
	if(TIO_FAILED(result))
		goto clean_up_and_return;

	result = pr1_message_get_error_code(response);
	if(TIO_FAILED(result)) 
		goto clean_up_and_return;

	result = TIO_SUCCESS;
	
clean_up_and_return:
	pr1_message_delete(response);

	return result;
}

int tio_group_set_subscription_callback(struct TIO_CONNECTION* connection,  event_callback_t callback, void* cookie)
{
	connection->group_event_callback = callback;
	connection->group_event_cookie = cookie;
	return 0;
}


int tio_group_subscribe(struct TIO_CONNECTION* connection, const char* group_name, const char* start)
{
	int result;
	struct PR1_MESSAGE* request = NULL;
	struct PR1_MESSAGE* response = NULL;

	check_correct_thread(connection);

	request = pr1_message_new();

	pr1_message_add_field_int(request, MESSAGE_FIELD_ID_COMMAND, TIO_COMMAND_GROUP_SUBSCRIBE);
	pr1_message_add_field_string(request, MESSAGE_FIELD_ID_GROUP_NAME, group_name);

	if(start)
		pr1_message_add_field_string(request, MESSAGE_FIELD_ID_START_RECORD, start);

	result = pr1_message_send_and_delete(connection->socket, request, 0);
	if(TIO_FAILED(result))
		goto clean_up_and_return;

	result = tio_receive_until_not_event(connection, &response);
	if(TIO_FAILED(result))
		goto clean_up_and_return;

	result = pr1_message_get_error_code(response);
	if(TIO_FAILED(result)) 
		goto clean_up_and_return;

	result = TIO_SUCCESS;

clean_up_and_return:
	pr1_message_delete(response);

	return result;
}


void tio_begin_network_batch(struct TIO_CONNECTION* connection)
{
	assert(connection->pending_event_count == 0);
	assert(connection->wait_for_answer == TRUE);

	connection->wait_for_answer = FALSE;
}

void tio_finish_network_batch(struct TIO_CONNECTION* connection)
{
	struct PR1_MESSAGE* response;
	int result, a;
	assert(connection->wait_for_answer == FALSE);

	connection->wait_for_answer = TRUE;

	for(a = 0 ; a < connection->pending_event_count ; a++)
	{
		result = tio_receive_until_not_event(connection, &response);

		if(result > 0)
			pr1_message_delete(response);
	}

	connection->pending_event_count = 0;
	
}

void check_not_on_network_batch(struct TIO_CONNECTION* connection)
{
	assert(connection->wait_for_answer == TRUE);
}



void tio_initialize()
{
#ifdef _WIN32
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2,2), &wsadata);
#endif

	g_initialized = TRUE;
}

