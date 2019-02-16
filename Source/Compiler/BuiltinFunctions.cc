#include "BuiltinFunctions.hh"

#include <inttypes.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Contexts.hh"
#include "../Types/Strings.hh"
#include "../Types/Dictionary.hh"
#include "../Types/List.hh"
#include "../Types/Instance.hh"
#include "../AST/PythonLexer.hh" // for escape()

// builtin module implementations
#include "../Modules/__nemesys__.hh"
#include "../Modules/builtins.hh"
#include "../Modules/errno.hh"
#include "../Modules/math.hh"
#include "../Modules/posix.hh"
#include "../Modules/sys.hh"
#include "../Modules/time.hh"
#include "Compile.hh"

using namespace std;
using FragDef = BuiltinFragmentDefinition;



InstanceObject MemoryError_instance;



#define DECLARE_MODULE(name) {#name, name##_initialize}
typedef shared_ptr<ModuleContext> (*module_constructor_t)(GlobalContext*);
static unordered_map<string, module_constructor_t> builtin_modules({
  DECLARE_MODULE(__nemesys__),
  DECLARE_MODULE(builtins),
  DECLARE_MODULE(errno),
  DECLARE_MODULE(math),
  DECLARE_MODULE(posix),
  DECLARE_MODULE(sys),
  DECLARE_MODULE(time),
});
#undef DECLARE_MODULE

shared_ptr<ModuleContext> create_builtin_module(GlobalContext* global,
    const string& module_name) {
  try {
    auto& constructor = builtin_modules.at(module_name);
    auto m = constructor(global);
    initialize_global_space_for_module(global, m.get());
    return m;
  } catch (const out_of_range&) {
    return NULL;
  }
}
