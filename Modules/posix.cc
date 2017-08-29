#include "posix.hh"

#include <inttypes.h>

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Analysis.hh"
#include "../Types/Strings.hh"
#include "../Types/List.hh"
#include "../Types/Dictionary.hh"

using namespace std;



extern shared_ptr<GlobalAnalysis> global;

static wstring __doc__ = L"\
This module provides access to operating system functionality that is\n\
standardized by the C Standard and the POSIX standard (a thinly\n\
disguised Unix interface). Refer to the library manual and\n\
corresponding Unix manual entries for more information on calls.";



static void posix_closerange(int64_t fd, int64_t end_fd) {
  for (; fd < end_fd; fd++) {
    close(fd);
  }
}

static BytesObject* posix_read(int64_t fd, int64_t buffer_size) {
  BytesObject* ret = bytes_new(NULL, NULL, buffer_size);
  ssize_t bytes_read = read(fd, ret->data, buffer_size);
  if (bytes_read >= 0) {
    ret->count = bytes_read;
  } else {
    ret->count = 0;
  }
  return ret;
}

static int64_t posix_write(int64_t fd, BytesObject* data) {
  return write(fd, data->data, data->count);
}

static int64_t posix_open(UnicodeObject* path, int64_t flags, int64_t mode) {
  BytesObject* path_bytes = encode_ascii(path);
  int64_t ret = open(path_bytes->data, flags, mode);
  delete_reference(path_bytes);
  delete_reference(path);
  return ret;
}

static int64_t posix_execv(UnicodeObject* path, ListObject* args) {

  BytesObject* path_bytes = encode_ascii(path);

  vector<BytesObject*> args_objects;
  vector<char*> args_pointers;
  for (size_t x = 0; x < args->count; x++) {
    args_objects.emplace_back(encode_ascii(reinterpret_cast<UnicodeObject*>(args->items[x])));
    args_pointers.emplace_back(args_objects.back()->data);
  }
  args_pointers.emplace_back(nullptr);

  int ret = execv(path_bytes->data, args_pointers.data());

  delete_reference(path_bytes);
  for (auto& o : args_objects) {
    delete_reference(o);
  }

  return ret;
}

static int64_t posix_execve(UnicodeObject* path, ListObject* args,
    DictionaryObject* env) {

  BytesObject* path_bytes = encode_ascii(path);

  vector<BytesObject*> args_objects;
  vector<char*> args_pointers;
  for (size_t x = 0; x < args->count; x++) {
    args_objects.emplace_back(encode_ascii(reinterpret_cast<UnicodeObject*>(args->items[x])));
    args_pointers.emplace_back(args_objects.back()->data);
  }
  args_pointers.emplace_back(nullptr);

  vector<char*> envs_pointers;
  DictionaryObject::SlotContents dsc;
  while (dictionary_next_item(env, &dsc)) {
    BytesObject* key_bytes = encode_ascii(reinterpret_cast<UnicodeObject*>(dsc.key));
    BytesObject* value_bytes = encode_ascii(reinterpret_cast<UnicodeObject*>(dsc.value));

    char* env_item = reinterpret_cast<char*>(malloc(key_bytes->count + value_bytes->count + 2));
    memcpy(env_item, key_bytes->data, key_bytes->count);
    env_item[key_bytes->count] = '=';
    memcpy(&env_item[key_bytes->count + 1], value_bytes->data, value_bytes->count);
    env_item[key_bytes->count + value_bytes->count + 1] = 0;
    envs_pointers.emplace_back(env_item);

    delete_reference(key_bytes);
    delete_reference(value_bytes);
  }
  envs_pointers.emplace_back(nullptr);

  int ret = execve(path_bytes->data, args_pointers.data(), envs_pointers.data());

  delete_reference(path_bytes);
  for (auto& o : args_objects) {
    delete_reference(o);
  }
  envs_pointers.pop_back(); // the last one is NULL
  for (auto& ptr : envs_pointers) {
    free(ptr);
  }

  return ret;
}

static UnicodeObject* posix_strerror(int64_t code) {
  char buf[128];
  strerror_r(code, buf, sizeof(buf));
  return decode_ascii(buf);
}



static map<string, Variable> globals({
  {"__doc__",                Variable(ValueType::Unicode, __doc__)},
  {"__package__",            Variable(ValueType::Unicode, L"")},

  {"CLD_CONTINUED",          Variable(ValueType::Int, static_cast<int64_t>(CLD_CONTINUED))},
  {"CLD_DUMPED",             Variable(ValueType::Int, static_cast<int64_t>(CLD_DUMPED))},
  {"CLD_EXITED",             Variable(ValueType::Int, static_cast<int64_t>(CLD_EXITED))},
  {"CLD_TRAPPED",            Variable(ValueType::Int, static_cast<int64_t>(CLD_TRAPPED))},

  {"EX_CANTCREAT",           Variable(ValueType::Int, static_cast<int64_t>(EX_CANTCREAT))},
  {"EX_CONFIG",              Variable(ValueType::Int, static_cast<int64_t>(EX_CONFIG))},
  {"EX_DATAERR",             Variable(ValueType::Int, static_cast<int64_t>(EX_DATAERR))},
  {"EX_IOERR",               Variable(ValueType::Int, static_cast<int64_t>(EX_IOERR))},
  {"EX_NOHOST",              Variable(ValueType::Int, static_cast<int64_t>(EX_NOHOST))},
  {"EX_NOINPUT",             Variable(ValueType::Int, static_cast<int64_t>(EX_NOINPUT))},
  {"EX_NOPERM",              Variable(ValueType::Int, static_cast<int64_t>(EX_NOPERM))},
  {"EX_NOUSER",              Variable(ValueType::Int, static_cast<int64_t>(EX_NOUSER))},
  {"EX_OK",                  Variable(ValueType::Int, static_cast<int64_t>(EX_OK))},
  {"EX_OSERR",               Variable(ValueType::Int, static_cast<int64_t>(EX_OSERR))},
  {"EX_OSFILE",              Variable(ValueType::Int, static_cast<int64_t>(EX_OSFILE))},
  {"EX_PROTOCOL",            Variable(ValueType::Int, static_cast<int64_t>(EX_PROTOCOL))},
  {"EX_SOFTWARE",            Variable(ValueType::Int, static_cast<int64_t>(EX_SOFTWARE))},
  {"EX_TEMPFAIL",            Variable(ValueType::Int, static_cast<int64_t>(EX_TEMPFAIL))},
  {"EX_UNAVAILABLE",         Variable(ValueType::Int, static_cast<int64_t>(EX_UNAVAILABLE))},
  {"EX_USAGE",               Variable(ValueType::Int, static_cast<int64_t>(EX_USAGE))},

  {"F_LOCK",                 Variable(ValueType::Int, static_cast<int64_t>(F_LOCK))},
  {"F_OK",                   Variable(ValueType::Int, static_cast<int64_t>(F_OK))},
  {"F_TEST",                 Variable(ValueType::Int, static_cast<int64_t>(F_TEST))},
  {"F_TLOCK",                Variable(ValueType::Int, static_cast<int64_t>(F_TLOCK))},
  {"F_ULOCK",                Variable(ValueType::Int, static_cast<int64_t>(F_ULOCK))},

  {"O_ACCMODE",              Variable(ValueType::Int, static_cast<int64_t>(O_ACCMODE))},
  {"O_APPEND",               Variable(ValueType::Int, static_cast<int64_t>(O_APPEND))},
  {"O_ASYNC",                Variable(ValueType::Int, static_cast<int64_t>(O_ASYNC))},
  {"O_CLOEXEC",              Variable(ValueType::Int, static_cast<int64_t>(O_CLOEXEC))},
  {"O_CREAT",                Variable(ValueType::Int, static_cast<int64_t>(O_CREAT))},
  {"O_DIRECTORY",            Variable(ValueType::Int, static_cast<int64_t>(O_DIRECTORY))},
  {"O_DSYNC",                Variable(ValueType::Int, static_cast<int64_t>(O_DSYNC))},
  {"O_EXCL",                 Variable(ValueType::Int, static_cast<int64_t>(O_EXCL))},
  {"O_NDELAY",               Variable(ValueType::Int, static_cast<int64_t>(O_NDELAY))},
  {"O_NOCTTY",               Variable(ValueType::Int, static_cast<int64_t>(O_NOCTTY))},
  {"O_NOFOLLOW",             Variable(ValueType::Int, static_cast<int64_t>(O_NOFOLLOW))},
  {"O_NONBLOCK",             Variable(ValueType::Int, static_cast<int64_t>(O_NONBLOCK))},
  {"O_RDONLY",               Variable(ValueType::Int, static_cast<int64_t>(O_RDONLY))},
  {"O_RDWR",                 Variable(ValueType::Int, static_cast<int64_t>(O_RDWR))},
  {"O_SYNC",                 Variable(ValueType::Int, static_cast<int64_t>(O_SYNC))},
  {"O_TRUNC",                Variable(ValueType::Int, static_cast<int64_t>(O_TRUNC))},
  {"O_WRONLY",               Variable(ValueType::Int, static_cast<int64_t>(O_WRONLY))},
#ifdef MACOSX
  {"O_EXLOCK",               Variable(ValueType::Int, static_cast<int64_t>(O_EXLOCK))}, // osx only
  {"O_SHLOCK",               Variable(ValueType::Int, static_cast<int64_t>(O_SHLOCK))}, // osx only
#endif
#ifdef LINUX
  {"O_DIRECT",               Variable(ValueType::Int, static_cast<int64_t>(O_DIRECT))}, // linux only
  {"O_LARGEFILE",            Variable(ValueType::Int, static_cast<int64_t>(O_LARGEFILE))}, // linux only
  {"O_NOATIME",              Variable(ValueType::Int, static_cast<int64_t>(O_NOATIME))}, // linux only
  {"O_PATH",                 Variable(ValueType::Int, static_cast<int64_t>(O_PATH))}, // linux only
  {"O_RSYNC",                Variable(ValueType::Int, static_cast<int64_t>(O_RSYNC))}, // linux only
  {"O_TMPFILE",              Variable(ValueType::Int, static_cast<int64_t>(O_TMPFILE))}, // linux only
#endif

  // unimplemented stuff:

  // {"DirEntry", Variable()},
  // {"NGROUPS_MAX", Variable()},
  // {"POSIX_FADV_DONTNEED", Variable()}, // linux only
  // {"POSIX_FADV_NOREUSE", Variable()}, // linux only
  // {"POSIX_FADV_NORMAL", Variable()}, // linux only
  // {"POSIX_FADV_RANDOM", Variable()}, // linux only
  // {"POSIX_FADV_SEQUENTIAL", Variable()}, // linux only
  // {"POSIX_FADV_WILLNEED", Variable()}, // linux only
  // {"PRIO_PGRP", Variable()},
  // {"PRIO_PROCESS", Variable()},
  // {"PRIO_USER", Variable()},
  // {"P_ALL", Variable()},
  // {"P_PGID", Variable()},
  // {"P_PID", Variable()},
  // {"RTLD_DEEPBIND", Variable()}, // linux only
  // {"RTLD_GLOBAL", Variable()},
  // {"RTLD_LAZY", Variable()},
  // {"RTLD_LOCAL", Variable()},
  // {"RTLD_NODELETE", Variable()},
  // {"RTLD_NOLOAD", Variable()},
  // {"RTLD_NOW", Variable()},
  // {"R_OK", Variable()},
  // {"SCHED_BATCH", Variable()}, // linux only
  // {"SCHED_FIFO", Variable()},
  // {"SCHED_IDLE", Variable()}, // linux only
  // {"SCHED_OTHER", Variable()},
  // {"SCHED_RESET_ON_FORK", Variable()}, // linux only
  // {"SCHED_RR", Variable()},
  // {"SEEK_DATA", Variable()}, // linux only
  // {"SEEK_HOLE", Variable()}, // linux only
  // {"ST_APPEND", Variable()}, // linux only
  // {"ST_MANDLOCK", Variable()}, // linux only
  // {"ST_NOATIME", Variable()}, // linux only
  // {"ST_NODEV", Variable()}, // linux only
  // {"ST_NODIRATIME", Variable()}, // linux only
  // {"ST_NOEXEC", Variable()}, // linux only
  // {"ST_NOSUID", Variable()},
  // {"ST_RDONLY", Variable()},
  // {"ST_RELATIME", Variable()}, // linux only
  // {"ST_SYNCHRONOUS", Variable()}, // linux only
  // {"ST_WRITE", Variable()}, // linux only
  // {"TMP_MAX", Variable()},
  // {"WCONTINUED", Variable()},
  // {"WCOREDUMP", Variable()},
  // {"WEXITED", Variable()},
  // {"WEXITSTATUS", Variable()},
  // {"WIFCONTINUED", Variable()},
  // {"WIFEXITED", Variable()},
  // {"WIFSIGNALED", Variable()},
  // {"WIFSTOPPED", Variable()},
  // {"WNOHANG", Variable()},
  // {"WNOWAIT", Variable()},
  // {"WSTOPPED", Variable()},
  // {"WSTOPSIG", Variable()},
  // {"WTERMSIG", Variable()},
  // {"WUNTRACED", Variable()},
  // {"W_OK", Variable()},
  // {"XATTR_CREATE", Variable()}, // linux only
  // {"XATTR_REPLACE", Variable()}, // linux only
  // {"XATTR_SIZE_MAX", Variable()}, // linux only
  // {"X_OK", Variable()},

  // {"_have_functions", Variable()},
  // {"access", Variable()},
  // {"chdir", Variable()},
  // {"chflags", Variable()}, // osx only
  // {"chmod", Variable()},
  // {"chown", Variable()},
  // {"chroot", Variable()},
  // {"confstr", Variable()},
  // {"confstr_names", Variable()},
  // {"cpu_count", Variable()},
  // {"ctermid", Variable()},
  // {"device_encoding", Variable()},
  // {"environ", Variable()},
  // {"error", Variable()},
  // {"fchdir", Variable()},
  // {"fchmod", Variable()},
  // {"fchown", Variable()},
  // {"fdatasync", Variable()}, // linux only
  // {"forkpty", Variable()},
  // {"fpathconf", Variable()},
  // {"fspath", Variable()},
  // {"fstat", Variable()},
  // {"fstatvfs", Variable()},
  // {"fsync", Variable()},
  // {"ftruncate", Variable()},
  // {"get_blocking", Variable()},
  // {"get_inheritable", Variable()},
  // {"get_terminal_size", Variable()},
  // {"getcwd", Variable()},
  // {"getcwdb", Variable()},
  // {"getgrouplist", Variable()},
  // {"getgroups", Variable()},
  // {"getloadavg", Variable()},
  // {"getlogin", Variable()},
  // {"getpriority", Variable()},
  // {"getresgid", Variable()}, // linux only
  // {"getresuid", Variable()}, // linux only
  // {"getxattr", Variable()}, // linux only
  // {"initgroups", Variable()},
  // {"isatty", Variable()},
  // {"lchflags", Variable()}, // osx only
  // {"lchmod", Variable()}, // osx only
  // {"lchown", Variable()},
  // {"link", Variable()},
  // {"listdir", Variable()},
  // {"listxattr", Variable()}, // linux only
  // {"lockf", Variable()},
  // {"lseek", Variable()},
  // {"lstat", Variable()},
  // {"major", Variable()},
  // {"makedev", Variable()},
  // {"minor", Variable()},
  // {"mkdir", Variable()},
  // {"mkfifo", Variable()},
  // {"mknod", Variable()},
  // {"nice", Variable()},
  // {"openpty", Variable()},
  // {"pathconf", Variable()},
  // {"pathconf_names", Variable()},
  // {"pipe", Variable()},
  // {"pipe2", Variable()}, // linux only
  // {"posix_fadvise", Variable()}, // linux only
  // {"posix_fallocate", Variable()}, // linux only
  // {"pread", Variable()},
  // {"putenv", Variable()},
  // {"pwrite", Variable()},
  // {"readlink", Variable()},
  // {"readv", Variable()},
  // {"remove", Variable()},
  // {"removexattr", Variable()}, // linux only
  // {"rename", Variable()},
  // {"replace", Variable()},
  // {"rmdir", Variable()},
  // {"scandir", Variable()},
  // {"sched_get_priority_max", Variable()},
  // {"sched_get_priority_min", Variable()},
  // {"sched_getaffinity", Variable()}, // linux only
  // {"sched_getparam", Variable()}, // linux only
  // {"sched_getscheduler", Variable()}, // linux only
  // {"sched_param", Variable()}, // linux only
  // {"sched_rr_get_interval", Variable()}, // linux only
  // {"sched_setaffinity", Variable()}, // linux only
  // {"sched_setparam", Variable()}, // linux only
  // {"sched_setscheduler", Variable()}, // linux only
  // {"sched_yield", Variable()},
  // {"sendfile", Variable()},
  // {"set_blocking", Variable()},
  // {"set_inheritable", Variable()},
  // {"setegid", Variable()},
  // {"seteuid", Variable()},
  // {"setgid", Variable()},
  // {"setgroups", Variable()},
  // {"setpgid", Variable()},
  // {"setpgrp", Variable()},
  // {"setpriority", Variable()},
  // {"setregid", Variable()},
  // {"setresgid", Variable()}, // linux only
  // {"setresuid", Variable()}, // linux only
  // {"setreuid", Variable()},
  // {"setsid", Variable()},
  // {"setuid", Variable()},
  // {"setxattr", Variable()}, // linux only
  // {"stat", Variable()},
  // {"stat_float_times", Variable()},
  // {"stat_result", Variable()},
  // {"statvfs", Variable()},
  // {"statvfs_result", Variable()},
  // {"symlink", Variable()},
  // {"sync", Variable()},
  // {"sysconf", Variable()},
  // {"sysconf_names", Variable()},
  // {"system", Variable()},
  // {"tcgetpgrp", Variable()},
  // {"tcsetpgrp", Variable()},
  // {"terminal_size", Variable()},
  // {"times", Variable()},
  // {"times_result", Variable()},
  // {"truncate", Variable()},
  // {"ttyname", Variable()},
  // {"umask", Variable()},
  // {"uname", Variable()},
  // {"uname_result", Variable()},
  // {"unlink", Variable()},
  // {"unsetenv", Variable()},
  // {"urandom", Variable()},
  // {"utime", Variable()},
  // {"wait", Variable()},
  // {"wait3", Variable()},
  // {"wait4", Variable()},
  // {"waitid", Variable()}, // linux only
  // {"waitid_result", Variable()}, // linux only
  // {"waitpid", Variable()},
  // {"writev", Variable()},
});

std::shared_ptr<ModuleAnalysis> posix_module(new ModuleAnalysis("posix", globals));

void posix_initialize() {
  Variable Int(ValueType::Int);
  Variable Bytes(ValueType::Bytes);
  Variable Unicode(ValueType::Unicode);
  Variable List_Unicode(ValueType::List, vector<Variable>({Unicode}));
  Variable Dict_Unicode_Unicode(ValueType::Dict, vector<Variable>({Unicode, Unicode}));
  Variable None(ValueType::None);

  posix_module->create_builtin_function("getpid", {}, Int,
      reinterpret_cast<const void*>(&getpid));
  posix_module->create_builtin_function("getppid", {}, Int,
      reinterpret_cast<const void*>(&getppid));
  posix_module->create_builtin_function("getpgid", {Int}, Int,
      reinterpret_cast<const void*>(&getpgid));
  posix_module->create_builtin_function("getpgrp", {}, Int,
      reinterpret_cast<const void*>(&getpgrp));
  posix_module->create_builtin_function("getsid", {Int}, Int,
      reinterpret_cast<const void*>(&getsid));

  posix_module->create_builtin_function("getuid", {}, Int,
      reinterpret_cast<const void*>(&getuid));
  posix_module->create_builtin_function("getgid", {}, Int,
      reinterpret_cast<const void*>(&getgid));
  posix_module->create_builtin_function("geteuid", {}, Int,
      reinterpret_cast<const void*>(&geteuid));
  posix_module->create_builtin_function("getegid", {}, Int,
      reinterpret_cast<const void*>(&getegid));

  // these functions never return, so return type is technically unused
  posix_module->create_builtin_function("_exit", {Int}, Int,
      reinterpret_cast<const void*>(&_exit));
  posix_module->create_builtin_function("abort", {}, Int,
      reinterpret_cast<const void*>(&abort));

  // TODO: close should raise OSError on failure when exceptions are implemented
  posix_module->create_builtin_function("close", {Int}, None,
      reinterpret_cast<const void*>(&close));
  posix_module->create_builtin_function("closerange", {Int, Int}, None,
      reinterpret_cast<const void*>(&posix_closerange));

  posix_module->create_builtin_function("dup", {Int}, Int,
      reinterpret_cast<const void*>(&dup));
  posix_module->create_builtin_function("dup2", {Int, Int}, Int,
      reinterpret_cast<const void*>(&dup2));

  posix_module->create_builtin_function("fork", {}, Int,
      reinterpret_cast<const void*>(&fork));

  posix_module->create_builtin_function("kill", {Int, Int}, Int,
      reinterpret_cast<const void*>(&kill));
  posix_module->create_builtin_function("killpg", {Int, Int}, Int,
      reinterpret_cast<const void*>(&killpg));

  posix_module->create_builtin_function("open",
      {Unicode, Int, Variable(ValueType::Int, 0777LL)}, Int,
      reinterpret_cast<const void*>(&posix_open));
  posix_module->create_builtin_function("read", {Int, Int}, Bytes,
      reinterpret_cast<const void*>(&posix_read));
  posix_module->create_builtin_function("write", {Int, Bytes}, Int,
      reinterpret_cast<const void*>(&posix_write));

  posix_module->create_builtin_function("execv",
      {Unicode, List_Unicode}, Int,
      reinterpret_cast<const void*>(&posix_execv));
  posix_module->create_builtin_function("execve",
      {Unicode, List_Unicode, Dict_Unicode_Unicode}, Int,
      reinterpret_cast<const void*>(&posix_execve));

  posix_module->create_builtin_function("strerror", {Int}, Unicode,
      reinterpret_cast<const void*>(&posix_strerror));
}
