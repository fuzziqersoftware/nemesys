#include "posix.hh"

#include <inttypes.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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

#include "../Compiler/Contexts.hh"
#include "../Compiler/BuiltinFunctions.hh"
#include "../Types/Strings.hh"
#include "../Types/List.hh"
#include "../Types/Dictionary.hh"

using namespace std;



extern shared_ptr<GlobalContext> global;

extern char** environ;

static wstring __doc__ = L"\
This module provides access to operating system functionality that is\n\
standardized by the C Standard and the POSIX standard (a thinly\n\
disguised Unix interface). Refer to the library manual and\n\
corresponding Unix manual entries for more information on calls.";

unordered_map<Value, shared_ptr<Value>> get_environ() {
  unordered_map<Value, shared_ptr<Value>> ret;
  for (char** env = environ; *env; env++) {
    size_t equals_loc;
    for (equals_loc = 0; (*env)[equals_loc] && ((*env)[equals_loc] != '='); equals_loc++);
    if (!(*env)[equals_loc]) {
      continue;
    }

    ret.emplace(piecewise_construct,
        forward_as_tuple(ValueType::Bytes, string(*env, equals_loc)),
        forward_as_tuple(new Value(ValueType::Bytes, string(&(*env)[equals_loc + 1]))));
  }
  return ret;
}

unordered_map<Value, shared_ptr<Value>> sysconf_names({
  {Value(ValueType::Unicode, L"SC_ARG_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_ARG_MAX)))},
  {Value(ValueType::Unicode, L"SC_CHILD_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_CHILD_MAX)))},
  {Value(ValueType::Unicode, L"SC_CLK_TCK"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_CLK_TCK)))},
  {Value(ValueType::Unicode, L"SC_IOV_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_IOV_MAX)))},
  {Value(ValueType::Unicode, L"SC_NGROUPS_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_NGROUPS_MAX)))},
  {Value(ValueType::Unicode, L"SC_NPROCESSORS_CONF"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_NPROCESSORS_CONF)))},
  {Value(ValueType::Unicode, L"SC_NPROCESSORS_ONLN"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_NPROCESSORS_ONLN)))},
  {Value(ValueType::Unicode, L"SC_OPEN_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_OPEN_MAX)))},
  {Value(ValueType::Unicode, L"SC_PAGESIZE"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_PAGESIZE)))},
  {Value(ValueType::Unicode, L"SC_STREAM_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_STREAM_MAX)))},
  {Value(ValueType::Unicode, L"SC_TZNAME_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_TZNAME_MAX)))},
  {Value(ValueType::Unicode, L"SC_JOB_CONTROL"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_JOB_CONTROL)))},
  {Value(ValueType::Unicode, L"SC_SAVED_IDS"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_SAVED_IDS)))},
  {Value(ValueType::Unicode, L"SC_VERSION"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_VERSION)))},
  {Value(ValueType::Unicode, L"SC_BC_BASE_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_BC_BASE_MAX)))},
  {Value(ValueType::Unicode, L"SC_BC_DIM_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_BC_DIM_MAX)))},
  {Value(ValueType::Unicode, L"SC_BC_SCALE_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_BC_SCALE_MAX)))},
  {Value(ValueType::Unicode, L"SC_BC_STRING_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_BC_STRING_MAX)))},
  {Value(ValueType::Unicode, L"SC_COLL_WEIGHTS_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_COLL_WEIGHTS_MAX)))},
  {Value(ValueType::Unicode, L"SC_EXPR_NEST_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_EXPR_NEST_MAX)))},
  {Value(ValueType::Unicode, L"SC_LINE_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_LINE_MAX)))},
  {Value(ValueType::Unicode, L"SC_RE_DUP_MAX"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_RE_DUP_MAX)))},
  {Value(ValueType::Unicode, L"SC_2_VERSION"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_2_VERSION)))},
  {Value(ValueType::Unicode, L"SC_2_C_BIND"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_2_C_BIND)))},
  {Value(ValueType::Unicode, L"SC_2_C_DEV"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_2_C_DEV)))},
  {Value(ValueType::Unicode, L"SC_2_CHAR_TERM"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_2_CHAR_TERM)))},
  {Value(ValueType::Unicode, L"SC_2_FORT_DEV"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_2_FORT_DEV)))},
  {Value(ValueType::Unicode, L"SC_2_FORT_RUN"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_2_FORT_RUN)))},
  {Value(ValueType::Unicode, L"SC_2_LOCALEDEF"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_2_LOCALEDEF)))},
  {Value(ValueType::Unicode, L"SC_2_SW_DEV"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_2_SW_DEV)))},
  {Value(ValueType::Unicode, L"SC_2_UPE"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_2_UPE)))},
  {Value(ValueType::Unicode, L"SC_PHYS_PAGES"), shared_ptr<Value>(
      new Value(ValueType::Int, static_cast<int64_t>(_SC_PHYS_PAGES)))},
});

static map<string, Value> globals({
  {"__doc__",                Value(ValueType::Unicode, __doc__)},
  {"__package__",            Value(ValueType::Unicode, L"")},

  {"CLD_CONTINUED",          Value(ValueType::Int, static_cast<int64_t>(CLD_CONTINUED))},
  {"CLD_DUMPED",             Value(ValueType::Int, static_cast<int64_t>(CLD_DUMPED))},
  {"CLD_EXITED",             Value(ValueType::Int, static_cast<int64_t>(CLD_EXITED))},
  {"CLD_TRAPPED",            Value(ValueType::Int, static_cast<int64_t>(CLD_TRAPPED))},

  {"EX_CANTCREAT",           Value(ValueType::Int, static_cast<int64_t>(EX_CANTCREAT))},
  {"EX_CONFIG",              Value(ValueType::Int, static_cast<int64_t>(EX_CONFIG))},
  {"EX_DATAERR",             Value(ValueType::Int, static_cast<int64_t>(EX_DATAERR))},
  {"EX_IOERR",               Value(ValueType::Int, static_cast<int64_t>(EX_IOERR))},
  {"EX_NOHOST",              Value(ValueType::Int, static_cast<int64_t>(EX_NOHOST))},
  {"EX_NOINPUT",             Value(ValueType::Int, static_cast<int64_t>(EX_NOINPUT))},
  {"EX_NOPERM",              Value(ValueType::Int, static_cast<int64_t>(EX_NOPERM))},
  {"EX_NOUSER",              Value(ValueType::Int, static_cast<int64_t>(EX_NOUSER))},
  {"EX_OK",                  Value(ValueType::Int, static_cast<int64_t>(EX_OK))},
  {"EX_OSERR",               Value(ValueType::Int, static_cast<int64_t>(EX_OSERR))},
  {"EX_OSFILE",              Value(ValueType::Int, static_cast<int64_t>(EX_OSFILE))},
  {"EX_PROTOCOL",            Value(ValueType::Int, static_cast<int64_t>(EX_PROTOCOL))},
  {"EX_SOFTWARE",            Value(ValueType::Int, static_cast<int64_t>(EX_SOFTWARE))},
  {"EX_TEMPFAIL",            Value(ValueType::Int, static_cast<int64_t>(EX_TEMPFAIL))},
  {"EX_UNAVAILABLE",         Value(ValueType::Int, static_cast<int64_t>(EX_UNAVAILABLE))},
  {"EX_USAGE",               Value(ValueType::Int, static_cast<int64_t>(EX_USAGE))},

  {"F_LOCK",                 Value(ValueType::Int, static_cast<int64_t>(F_LOCK))},
  {"F_OK",                   Value(ValueType::Int, static_cast<int64_t>(F_OK))},
  {"F_TEST",                 Value(ValueType::Int, static_cast<int64_t>(F_TEST))},
  {"F_TLOCK",                Value(ValueType::Int, static_cast<int64_t>(F_TLOCK))},
  {"F_ULOCK",                Value(ValueType::Int, static_cast<int64_t>(F_ULOCK))},

  {"O_ACCMODE",              Value(ValueType::Int, static_cast<int64_t>(O_ACCMODE))},
  {"O_APPEND",               Value(ValueType::Int, static_cast<int64_t>(O_APPEND))},
  {"O_ASYNC",                Value(ValueType::Int, static_cast<int64_t>(O_ASYNC))},
  {"O_CLOEXEC",              Value(ValueType::Int, static_cast<int64_t>(O_CLOEXEC))},
  {"O_CREAT",                Value(ValueType::Int, static_cast<int64_t>(O_CREAT))},
  {"O_DIRECTORY",            Value(ValueType::Int, static_cast<int64_t>(O_DIRECTORY))},
  {"O_DSYNC",                Value(ValueType::Int, static_cast<int64_t>(O_DSYNC))},
  {"O_EXCL",                 Value(ValueType::Int, static_cast<int64_t>(O_EXCL))},
  {"O_NDELAY",               Value(ValueType::Int, static_cast<int64_t>(O_NDELAY))},
  {"O_NOCTTY",               Value(ValueType::Int, static_cast<int64_t>(O_NOCTTY))},
  {"O_NOFOLLOW",             Value(ValueType::Int, static_cast<int64_t>(O_NOFOLLOW))},
  {"O_NONBLOCK",             Value(ValueType::Int, static_cast<int64_t>(O_NONBLOCK))},
  {"O_RDONLY",               Value(ValueType::Int, static_cast<int64_t>(O_RDONLY))},
  {"O_RDWR",                 Value(ValueType::Int, static_cast<int64_t>(O_RDWR))},
  {"O_SYNC",                 Value(ValueType::Int, static_cast<int64_t>(O_SYNC))},
  {"O_TRUNC",                Value(ValueType::Int, static_cast<int64_t>(O_TRUNC))},
  {"O_WRONLY",               Value(ValueType::Int, static_cast<int64_t>(O_WRONLY))},
#ifdef MACOSX
  {"O_EXLOCK",               Value(ValueType::Int, static_cast<int64_t>(O_EXLOCK))}, // osx only
  {"O_SHLOCK",               Value(ValueType::Int, static_cast<int64_t>(O_SHLOCK))}, // osx only
#endif
#ifdef LINUX
  {"O_DIRECT",               Value(ValueType::Int, static_cast<int64_t>(O_DIRECT))}, // linux only
  {"O_LARGEFILE",            Value(ValueType::Int, static_cast<int64_t>(O_LARGEFILE))}, // linux only
  {"O_NOATIME",              Value(ValueType::Int, static_cast<int64_t>(O_NOATIME))}, // linux only
  {"O_PATH",                 Value(ValueType::Int, static_cast<int64_t>(O_PATH))}, // linux only
  {"O_RSYNC",                Value(ValueType::Int, static_cast<int64_t>(O_RSYNC))}, // linux only
  {"O_TMPFILE",              Value(ValueType::Int, static_cast<int64_t>(O_TMPFILE))}, // linux only
#endif

  {"environ",                Value(ValueType::Dict, get_environ())},
  {"sysconf_names",          Value(ValueType::Dict, sysconf_names)},

  // unimplemented stuff:

  // {"DirEntry", Value()},
  // {"NGROUPS_MAX", Value()},
  // {"POSIX_FADV_DONTNEED", Value()}, // linux only
  // {"POSIX_FADV_NOREUSE", Value()}, // linux only
  // {"POSIX_FADV_NORMAL", Value()}, // linux only
  // {"POSIX_FADV_RANDOM", Value()}, // linux only
  // {"POSIX_FADV_SEQUENTIAL", Value()}, // linux only
  // {"POSIX_FADV_WILLNEED", Value()}, // linux only
  // {"PRIO_PGRP", Value()},
  // {"PRIO_PROCESS", Value()},
  // {"PRIO_USER", Value()},
  // {"P_ALL", Value()},
  // {"P_PGID", Value()},
  // {"P_PID", Value()},
  // {"RTLD_DEEPBIND", Value()}, // linux only
  // {"RTLD_GLOBAL", Value()},
  // {"RTLD_LAZY", Value()},
  // {"RTLD_LOCAL", Value()},
  // {"RTLD_NODELETE", Value()},
  // {"RTLD_NOLOAD", Value()},
  // {"RTLD_NOW", Value()},
  // {"R_OK", Value()},
  // {"SCHED_BATCH", Value()}, // linux only
  // {"SCHED_FIFO", Value()},
  // {"SCHED_IDLE", Value()}, // linux only
  // {"SCHED_OTHER", Value()},
  // {"SCHED_RESET_ON_FORK", Value()}, // linux only
  // {"SCHED_RR", Value()},
  // {"SEEK_DATA", Value()}, // linux only
  // {"SEEK_HOLE", Value()}, // linux only
  // {"ST_APPEND", Value()}, // linux only
  // {"ST_MANDLOCK", Value()}, // linux only
  // {"ST_NOATIME", Value()}, // linux only
  // {"ST_NODEV", Value()}, // linux only
  // {"ST_NODIRATIME", Value()}, // linux only
  // {"ST_NOEXEC", Value()}, // linux only
  // {"ST_NOSUID", Value()},
  // {"ST_RDONLY", Value()},
  // {"ST_RELATIME", Value()}, // linux only
  // {"ST_SYNCHRONOUS", Value()}, // linux only
  // {"ST_WRITE", Value()}, // linux only
  // {"TMP_MAX", Value()},
  // {"WCONTINUED", Value()},
  // {"WCOREDUMP", Value()},
  // {"WEXITED", Value()},
  // {"WEXITSTATUS", Value()},
  // {"WIFCONTINUED", Value()},
  // {"WIFEXITED", Value()},
  // {"WIFSIGNALED", Value()},
  // {"WIFSTOPPED", Value()},
  // {"WNOHANG", Value()},
  // {"WNOWAIT", Value()},
  // {"WSTOPPED", Value()},
  // {"WSTOPSIG", Value()},
  // {"WTERMSIG", Value()},
  // {"WUNTRACED", Value()},
  // {"W_OK", Value()},
  // {"XATTR_CREATE", Value()}, // linux only
  // {"XATTR_REPLACE", Value()}, // linux only
  // {"XATTR_SIZE_MAX", Value()}, // linux only
  // {"X_OK", Value()},

  // {"_have_functions", Value(ValueType::List, ['HAVE_FACCESSAT', 'HAVE_FCHDIR', 'HAVE_FCHMOD', 'HAVE_FCHMODAT', 'HAVE_FCHOWN', 'HAVE_FCHOWNAT', 'HAVE_FDOPENDIR', 'HAVE_FPATHCONF', 'HAVE_FSTATAT', 'HAVE_FSTATVFS', 'HAVE_FTRUNCATE', 'HAVE_FUTIMES', 'HAVE_LINKAT', 'HAVE_LCHFLAGS', 'HAVE_LCHMOD', 'HAVE_LCHOWN', 'HAVE_LSTAT', 'HAVE_LUTIMES', 'HAVE_MKDIRAT', 'HAVE_OPENAT', 'HAVE_READLINKAT', 'HAVE_RENAMEAT', 'HAVE_SYMLINKAT', 'HAVE_UNLINKAT'])},
});

static void raise_OSError(ExceptionBlock* exc_block, int64_t error_code) {
  raise_python_exception(exc_block, create_single_attr_instance(
      global->OSError_class_id, static_cast<int64_t>(error_code)));
}

static void set_attribute_on_class_instance(const ClassContext* cls,
    void* instance, const char* attribute_name, int64_t value) {
  uint8_t* p = reinterpret_cast<uint8_t*>(instance);
  *reinterpret_cast<int64_t*>(p + cls->offset_for_attribute(attribute_name)) = value;
}

#define simple_wrapper(call, ...) void_fn_ptr([](__VA_ARGS__) -> int64_t { \
    int64_t ret = call; \
    if (ret < 0) { \
      raise_OSError(exc_block, errno); \
    } \
    return ret; \
  })

shared_ptr<ModuleContext> posix_initialize(GlobalContext* global_context) {
  Value Bool(ValueType::Bool);
  Value Bool_True(ValueType::Bool, true);
  Value Bool_False(ValueType::Bool, false);
  Value Int(ValueType::Int);
  Value Float(ValueType::Float);
  Value Bytes(ValueType::Bytes);
  Value Unicode(ValueType::Unicode);
  Value List_Unicode(ValueType::List, vector<Value>({Unicode}));
  Value Dict_Unicode_Unicode(ValueType::Dict, vector<Value>({Unicode, Unicode}));
  Value None(ValueType::None);

  shared_ptr<ModuleContext> module(new ModuleContext(global_context, "posix", globals));

  BuiltinClassDefinition stat_result_def("stat_result", {
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
      {}, void_fn_ptr(&free));

  // note: we don't create stat_result within posix_module because it doesn't
  // have an __init__ function, so it isn't constructible from python code
  static int64_t stat_result_class_id = module->create_builtin_class(stat_result_def);
  Value StatResult(ValueType::Instance, stat_result_class_id, NULL);
  static ClassContext* stat_result_class = global_context->context_for_class(stat_result_class_id);

  static auto convert_stat_result = +[](const struct stat* st) -> void* {
    // TODO: this can be optimized to avoid all the map lookups
    InstanceObject* res = create_instance(stat_result_class_id, stat_result_class->attribute_count());
    set_attribute_on_class_instance(stat_result_class, res, "st_mode", st->st_mode);
    set_attribute_on_class_instance(stat_result_class, res, "st_ino", st->st_ino);
    set_attribute_on_class_instance(stat_result_class, res, "st_dev", st->st_dev);
    set_attribute_on_class_instance(stat_result_class, res, "st_nlink", st->st_nlink);
    set_attribute_on_class_instance(stat_result_class, res, "st_uid", st->st_uid);
    set_attribute_on_class_instance(stat_result_class, res, "st_gid", st->st_gid);
    set_attribute_on_class_instance(stat_result_class, res, "st_size", st->st_size);
    set_attribute_on_class_instance(stat_result_class, res, "st_blocks", st->st_blocks);
    set_attribute_on_class_instance(stat_result_class, res, "st_blksize", st->st_blksize);
    set_attribute_on_class_instance(stat_result_class, res, "st_rdev", st->st_rdev);

#ifdef MACOSX
    set_attribute_on_class_instance(stat_result_class, res, "st_atime_ns", st->st_atimespec.tv_sec * 1000000000 + st->st_atimespec.tv_nsec);
    set_attribute_on_class_instance(stat_result_class, res, "st_mtime_ns", st->st_mtimespec.tv_sec * 1000000000 + st->st_mtimespec.tv_nsec);
    set_attribute_on_class_instance(stat_result_class, res, "st_ctime_ns", st->st_ctimespec.tv_sec * 1000000000 + st->st_ctimespec.tv_nsec);
    double atime = static_cast<double>(st->st_atimespec.tv_sec * 1000000000 + st->st_atimespec.tv_nsec) / 1000000000;
    double mtime = static_cast<double>(st->st_mtimespec.tv_sec * 1000000000 + st->st_mtimespec.tv_nsec) / 1000000000;
    double ctime = static_cast<double>(st->st_ctimespec.tv_sec * 1000000000 + st->st_ctimespec.tv_nsec) / 1000000000;
#else // LINUX
    set_attribute_on_class_instance(stat_result_class, res, "st_atime_ns", st->st_atim.tv_sec * 1000000000 + st->st_atim.tv_nsec);
    set_attribute_on_class_instance(stat_result_class, res, "st_mtime_ns", st->st_mtim.tv_sec * 1000000000 + st->st_mtim.tv_nsec);
    set_attribute_on_class_instance(stat_result_class, res, "st_ctime_ns", st->st_ctim.tv_sec * 1000000000 + st->st_ctim.tv_nsec);
    double atime = static_cast<double>(st->st_atim.tv_sec * 1000000000 + st->st_atim.tv_nsec) / 1000000000;
    double mtime = static_cast<double>(st->st_mtim.tv_sec * 1000000000 + st->st_mtim.tv_nsec) / 1000000000;
    double ctime = static_cast<double>(st->st_ctim.tv_sec * 1000000000 + st->st_ctim.tv_nsec) / 1000000000;
#endif

    set_attribute_on_class_instance(stat_result_class, res, "st_atime", *reinterpret_cast<int64_t*>(&atime));
    set_attribute_on_class_instance(stat_result_class, res, "st_mtime", *reinterpret_cast<int64_t*>(&mtime));
    set_attribute_on_class_instance(stat_result_class, res, "st_ctime", *reinterpret_cast<int64_t*>(&ctime));

    return res;
  };

  vector<BuiltinFunctionDefinition> module_function_defs({
    {"getpid", {}, Int, void_fn_ptr(&getpid), false},
    {"getppid", {}, Int, void_fn_ptr(&getppid), false},
    {"getpgid", {Int}, Int, void_fn_ptr(&getpgid), false},
    {"getpgrp", {}, Int, void_fn_ptr(&getpgrp), false},
    {"getsid", {Int}, Int, void_fn_ptr(&getsid), false},

    {"getuid", {}, Int, void_fn_ptr(&getuid), false},
    {"getgid", {}, Int, void_fn_ptr(&getgid), false},
    {"geteuid", {}, Int, void_fn_ptr(&geteuid), false},
    {"getegid", {}, Int, void_fn_ptr(&getegid), false},

    // these functions never return, so return type is technically unused
    {"_exit", {Int}, Int, void_fn_ptr(&_exit), false},
    {"abort", {}, Int, void_fn_ptr(&abort), false},

    {"close", {Int}, None, simple_wrapper(close(fd), int64_t fd, ExceptionBlock* exc_block), true},

    {"closerange", {Int, Int}, None, void_fn_ptr([](int64_t fd, int64_t end_fd) {
      for (; fd < end_fd; fd++) {
        close(fd);
      }
    }), false},

    {"dup", {Int}, Int, simple_wrapper(dup(fd), int64_t fd, ExceptionBlock* exc_block), true},
    {"dup2", {Int}, Int, simple_wrapper(dup2(fd, new_fd), int64_t fd, int64_t new_fd, ExceptionBlock* exc_block), true},

    {"fork", {}, Int, simple_wrapper(fork(), ExceptionBlock* exc_block), true},

    {"kill", {Int, Int}, Int, simple_wrapper(kill(pid, sig), int64_t pid, int64_t sig, ExceptionBlock* exc_block), true},
    {"killpg", {Int, Int}, Int, simple_wrapper(killpg(pid, sig), int64_t pid, int64_t sig, ExceptionBlock* exc_block), true},

    {"open", {Unicode, Int, Value(ValueType::Int, static_cast<int64_t>(0777))}, Int,
        void_fn_ptr([](UnicodeObject* path, int64_t flags, int64_t mode, ExceptionBlock* exc_block) -> int64_t {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      int64_t ret = open(path_bytes->data, flags, mode);
      delete_reference(path_bytes);

      if (ret < 0) {
        raise_OSError(exc_block, errno);
      }

      return ret;
    }), true},

    {"read", {Int, Int}, Bytes, void_fn_ptr([](int64_t fd, int64_t buffer_size, ExceptionBlock* exc_block) -> BytesObject* {
      BytesObject* ret = bytes_new(NULL, buffer_size);
      ssize_t bytes_read = read(fd, ret->data, buffer_size);
      if (bytes_read >= 0) {
        ret->count = bytes_read;
      } else {
        delete_reference(ret);
        raise_OSError(exc_block, errno);
      }
      return ret;
    }), true},

    {"write", {Int, Bytes}, Int, void_fn_ptr([](int64_t fd, BytesObject* data, ExceptionBlock* exc_block) -> int64_t {
      ssize_t bytes_written = write(fd, data->data, data->count);
      delete_reference(data);
      if (bytes_written < 0) {
        raise_OSError(exc_block, errno);
      }
      return bytes_written;
    }), true},

    {"execv", {Unicode, List_Unicode}, None, void_fn_ptr([](UnicodeObject* path, ListObject* args, ExceptionBlock* exc_block) {

      BytesObject* path_bytes = unicode_encode_ascii(path);

      vector<BytesObject*> args_objects;
      vector<char*> args_pointers;
      for (size_t x = 0; x < args->count; x++) {
        args_objects.emplace_back(unicode_encode_ascii(reinterpret_cast<UnicodeObject*>(args->items[x])));
        args_pointers.emplace_back(args_objects.back()->data);
      }
      args_pointers.emplace_back(nullptr);

      execv(path_bytes->data, args_pointers.data());

      // little optimization: we expect execv to succeed most of the time, so we
      // don't bother deleting path until after it returns (and has failed)
      delete_reference(path);
      delete_reference(path_bytes);
      for (auto& o : args_objects) {
        delete_reference(o);
      }

      raise_OSError(exc_block, errno);
    }), true},

    {"execve", {Unicode, List_Unicode, Dict_Unicode_Unicode}, None,
        void_fn_ptr([](UnicodeObject* path, ListObject* args,
          DictionaryObject* env, ExceptionBlock* exc_block) {

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

      execve(path_bytes->data, args_pointers.data(), envs_pointers.data());

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

      raise_OSError(exc_block, errno);
    }), true},

    {"strerror", {Int}, Unicode, void_fn_ptr([](int64_t code) -> UnicodeObject* {
      char buf[128];

#ifdef LINUX
      // linux has a "feature" where there are two different versions of
      // strerror_r. the GNU-specific one returns a char*, which might not point
      // to buf, probbaly to avoid copying. we can't use the XSI-compliant one
      // because apparantely libstdc++ requires _GNU_SOURCE to be defined, which
      // enables the GNU-specific version of strerror_r. so this means we have
      // to expect buf to be unused sometimes, sigh
      return bytes_decode_ascii(strerror_r(code, buf, sizeof(buf)));
#else
      strerror_r(code, buf, sizeof(buf));
      return bytes_decode_ascii(buf);
#endif
    }), false},

    // TODO: most functions below here should raise OSError on failure instead
    // of returning errno
    {"access", {Unicode, Int}, Int,
        void_fn_ptr([](UnicodeObject* path, int64_t mode) -> int64_t {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      int64_t ret = access(path_bytes->data, mode);
      delete_reference(path_bytes);
      return ret;
    }), false},

    {"chdir", {Unicode}, Int, void_fn_ptr([](UnicodeObject* path) -> int64_t {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      int64_t ret = chdir(path_bytes->data);
      delete_reference(path_bytes);
      return ret;
    }), false},

    {"fchdir", {Int}, Int, void_fn_ptr(&fchdir), false},

    {"chmod", {Unicode, Int}, Int, void_fn_ptr([](UnicodeObject* path, int64_t mode) -> int64_t {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      int64_t ret = chmod(path_bytes->data, mode);
      delete_reference(path_bytes);
      return ret;
    }), false},

    {"fchmod", {Int, Int}, Int, void_fn_ptr(&fchmod), false},

#ifdef MACOSX
    {"chflags", {Unicode, Int}, Int,
        void_fn_ptr([](UnicodeObject* path, int64_t flags) -> int64_t {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      int64_t ret = chflags(path_bytes->data, flags);
      delete_reference(path_bytes);
      return ret;
    }), false},

    {"fchflags", {Int, Int}, Int, void_fn_ptr(&fchflags), false},
#endif

    {"chown", {Unicode, Int, Int}, Int, void_fn_ptr([](UnicodeObject* path, int64_t uid, int64_t gid) -> int64_t {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      int64_t ret = chown(path_bytes->data, uid, gid);
      delete_reference(path_bytes);
      return ret;
    }), false},

    {"lchown", {Unicode, Int, Int}, Int,
        void_fn_ptr([](UnicodeObject* path, int64_t uid, int64_t gid) -> int64_t {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      int64_t ret = lchown(path_bytes->data, uid, gid);
      delete_reference(path_bytes);
      return ret;
    }), false},

    {"fchown", {Int, Int, Int}, Int, void_fn_ptr(&fchown), false},

    {"chroot", {Unicode}, Int, void_fn_ptr([](UnicodeObject* path) -> int64_t {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      int64_t ret = chroot(path_bytes->data);
      delete_reference(path_bytes);
      return ret;
    }), false},

    {"ctermid", {}, Unicode, void_fn_ptr([]() -> UnicodeObject* {
      char result[L_ctermid];
      ctermid(result);
      return bytes_decode_ascii(result);
    }), false},

    {"cpu_count", {}, Int, void_fn_ptr([]() -> int64_t {
      // TODO: should we use procfs here instead?
      return sysconf(_SC_NPROCESSORS_ONLN);
    }), false},

    // TODO: support dir_fd
    {"stat", {Unicode, Bool_True}, StatResult,
        void_fn_ptr([](UnicodeObject* path, bool follow_symlinks, ExceptionBlock* exc_block) -> void* {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      struct stat st;
      int64_t ret = follow_symlinks ? stat(path_bytes->data, &st) : lstat(path_bytes->data, &st);
      delete_reference(path_bytes);

      if (ret) {
        raise_OSError(exc_block, errno);
      }
      return convert_stat_result(&st);
    }), true},

    {"fstat", {Int}, StatResult, void_fn_ptr([](int64_t fd, ExceptionBlock* exc_block) -> void* {
      struct stat st;
      if (fstat(fd, &st)) {
        raise_OSError(exc_block, errno);
      }
      return convert_stat_result(&st);
    }), true},

    {"truncate", {Unicode, Int}, Int, void_fn_ptr([](UnicodeObject* path, int64_t size) -> int64_t {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      int64_t ret = truncate(path_bytes->data, size);
      delete_reference(path_bytes);
      return ret;
    }), false},

    {"ftruncate", {Int, Int}, Int, void_fn_ptr(&ftruncate), false},

    {"getcwd", {}, Unicode,
        void_fn_ptr([](ExceptionBlock* exc_block) -> UnicodeObject* {
      char path[MAXPATHLEN];
      if (!getcwd(path, MAXPATHLEN)) {
        raise_OSError(exc_block, errno);
      }
      return bytes_decode_ascii(path);
    }), true},

    {"getcwdb", {}, Bytes, void_fn_ptr([](ExceptionBlock* exc_block) -> BytesObject* {
      BytesObject* ret = bytes_new(NULL, MAXPATHLEN);
      if (!getcwd(ret->data, MAXPATHLEN)) {
        delete_reference(ret);
        raise_OSError(exc_block, errno);
      } else {
        ret->count = strlen(ret->data);
      }
      return ret;
    }), true},

    {"lseek", {Int, Int, Int}, Int, void_fn_ptr(&lseek), false},
    {"fsync", {Int}, Int, void_fn_ptr(&fsync), false},
    {"isatty", {Int}, Bool, void_fn_ptr(&isatty), false},

    {"listdir", {Value(ValueType::Unicode, L".")}, List_Unicode,
        void_fn_ptr([](UnicodeObject* path) -> void* {
      BytesObject* path_bytes = unicode_encode_ascii(path);
      delete_reference(path);

      // TODO: we shouldn't use list_directory here because it can throw c++
      // exceptions; that will break nemesys-generated code
      auto items = list_directory(path_bytes->data);
      delete_reference(path_bytes);

      ListObject* l = list_new(items.size(), true);
      size_t x = 0;
      for (const auto& item : items) {
        l->items[x++] = bytes_decode_ascii(item.c_str());
      }

      return l;
    }), false},

    // TODO: support passing names as strings
    {"sysconf", {Int}, Int, void_fn_ptr(&sysconf), false},

    // {"confstr", Value()},
    // {"confstr_names", Value()},
    // {"device_encoding", Value()},
    // {"error", Value()},
    // {"fdatasync", Value()}, // linux only
    // {"forkpty", Value()},
    // {"fpathconf", Value()},
    // {"fspath", Value()},
    // {"fstatvfs", Value()},
    // {"get_blocking", Value()},
    // {"get_inheritable", Value()},
    // {"get_terminal_size", Value()},
    // {"getgrouplist", Value()},
    // {"getgroups", Value()},
    // {"getloadavg", Value()},
    // {"getlogin", Value()},
    // {"getpriority", Value()},
    // {"getresgid", Value()}, // linux only
    // {"getresuid", Value()}, // linux only
    // {"getxattr", Value()}, // linux only
    // {"initgroups", Value()},
    // {"lchflags", Value()}, // osx only
    // {"lchmod", Value()}, // osx only
    // {"link", Value()},
    // {"listxattr", Value()}, // linux only
    // {"lockf", Value()},
    // {"major", Value()},
    // {"makedev", Value()},
    // {"minor", Value()},
    // {"mkdir", Value()},
    // {"mkfifo", Value()},
    // {"mknod", Value()},
    // {"nice", Value()},
    // {"openpty", Value()},
    // {"pathconf", Value()},
    // {"pathconf_names", Value()},
    // {"pipe", Value()},
    // {"pipe2", Value()}, // linux only
    // {"posix_fadvise", Value()}, // linux only
    // {"posix_fallocate", Value()}, // linux only
    // {"pread", Value()},
    // {"putenv", Value()},
    // {"pwrite", Value()},
    // {"readlink", Value()},
    // {"readv", Value()},
    // {"remove", Value()},
    // {"removexattr", Value()}, // linux only
    // {"rename", Value()},
    // {"replace", Value()},
    // {"rmdir", Value()},
    // {"scandir", Value()},
    // {"sched_get_priority_max", Value()},
    // {"sched_get_priority_min", Value()},
    // {"sched_getaffinity", Value()}, // linux only
    // {"sched_getparam", Value()}, // linux only
    // {"sched_getscheduler", Value()}, // linux only
    // {"sched_param", Value()}, // linux only
    // {"sched_rr_get_interval", Value()}, // linux only
    // {"sched_setaffinity", Value()}, // linux only
    // {"sched_setparam", Value()}, // linux only
    // {"sched_setscheduler", Value()}, // linux only
    // {"sched_yield", Value()},
    // {"sendfile", Value()},
    // {"set_blocking", Value()},
    // {"set_inheritable", Value()},
    // {"setegid", Value()},
    // {"seteuid", Value()},
    // {"setgid", Value()},
    // {"setgroups", Value()},
    // {"setpgid", Value()},
    // {"setpgrp", Value()},
    // {"setpriority", Value()},
    // {"setregid", Value()},
    // {"setresgid", Value()}, // linux only
    // {"setresuid", Value()}, // linux only
    // {"setreuid", Value()},
    // {"setsid", Value()},
    // {"setuid", Value()},
    // {"setxattr", Value()}, // linux only
    // {"stat_float_times", Value()},
    // {"statvfs", Value()},
    // {"statvfs_result", Value()},
    // {"symlink", Value()},
    // {"sync", Value()},
    // {"system", Value()},
    // {"tcgetpgrp", Value()},
    // {"tcsetpgrp", Value()},
    // {"terminal_size", Value()},
    // {"times", Value()},
    // {"times_result", Value()},
    // {"ttyname", Value()},
    // {"umask", Value()},
    // {"uname", Value()},
    // {"uname_result", Value()},
    // {"unlink", Value()},
    // {"unsetenv", Value()},
    // {"urandom", Value()},
    // {"utime", Value()},
    // {"wait", Value()},
    // {"wait3", Value()},
    // {"wait4", Value()},
    // {"waitid", Value()}, // linux only
    // {"waitid_result", Value()}, // linux only
    // {"waitpid", Value()},
    // {"writev", Value()},
  });

  for (auto& def : module_function_defs) {
    module->create_builtin_function(def);
  }
  return module;
}
