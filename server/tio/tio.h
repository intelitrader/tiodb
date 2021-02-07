#pragma once

namespace tio
{
	enum IoServiceMethod
	{
		run,
		poll
	};

	struct SERVER_OPTIONS
	{
		IoServiceMethod ioServiceMethod;
		unsigned short ioServicePollSleepTime;
	};
}
