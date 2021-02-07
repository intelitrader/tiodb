#pragma once
#define _CRT_SECURE_NO_WARNINGS

#ifdef _WIN32
#pragma warning( push )
#pragma warning( disable: 4244 ) // boost stinks
#endif
#include <boost/program_options.hpp>
#include <boost/process.hpp>
#include <boost/process/async.hpp>
#include <boost/asio.hpp>
#ifdef _WIN32
#pragma warning( pop )
#endif

#include <stdio.h>
#include <ctype.h>

#include <memory>
#include <thread>
#include <functional>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <atomic>

#include <iostream>
#include <sstream>

#include <chrono>
