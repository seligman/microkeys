#pragma once

// Common includes for the platform neutral core.  Nothing in src/core may
// include an OS header, so the same code builds for Windows, macOS, and the
// Linux unit tests.

#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include <deque>
#include <thread>

using namespace std;

// Log levels, shared with the Python layer
#define LOG_INFO	0
#define LOG_OUTPUT	1
#define LOG_ERROR	2
#define LOG_DEBUG	3

// Modifier bits, shared with the Python layer
#define MOD_KEY_CTRL	1
#define MOD_KEY_ALT		2
#define MOD_KEY_SHIFT	4
#define MOD_KEY_META	8

// Kinds of jobs the UI thread hands to the script thread
#define FIRE_HOTKEY		1
#define FIRE_HOTSTRING	2
#define FIRE_COMMAND	3
