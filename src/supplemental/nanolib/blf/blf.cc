#include "nng/supplemental/nanolib/blf.h"
#include "nng/supplemental/nanolib/log.h"
#include "queue.h"
#include <Vector/BLF.h>
#include <assert.h>
#include <atomic>
#include <bitset>
#include <codecvt>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <inttypes.h>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
using namespace std;

#define UINT64_MAX_DIGITS 20
#define _Atomic(X) std::atomic<X>
static atomic_bool is_available = { false };
#define WAIT_FOR_AVAILABLE    \
	while (!is_available) \
		nng_msleep(10);
static conf_blf *g_conf = NULL;

#define DO_IT_IF_NOT_NULL(func, arg1, arg2) \
	if (arg1) {                         \
		func(arg1, arg2);           \
	}

#define FREE_IF_NOT_NULL(free, size) DO_IT_IF_NOT_NULL(nng_free, free, size)


CircularQueue   blf_queue;
CircularQueue   blf_file_queue;
pthread_mutex_t blf_queue_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  blf_queue_not_empty = PTHREAD_COND_INITIALIZER;

static bool
directory_exists(const std::string &directory_path)
{
	struct stat buffer;
	return (stat(directory_path.c_str(), &buffer) == 0 &&
	    S_ISDIR(buffer.st_mode));
}

static bool
create_directory(const std::string &directory_path)
{
	int status = mkdir(
	    directory_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	return (status == 0);
}
