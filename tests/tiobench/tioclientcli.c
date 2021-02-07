#include "..\..\client\c\tioclient.h"
#include <Windows.h>
#include <malloc.h>
#include <stdio.h>

#pragma comment(lib, "tioclient.lib")

LONG g_name = 0;
const int g_containersByThread = 1000;

DWORD WINAPI create_container_as_crazy_person(PVOID* pv)
{
	DWORD ret = ERROR_SUCCESS;
	int tioerr = 0;
	int tryconnect = 5;
	struct TIO_CONNECTION* connection = NULL;
	struct TIO_CONTAINER* container = NULL;
	LONG name = -1;
	CHAR buffer[33];
	int i;

	while ((tioerr = tio_connect("localhost", 2605, &connection)) != 0 && --tryconnect)
		Sleep(1000);

	if (tioerr == 0 )
	{
		for (i = 0; i < g_containersByThread; ++i)
		{
			name = InterlockedIncrement(&g_name);
			sprintf(buffer, "%d", (int)name);

			//if (name % g_containersByThread == 0)
				//printf("container #%d\n", (int)name);

			if (tio_create(connection, buffer, "volatile_list", &container) == 0)
			{
				tio_close(container);
			}
			else
			{
				ret = (DWORD)tioerr;
				printf("error creating container %s\n", buffer);
				break;
			}
		}
	}
	else
	{
		printf("error %d connecting to tio\n", tioerr);
		ret = (DWORD)tioerr;
	}

	return ret;
}

int create_container_as_crazy_person_test()
{
	int i = 1000;
	HANDLE threads[1000];
	size_t nthreads = sizeof(threads) / sizeof(HANDLE);
	DWORD wait, err;

	for (i = 0; i < nthreads; ++i)
	{
		threads[i] = CreateThread(NULL, 0, create_container_as_crazy_person, NULL, 0, NULL);
		if (!threads[i])
		{
			err = (int) GetLastError();
			printf("error %d creating thread; aborting\n", (int) err);
			return (int) err;
		}
	}

	for (i = 0; i < sizeof(threads) / sizeof(HANDLE); ++i)
	{
		wait = WaitForSingleObject(threads[i], INFINITE);

		if (wait == WAIT_OBJECT_0)
		{
			if (GetExitCodeThread(threads[i], &err))
			{
				err = GetLastError();
			}
		}
		else err = GetLastError();

		CloseHandle(threads[i]);

		if (err != ERROR_SUCCESS)
		{
			printf("error %d getting exit code from thread %d; aborting\n", (int) err, i);
			return (int) err;
		}
	}

	printf("%d container(s) created from %d thread(s); last container #%d\n", 
		(int) (g_containersByThread * nthreads), (int) nthreads, g_name);
}

