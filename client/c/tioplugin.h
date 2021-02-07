#pragma once

//
// for plugins
//
struct KEY_AND_VALUE
{
	const char* key;
	const char* value;
};

typedef void (*tio_plugin_start_t)(void* container_manager, struct KEY_AND_VALUE* parameters);
typedef void (*tio_plugin_stop_t)();
