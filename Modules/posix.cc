#include "posix.hh"

#include <inttypes.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sysexits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <phosg/Filesystem.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Analysis.hh"
#include "../BuiltinFunctions.hh"
#include "../Types/Strings.hh"
#include "../Types/List.hh"
#include "../Types/Dictionary.hh"

using namespace std;



extern shared_ptr<GlobalAnalysis> global;

extern char** environ;

static wstring __doc__ = L"\
This module provides access to operating system functionality that is\n\
standardized by the C Standard and the POSIX standard (a thinly\n\
disguised Unix interface). Refer to the library manual and\n\
corresponding Unix manual entries for more information on calls.";

unordered_map<Variable, shared_ptr<Variable>> get_environ() {
  unordered_map<Variable, shared_ptr<Variable>> ret;
  for (char** env = environ; *env; env++) {
    size_t equals_loc;
    for (equals_loc = 0; (*env)[equals_loc] && ((*env)[equals_loc] != '='); equals_loc++);
    if (!(*env)[equals_loc]) {
      continue;
    }

    ret.emplace(piecewise_construct,
        forward_as_tuple(ValueType::Bytes, string(*env, equals_loc)),
        forward_as_tuple(new Variable(ValueType::Bytes, string(&(*env)[equals_loc + 1]))));
  }
  return ret;
}

unordered_map<Variable, shared_ptr<Variable>> sysconf_names({
  {Variable(ValueType::Unicode, L"SC_ARG_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_ARG_MAX)))},
  {Variable(ValueType::Unicode, L"SC_CHILD_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_CHILD_MAX)))},
  {Variable(ValueType::Unicode, L"SC_CLK_TCK"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_CLK_TCK)))},
  {Variable(ValueType::Unicode, L"SC_IOV_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_IOV_MAX)))},
  {Variable(ValueType::Unicode, L"SC_NGROUPS_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_NGROUPS_MAX)))},
  {Variable(ValueType::Unicode, L"SC_NPROCESSORS_CONF"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_NPROCESSORS_CONF)))},
  {Variable(ValueType::Unicode, L"SC_NPROCESSORS_ONLN"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_NPROCESSORS_ONLN)))},
  {Variable(ValueType::Unicode, L"SC_OPEN_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_OPEN_MAX)))},
  {Variable(ValueType::Unicode, L"SC_PAGESIZE"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_PAGESIZE)))},
  {Variable(ValueType::Unicode, L"SC_STREAM_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_STREAM_MAX)))},
  {Variable(ValueType::Unicode, L"SC_TZNAME_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_TZNAME_MAX)))},
  {Variable(ValueType::Unicode, L"SC_JOB_CONTROL"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_JOB_CONTROL)))},
  {Variable(ValueType::Unicode, L"SC_SAVED_IDS"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_SAVED_IDS)))},
  {Variable(ValueType::Unicode, L"SC_VERSION"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_VERSION)))},
  {Variable(ValueType::Unicode, L"SC_BC_BASE_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_BC_BASE_MAX)))},
  {Variable(ValueType::Unicode, L"SC_BC_DIM_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_BC_DIM_MAX)))},
  {Variable(ValueType::Unicode, L"SC_BC_SCALE_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_BC_SCALE_MAX)))},
  {Variable(ValueType::Unicode, L"SC_BC_STRING_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_BC_STRING_MAX)))},
  {Variable(ValueType::Unicode, L"SC_COLL_WEIGHTS_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_COLL_WEIGHTS_MAX)))},
  {Variable(ValueType::Unicode, L"SC_EXPR_NEST_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_EXPR_NEST_MAX)))},
  {Variable(ValueType::Unicode, L"SC_LINE_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_LINE_MAX)))},
  {Variable(ValueType::Unicode, L"SC_RE_DUP_MAX"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_RE_DUP_MAX)))},
  {Variable(ValueType::Unicode, L"SC_2_VERSION"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_2_VERSION)))},
  {Variable(ValueType::Unicode, L"SC_2_C_BIND"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_2_C_BIND)))},
  {Variable(ValueType::Unicode, L"SC_2_C_DEV"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_2_C_DEV)))},
  {Variable(ValueType::Unicode, L"SC_2_CHAR_TERM"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_2_CHAR_TERM)))},
  {Variable(ValueType::Unicode, L"SC_2_FORT_DEV"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_2_FORT_DEV)))},
  {Variable(ValueType::Unicode, L"SC_2_FORT_RUN"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_2_FORT_RUN)))},
  {Variable(ValueType::Unicode, L"SC_2_LOCALEDEF"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_2_LOCALEDEF)))},
  {Variable(ValueType::Unicode, L"SC_2_SW_DEV"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_2_SW_DEV)))},
  {Variable(ValueType::Unicode, L"SC_2_UPE"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_2_UPE)))},
  {Variable(ValueType::Unicode, L"SC_PHYS_PAGES"), shared_ptr<Variable>(
      new Variable(ValueType::Int, static_cast<int64_t>(_SC_PHYS_PAGES)))},
});

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

  {"environ",                Variable(ValueType::Dict, get_environ())},
  {"sysconf_names",          Variable(ValueType::Dict, sysconf_names)},

  // I'm lazy and don't want to write __init__ for this, so it's unavailable to
  // python code
  // {"stat_result", Variable()},

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

  // {"_have_functions", Variable(ValueType::List, ['HAVE_FACCESSAT', 'HAVE_FCHDIR', 'HAVE_FCHMOD', 'HAVE_FCHMODAT', 'HAVE_FCHOWN', 'HAVE_FCHOWNAT', 'HAVE_FDOPENDIR', 'HAVE_FPATHCONF', 'HAVE_FSTATAT', 'HAVE_FSTATVFS', 'HAVE_FTRUNCATE', 'HAVE_FUTIMES', 'HAVE_LINKAT', 'HAVE_LCHFLAGS', 'HAVE_LCHMOD', 'HAVE_LCHOWN', 'HAVE_LSTAT', 'HAVE_LUTIMES', 'HAVE_MKDIRAT', 'HAVE_OPENAT', 'HAVE_READLINKAT', 'HAVE_RENAMEAT', 'HAVE_SYMLINKAT', 'HAVE_UNLINKAT'])},
});

std::shared_ptr<ModuleAnalysis> posix_module(new ModuleAnalysis("posix", globals));

void posix_initialize() {
  Variable Bool(ValueType::Bool);
  Variable Bool_True(ValueType::Bool, true);
  Variable Bool_False(ValueType::Bool, false);
  Variable Int(ValueType::Int);
  Variable Float(ValueType::Float);
  Variable Bytes(ValueType::Bytes);
  Variable Unicode(ValueType::Unicode);
  Variable List_Unicode(ValueType::List, vector<Variable>({Unicode}));
  Variable Dict_Unicode_Unicode(ValueType::Dict, vector<Variable>({Unicode, Unicode}));
  Variable None(ValueType::None);

  posix_module->create_builtin_function("getpid", {}, Int,
      reinterpret_cast<const void*>(&getpid), false);
  posix_module->create_builtin_function("getppid", {}, Int,
      reinterpret_cast<const void*>(&getppid), false);
  posix_module->create_builtin_function("getpgid", {Int}, Int,
      reinterpret_cast<const void*>(&getpgid), false);
  posix_module->create_builtin_function("getpgrp", {}, Int,
      reinterpret_cast<const void*>(&getpgrp), false);
  posix_module->create_builtin_function("getsid", {Int}, Int,
      reinterpret_cast<const void*>(&getsid), false);

  posix_module->create_builtin_function("getuid", {}, Int,
      reinterpret_cast<const void*>(&getuid), false);
  posix_module->create_builtin_function("getgid", {}, Int,
      reinterpret_cast<const void*>(&getgid), false);
  posix_module->create_builtin_function("geteuid", {}, Int,
      reinterpret_cast<const void*>(&geteuid), false);
  posix_module->create_builtin_function("getegid", {}, Int,
      reinterpret_cast<const void*>(&getegid), false);

  // these functions never return, so return type is technically unused
  posix_module->create_builtin_function("_exit", {Int}, Int,
      reinterpret_cast<const void*>(&_exit), false);
  posix_module->create_builtin_function("abort", {}, Int,
      reinterpret_cast<const void*>(&abort), false);

  posix_module->create_builtin_function("close", {Int}, None,
      reinterpret_cast<const void*>(+[](int64_t fd, ExceptionBlock* exc_block) {
    if (close(fd)) {
      raise_python_exception(exc_block, create_single_attr_instance(
          OSError_class_id, static_cast<int64_t>(errno)));
    }
  }), true);
  posix_module->create_builtin_function("closerange", {Int, Int}, None,
      reinterpret_cast<const void*>(+[](int64_t fd, int64_t end_fd) {
    for (; fd < end_fd; fd++) {
      close(fd);
    }
  }), false);

  // TODO: most functions below here should raise OSError on failure
  posix_module->create_builtin_function("dup", {Int}, Int,
      reinterpret_cast<const void*>(&dup), false);
  posix_module->create_builtin_function("dup2", {Int, Int}, Int,
      reinterpret_cast<const void*>(&dup2), false);

  posix_module->create_builtin_function("fork", {}, Int,
      reinterpret_cast<const void*>(&fork), false);

  posix_module->create_builtin_function("kill", {Int, Int}, Int,
      reinterpret_cast<const void*>(&kill), false);
  posix_module->create_builtin_function("killpg", {Int, Int}, Int,
      reinterpret_cast<const void*>(&killpg), false);

  posix_module->create_builtin_function("open",
      {Unicode, Int, Variable(ValueType::Int, 0777LL)}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path, int64_t flags, int64_t mode) -> int64_t {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    int64_t ret = open(path_bytes->data, flags, mode);
    delete_reference(path_bytes);
    return ret;
  }), false);

  posix_module->create_builtin_function("read", {Int, Int}, Bytes,
      reinterpret_cast<const void*>(+[](int64_t fd, int64_t buffer_size) -> BytesObject* {
    BytesObject* ret = bytes_new(NULL, NULL, buffer_size);
    ssize_t bytes_read = read(fd, ret->data, buffer_size);
    if (bytes_read >= 0) {
      ret->count = bytes_read;
    } else {
      ret->count = 0;
    }
    return ret;
  }), false);

  posix_module->create_builtin_function("write", {Int, Bytes}, Int,
      reinterpret_cast<const void*>(+[](int64_t fd, BytesObject* data) -> int64_t {
    return write(fd, data->data, data->count);
  }), false);

  posix_module->create_builtin_function("execv",
      {Unicode, List_Unicode}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path, ListObject* args) -> int64_t {

    BytesObject* path_bytes = unicode_encode_ascii(path);

    vector<BytesObject*> args_objects;
    vector<char*> args_pointers;
    for (size_t x = 0; x < args->count; x++) {
      args_objects.emplace_back(unicode_encode_ascii(reinterpret_cast<UnicodeObject*>(args->items[x])));
      args_pointers.emplace_back(args_objects.back()->data);
    }
    args_pointers.emplace_back(nullptr);

    int64_t ret = execv(path_bytes->data, args_pointers.data());
    // little optimization: we expect execv to succeed most of the time, so we
    // don't bother deleting path until after it returns (and has failed)
    delete_reference(path);
    delete_reference(path_bytes);
    for (auto& o : args_objects) {
      delete_reference(o);
    }

    return ret;
  }), false);

  posix_module->create_builtin_function("execve",
      {Unicode, List_Unicode, Dict_Unicode_Unicode}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path, ListObject* args,
        DictionaryObject* env) -> int64_t {

    BytesObject* path_bytes = unicode_encode_ascii(path);

    vector<BytesObject*> args_objects;
    vector<char*> args_pointers;
    for (size_t x = 0; x < args->count; x++) {
      args_objects.emplace_back(unicode_encode_ascii(reinterpret_cast<UnicodeObject*>(args->items[x])));
      args_pointers.emplace_back(args_objects.back()->data);
    }
    args_pointers.emplace_back(nullptr);

    vector<char*> envs_pointers;
    DictionaryObject::SlotContents dsc;
    while (dictionary_next_item(env, &dsc)) {
      BytesObject* key_bytes = unicode_encode_ascii(reinterpret_cast<UnicodeObject*>(dsc.key));
      BytesObject* value_bytes = unicode_encode_ascii(reinterpret_cast<UnicodeObject*>(dsc.value));

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

    int64_t ret = execve(path_bytes->data, args_pointers.data(), envs_pointers.data());

    // little optimization: we expect execve to succeed most of the time, so we
    // don't bother deleting path until after it returns (and has failed)
    delete_reference(path);
    delete_reference(path_bytes);
    for (auto& o : args_objects) {
      delete_reference(o);
    }
    envs_pointers.pop_back(); // the last one is NULL
    for (auto& ptr : envs_pointers) {
      free(ptr);
    }

    return ret;
  }), false);

  posix_module->create_builtin_function("strerror", {Int}, Unicode,
      reinterpret_cast<const void*>(+[](int64_t code) -> UnicodeObject* {
    char buf[128];
    strerror_r(code, buf, sizeof(buf));
    return bytes_decode_ascii(buf);
  }), false);

  posix_module->create_builtin_function("access", {Unicode, Int}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path, int64_t mode) -> int64_t {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    int64_t ret = access(path_bytes->data, mode);
    delete_reference(path_bytes);
    return ret;
  }), false);

  posix_module->create_builtin_function("chdir", {Unicode}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path) -> int64_t {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    int64_t ret = chdir(path_bytes->data);
    delete_reference(path_bytes);
    return ret;
  }), false);

  posix_module->create_builtin_function("fchdir", {Int}, Int,
      reinterpret_cast<const void*>(&fchdir), false);

  posix_module->create_builtin_function("chmod", {Unicode, Int}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path, int64_t mode) -> int64_t {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    int64_t ret = chmod(path_bytes->data, mode);
    delete_reference(path_bytes);
    return ret;
  }), false);

  posix_module->create_builtin_function("fchmod", {Int, Int}, Int,
      reinterpret_cast<const void*>(&fchmod), false);

#ifdef MACOSX
  posix_module->create_builtin_function("chflags", {Unicode, Int}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path, int64_t flags) -> int64_t {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    int64_t ret = chflags(path_bytes->data, flags);
    delete_reference(path_bytes);
    return ret;
  }), false);

  posix_module->create_builtin_function("fchflags", {Int, Int}, Int,
      reinterpret_cast<const void*>(&fchflags), false);
#endif

  posix_module->create_builtin_function("chown", {Unicode, Int, Int}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path, int64_t uid, int64_t gid) -> int64_t {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    int64_t ret = chown(path_bytes->data, uid, gid);
    delete_reference(path_bytes);
    return ret;
  }), false);

  posix_module->create_builtin_function("lchown", {Unicode, Int, Int}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path, int64_t uid, int64_t gid) -> int64_t {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    int64_t ret = lchown(path_bytes->data, uid, gid);
    delete_reference(path_bytes);
    return ret;
  }), false);

  posix_module->create_builtin_function("fchown", {Int, Int, Int}, Int,
      reinterpret_cast<const void*>(&fchown), false);

  posix_module->create_builtin_function("chroot", {Unicode}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path) -> int64_t {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    int64_t ret = chroot(path_bytes->data);
    delete_reference(path_bytes);
    return ret;
  }), false);

  posix_module->create_builtin_function("ctermid", {}, Unicode,
      reinterpret_cast<const void*>(+[]() -> UnicodeObject* {
    char result[L_ctermid];
    ctermid(result);
    return bytes_decode_ascii(result);
  }), false);

  posix_module->create_builtin_function("cpu_count", {}, Int,
      reinterpret_cast<const void*>(+[]() -> int64_t {
    // TODO: should we use procfs here instead?
    return sysconf(_SC_NPROCESSORS_ONLN);
  }), false);

  static int64_t stat_result_class_id = create_builtin_class("stat_result", {
        {"st_mode", Int},
        {"st_ino", Int},
        {"st_dev", Int},
        {"st_nlink", Int},
        {"st_uid", Int},
        {"st_gid", Int},
        {"st_size", Int},
        {"st_atime", Float},
        {"st_mtime", Float},
        {"st_ctime", Float},
        {"st_atime_ns", Int},
        {"st_mtime_ns", Int},
        {"st_ctime_ns", Int},
        {"st_blocks", Int},
        {"st_blksize", Int},
        {"st_rdev", Int}},
      {}, NULL, reinterpret_cast<const void*>(&free), false);
  Variable StatResult(ValueType::Instance, stat_result_class_id, NULL);
  static ClassContext* stat_result_class = global->context_for_class(stat_result_class_id);

  static auto convert_stat_result = +[](int64_t ret, const struct stat* st) -> void* {
    // TODO: this can be optimized to avoid all the map lookups
    InstanceObject* res = create_instance(stat_result_class_id, stat_result_class->attribute_count());
    if (!ret) {
      stat_result_class->set_attribute(res, "st_mode", st->st_mode);
      stat_result_class->set_attribute(res, "st_ino", st->st_ino);
      stat_result_class->set_attribute(res, "st_dev", st->st_dev);
      stat_result_class->set_attribute(res, "st_nlink", st->st_nlink);
      stat_result_class->set_attribute(res, "st_uid", st->st_uid);
      stat_result_class->set_attribute(res, "st_gid", st->st_gid);
      stat_result_class->set_attribute(res, "st_size", st->st_size);
      stat_result_class->set_attribute(res, "st_blocks", st->st_blocks);
      stat_result_class->set_attribute(res, "st_blksize", st->st_blksize);
      stat_result_class->set_attribute(res, "st_rdev", st->st_rdev);

      stat_result_class->set_attribute(res, "st_atime_ns", st->st_atimespec.tv_sec * 1000000000 + st->st_atimespec.tv_nsec);
      stat_result_class->set_attribute(res, "st_mtime_ns", st->st_mtimespec.tv_sec * 1000000000 + st->st_mtimespec.tv_nsec);
      stat_result_class->set_attribute(res, "st_ctime_ns", st->st_ctimespec.tv_sec * 1000000000 + st->st_ctimespec.tv_nsec);

      double atime = static_cast<double>(st->st_atimespec.tv_sec * 1000000000 + st->st_atimespec.tv_nsec) / 1000000000;
      double mtime = static_cast<double>(st->st_mtimespec.tv_sec * 1000000000 + st->st_mtimespec.tv_nsec) / 1000000000;
      double ctime = static_cast<double>(st->st_ctimespec.tv_sec * 1000000000 + st->st_ctimespec.tv_nsec) / 1000000000;
      stat_result_class->set_attribute(res, "st_atime", *reinterpret_cast<int64_t*>(&atime));
      stat_result_class->set_attribute(res, "st_mtime", *reinterpret_cast<int64_t*>(&mtime));
      stat_result_class->set_attribute(res, "st_ctime", *reinterpret_cast<int64_t*>(&ctime));

    } else {
      // TODO: raise an exception on failure
      stat_result_class->set_attribute(res, "st_mode", 0);
      stat_result_class->set_attribute(res, "st_ino", 0);
      stat_result_class->set_attribute(res, "st_dev", 0);
      stat_result_class->set_attribute(res, "st_nlink", 0);
      stat_result_class->set_attribute(res, "st_uid", 0);
      stat_result_class->set_attribute(res, "st_gid", 0);
      stat_result_class->set_attribute(res, "st_size", 0);
      stat_result_class->set_attribute(res, "st_blocks", 0);
      stat_result_class->set_attribute(res, "st_blksize", 0);
      stat_result_class->set_attribute(res, "st_rdev", 0);
      stat_result_class->set_attribute(res, "st_atime_ns", 0);
      stat_result_class->set_attribute(res, "st_mtime_ns", 0);
      stat_result_class->set_attribute(res, "st_ctime_ns", 0);
      stat_result_class->set_attribute(res, "st_atime", 0);
      stat_result_class->set_attribute(res, "st_mtime", 0);
      stat_result_class->set_attribute(res, "st_ctime", 0);
    }

    return res;
  };

  // TODO: support dir_fd
  posix_module->create_builtin_function("stat", {Unicode, Bool_True}, StatResult,
      reinterpret_cast<const void*>(+[](UnicodeObject* path, bool follow_symlinks) -> void* {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    struct stat st;
    int64_t ret = follow_symlinks ? stat(path_bytes->data, &st) : lstat(path_bytes->data, &st);
    delete_reference(path_bytes);
    return convert_stat_result(ret, &st);
  }), false);

  posix_module->create_builtin_function("fstat", {Int}, StatResult,
      reinterpret_cast<const void*>(+[](int64_t fd) -> void* {
    struct stat st;
    return convert_stat_result(fstat(fd, &st), &st);
  }), false);

  posix_module->create_builtin_function("truncate", {Unicode, Int}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* path, int64_t size) -> int64_t {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    int64_t ret = truncate(path_bytes->data, size);
    delete_reference(path_bytes);
    return ret;
  }), false);

  posix_module->create_builtin_function("ftruncate", {Int, Int}, Int,
      reinterpret_cast<const void*>(&ftruncate), false);

  posix_module->create_builtin_function("getcwd", {}, Unicode,
      reinterpret_cast<const void*>(+[]() -> UnicodeObject* {
    char path[MAXPATHLEN];
    if (!getcwd(path, MAXPATHLEN)) {
      return unicode_new(NULL, NULL, 0);
    }
    return bytes_decode_ascii(path);
  }), false);

  posix_module->create_builtin_function("getcwdb", {}, Bytes,
      reinterpret_cast<const void*>(+[]() -> BytesObject* {
    BytesObject* ret = bytes_new(NULL, NULL, MAXPATHLEN);
    if (!getcwd(ret->data, MAXPATHLEN)) {
      ret->count = 0;
      ret->data[0] = 0;
    } else {
      ret->count = strlen(ret->data);
    }
    return ret;
  }), false);

  posix_module->create_builtin_function("lseek", {Int, Int, Int}, Int,
      reinterpret_cast<const void*>(&lseek), false);

  posix_module->create_builtin_function("fsync", {Int}, Int,
      reinterpret_cast<const void*>(&fsync), false);

  posix_module->create_builtin_function("isatty", {Int}, Bool,
      reinterpret_cast<const void*>(&isatty), false);

  posix_module->create_builtin_function("listdir", {Variable(ValueType::Unicode, L".")}, List_Unicode,
      reinterpret_cast<const void*>(+[](UnicodeObject* path) -> void* {
    BytesObject* path_bytes = unicode_encode_ascii(path);
    delete_reference(path);

    auto items = list_directory(path_bytes->data);

    ListObject* l = list_new(NULL, items.size(), true);
    size_t x = 0;
    for (const auto& item : items) {
      l->items[x++] = bytes_decode_ascii(item.c_str());
    }

    delete_reference(path_bytes);
    return l;
  }), false);

  // TODO: support passing names as strings
  posix_module->create_builtin_function("sysconf", {Int}, Int,
      reinterpret_cast<const void*>(&sysconf), false);

  // {"confstr", Variable()},
  // {"confstr_names", Variable()},
  // {"device_encoding", Variable()},
  // {"error", Variable()},
  // {"fdatasync", Variable()}, // linux only
  // {"forkpty", Variable()},
  // {"fpathconf", Variable()},
  // {"fspath", Variable()},
  // {"fstatvfs", Variable()},
  // {"get_blocking", Variable()},
  // {"get_inheritable", Variable()},
  // {"get_terminal_size", Variable()},
  // {"getgrouplist", Variable()},
  // {"getgroups", Variable()},
  // {"getloadavg", Variable()},
  // {"getlogin", Variable()},
  // {"getpriority", Variable()},
  // {"getresgid", Variable()}, // linux only
  // {"getresuid", Variable()}, // linux only
  // {"getxattr", Variable()}, // linux only
  // {"initgroups", Variable()},
  // {"lchflags", Variable()}, // osx only
  // {"lchmod", Variable()}, // osx only
  // {"link", Variable()},
  // {"listxattr", Variable()}, // linux only
  // {"lockf", Variable()},
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
  // {"stat_float_times", Variable()},
  // {"statvfs", Variable()},
  // {"statvfs_result", Variable()},
  // {"symlink", Variable()},
  // {"sync", Variable()},
  // {"system", Variable()},
  // {"tcgetpgrp", Variable()},
  // {"tcsetpgrp", Variable()},
  // {"terminal_size", Variable()},
  // {"times", Variable()},
  // {"times_result", Variable()},
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
}
