#include "Core.h"
#define CC_NO_UPDATER
#define CC_NO_DYNLIB
#define CC_NO_SOCKETS
#define CC_NO_THREADING
#define DEFAULT_COMMANDLINE_FUNC
#include "Platform.h"
#include "String_.h"
#include "Logger.h"
#include "Constants.h"
#include "Errors.h"
#include "Funcs.h"
#include "Utils.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "_PlatformBase.h"
#include "main_impl.h"
#include "../../../include/harp_api.h"

const char* Platform_AppNameSuffix = " ZenOS";
const cc_result ReturnCode_FileShareViolation = 1000000000;
const cc_result ReturnCode_FileNotFound = ENOENT;
const cc_result ReturnCode_PathNotFound = ENOENT;
const cc_result ReturnCode_DirectoryExists = EEXIST;
cc_uint8 Platform_Flags = PLAT_FLAG_SINGLE_PROCESS | PLAT_FLAG_APP_EXIT;
cc_bool Platform_ReadonlyFilesystem;
cc_bool Process_OpenSupported = false;

static void LogRaw(const char* msg, int len) {
	write(STDOUT_FILENO, msg, len);
	write(STDOUT_FILENO, "\n", 1);
}

void Platform_Log(const char* msg, int len) {
	LogRaw(msg, len);
}

TimeMS DateTime_CurrentUTC(void) {
	struct timeval cur;
	gettimeofday(&cur, NULL);
	return (cc_uint64)cur.tv_sec + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct cc_datetime* t) {
	struct timeval cur;
	struct tm* tm;
	time_t secs;
	gettimeofday(&cur, NULL);
	secs = cur.tv_sec;
	tm = localtime(&secs);
	if (!tm) memset(t, 0, sizeof(*t));
	else {
		t->year = tm->tm_year + 1900;
		t->month = tm->tm_mon + 1;
		t->day = tm->tm_mday;
		t->hour = tm->tm_hour;
		t->minute = tm->tm_min;
		t->second = tm->tm_sec;
	}
}

cc_uint64 Stopwatch_Measure(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (cc_uint64)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 beg, cc_uint64 end) {
	if (end < beg) return 0;
	return (end - beg) / 1000;
}

void CrashHandler_Install(void) { }

CC_NOINLINE void Process_Abort2(cc_result result, const char* raw_msg) {
	if (raw_msg) Platform_Log(raw_msg, String_Length(raw_msg));
	exit(result ? (int)result : 1);
}

void Platform_EncodePath(cc_filepath* dst, const cc_string* src) {
	String_EncodeUtf8(dst->buffer, src);
}

void Platform_DecodePath(cc_string* dst, const cc_filepath* path) {
	String_AppendUtf8(dst, path->buffer, String_Length(path->buffer));
}

void Directory_GetCachePath(cc_string* path) { }

cc_result Directory_Create2(const cc_filepath* path) {
	return mkdir(path->buffer, 0755) == -1 ? errno : 0;
}

int File_Exists(const cc_filepath* path) {
	struct stat st;
	return stat(path->buffer, &st) == 0 && S_ISREG(st.st_mode);
}

cc_result Directory_Enum(const cc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	cc_string path;
	char pathBuffer[FILENAME_SIZE];
	cc_filepath native;
	DIR* dir;
	struct dirent* entry;
	int res;

	Platform_EncodePath(&native, dirPath);
	dir = opendir(native.buffer);
	if (!dir) return errno;

	String_InitArray(path, pathBuffer);
	errno = 0;
	while ((entry = readdir(dir))) {
		struct stat st;
		char full[NATIVE_STR_LEN];
		int isDir = 0;

		if (entry->d_name[0] == '.' && entry->d_name[1] == '\0') continue;
		if (entry->d_name[0] == '.' && entry->d_name[1] == '.' && entry->d_name[2] == '\0') continue;

		path.length = 0;
		String_Format1(&path, "%s/", dirPath);
		String_AppendUtf8(&path, entry->d_name, String_Length(entry->d_name));

		String_EncodeUtf8(full, &path);
		if (stat(full, &st) == 0) isDir = S_ISDIR(st.st_mode);
		callback(&path, obj, isDir);
		errno = 0;
	}

	res = errno;
	closedir(dir);
	return res;
}

static cc_result File_Do(cc_file* file, const char* path, int flags) {
	*file = open(path, flags, 0644);
	return *file == -1 ? errno : 0;
}

cc_result File_Open(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDONLY);
}

cc_result File_Create(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT | O_TRUNC);
}

cc_result File_OpenOrCreate(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT);
}

cc_result File_Read(cc_file file, void* data, cc_uint32 count, cc_uint32* bytesRead) {
	ssize_t res = read(file, data, count);
	if (res < 0) { *bytesRead = 0; return errno; }
	*bytesRead = (cc_uint32)res;
	return 0;
}

cc_result File_Write(cc_file file, const void* data, cc_uint32 count, cc_uint32* bytesWrote) {
	ssize_t res = write(file, data, count);
	if (res < 0) { *bytesWrote = 0; return errno; }
	*bytesWrote = (cc_uint32)res;
	return 0;
}

cc_result File_Close(cc_file file) {
	return close(file) == -1 ? errno : 0;
}

cc_result File_Seek(cc_file file, int offset, int seekType) {
	static const int modes[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
	return lseek(file, offset, modes[seekType]) == -1 ? errno : 0;
}

cc_result File_Position(cc_file file, cc_uint32* pos) {
	off_t res = lseek(file, 0, SEEK_CUR);
	if (res == -1) { *pos = 0; return errno; }
	*pos = (cc_uint32)res;
	return 0;
}

cc_result File_Length(cc_file file, cc_uint32* len) {
	struct stat st;
	if (fstat(file, &st) == -1) { *len = 0; return errno; }
	*len = (cc_uint32)st.st_size;
	return 0;
}

void Thread_Sleep(cc_uint32 milliseconds) {
	usleep(milliseconds * 1000);
}

void Platform_LoadSysFonts(void) { }

cc_result Process_StartGame2(const cc_string* args, int numArgs) {
	return SetGameArgs(args, numArgs);
}

void Process_Exit(cc_result code) {
	exit((int)code);
}

cc_result Process_StartOpen(const cc_string* args) {
	return ERR_NOT_SUPPORTED;
}

void Platform_Init(void) { }
void Platform_Free(void) { }
cc_result Platform_SetDefaultCurrentDirectory(int argc, char **argv) { return 0; }

cc_bool Platform_DescribeError(cc_result res, cc_string* dst) {
	const char* msg = strerror((int)res);
	if (!msg) return false;
	String_AppendUtf8(dst, msg, String_Length(msg));
	return true;
}

cc_result Platform_GetEntropy(void* data, int len) {
	int fd = open("/dev/urandom", O_RDONLY);
	ssize_t got;
	if (fd < 0) return ERR_NOT_SUPPORTED;
	got = read(fd, data, len);
	close(fd);
	return got == len ? 0 : ERR_NOT_SUPPORTED;
}

cc_result Platform_Encrypt(const void* data, int len, cc_string* dst) {
	return ERR_NOT_SUPPORTED;
}

cc_result Platform_Decrypt(const void* data, int len, cc_string* dst) {
	return ERR_NOT_SUPPORTED;
}

int main(int argc, char** argv) {
	if (!harp_online()) {
		zen_log("ClassiCube: WM unreachable.", 2, 1);
		return -1;
	}
	cc_result res;
	SetupProgram(argc, argv);
	do {
		res = RunProgram(argc, argv);
	} while (Platform_IsSingleProcess() && (Window_Main.Exists || HasPendingGameArgs()));
	Window_Free();
	Process_Exit(res);
	return (int)res;
}