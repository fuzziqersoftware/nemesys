#include "Compile.hh"

#include <phosg/Strings.hh>

#include "../Debug.hh"
#include "../AST/PythonLexer.hh"
#include "../AST/PythonParser.hh"
#include "../Compiler/AnnotationVisitor.hh"
#include "../Compiler/AnalysisVisitor.hh"
#include "../Compiler/CompilationVisitor.hh"
#include "../Types/List.hh"
#include "../Types/Dictionary.hh"

using namespace std;



int64_t construct_value(GlobalContext* global, const Value& value,
    bool use_shared_constants) {
  switch (value.type) {
    case ValueType::None:
      return 0;

    case ValueType::Bool:
    case ValueType::Int:
    case ValueType::Float:
      // returning int_value for Float here is not an error. this function
      // returns the raw (binary) contents of the cell that this value would
      // occupy, and int_value is unioned with float_value so it accurately
      // represents the value too
      return value.int_value;

    case ValueType::Bytes:
      return reinterpret_cast<int64_t>(global->get_or_create_constant(
          *value.bytes_value, use_shared_constants));

    case ValueType::Unicode:
      return reinterpret_cast<int64_t>(global->get_or_create_constant(
          *value.unicode_value, use_shared_constants));

    case ValueType::Function:
    case ValueType::Module:
      return 0;

    case ValueType::List: {
      ListObject* l = list_new(value.list_value->size(),
          type_has_refcount(value.extension_types[0].type));
      for (size_t x = 0; x < value.list_value->size(); x++) {
        l->items[x] = reinterpret_cast<void*>(
            construct_value(global, *(*value.list_value)[x], false));
      }
      return reinterpret_cast<int64_t>(l);
    }

    case ValueType::Dict: {
      size_t (*key_length)(const void*) = NULL;
      uint8_t (*key_at)(const void*, size_t) = NULL;
      if (value.extension_types[0].type == ValueType::Bytes) {
        key_length = reinterpret_cast<size_t (*)(const void*)>(bytes_length);
        key_at = reinterpret_cast<uint8_t (*)(const void*, size_t)>(bytes_at);
      } else if (value.extension_types[0].type == ValueType::Unicode) {
        key_length = reinterpret_cast<size_t (*)(const void*)>(unicode_length);
        key_at = reinterpret_cast<uint8_t (*)(const void*, size_t)>(unicode_at);
      } else {
        throw compile_error("dictionary key type does not have sequence functions");
      }

      uint64_t flags = (type_has_refcount(value.extension_types[0].type) ? DictionaryFlag::KeysAreObjects : 0) |
          (type_has_refcount(value.extension_types[1].type) ? DictionaryFlag::ValuesAreObjects : 0);
      DictionaryObject* d = dictionary_new(key_length, key_at, flags);

      for (const auto& item : *value.dict_value) {
        dictionary_insert(d,
            reinterpret_cast<void*>(construct_value(global, item.first, false)),
            reinterpret_cast<void*>(construct_value(global, *item.second, false)));
      }
      return reinterpret_cast<int64_t>(d);
    }

    case ValueType::Tuple:
    case ValueType::Set:
    case ValueType::Class:
    default: {
      string value_str = value.str();
      throw compile_error("static construction unimplemented for " + value_str);
      // TODO: implement static constructors for collections
    }
  }
}


void initialize_global_space_for_module(GlobalContext* global,
    ModuleContext* module) {

  // clear everything first
  memset(&global->global_space[module->global_base_offset / 8], 0,
      sizeof(int64_t) * module->globals.size());

  // for built-in modules, construct everything statically
  size_t slot = module->global_base_offset / 8;
  for (const auto& it : module->globals) {
    // if the module is dynamic, only initialize a few globals (which the root
    // scope doesn't initialize)
    if (module->ast_root.get() &&
        !static_initialize_module_attributes.count(it.first)) {
      continue;
    }

    if (!it.second.value_known) {
      throw compile_error(string_printf("built-in global %s has unknown value",
          it.first.c_str()));
    }

    global->global_space[slot] = construct_value(global, it.second, true);
    slot++;
  }
}

void advance_module_phase(GlobalContext* global, ModuleContext* module,
    ModuleContext::Phase phase) {
  if (module->phase >= phase) {
    return;
  }

  // prevent infinite recursion: advance_module_phase cannot be called for a
  // module on which it is already executing (unless it would do nothing, above)
  if (!global->modules_in_progress.emplace(module).second) {
    throw compile_error("cyclic import dependency on module " + module->name);
  }

  while (module->phase < phase) {
    switch (module->phase) {
      case ModuleContext::Phase::Initial: {
        if (module->source.get()) {
          shared_ptr<PythonLexer> lexer(new PythonLexer(module->source));
          if (debug_flags & DebugFlag::ShowLexDebug) {
            fprintf(stderr, "[%s] ======== module lexed\n", module->name.c_str());
            const auto& tokens = lexer->get_tokens();
            for (size_t y = 0; y < tokens.size(); y++) {
              const auto& token = tokens[y];
              fprintf(stderr, "      n:%5lu type:%16s s:%s f:%lf i:%" PRId64
                  " off:%lu len:%lu\n", y,
                  PythonLexer::Token::name_for_token_type(token.type),
                  token.string_data.c_str(), token.float_data, token.int_data,
                  token.text_offset, token.text_length);
            }
            fputc('\n', stderr);
          }
          PythonParser parser(lexer);
          module->ast_root = parser.get_root();
          if (debug_flags & DebugFlag::ShowParseDebug) {
            fprintf(stderr, "[%s] ======== module parsed\n", module->name.c_str());
            module->ast_root->print(stderr);
            fputc('\n', stderr);
          }
        } else if (debug_flags & (DebugFlag::ShowLexDebug | DebugFlag::ShowParseDebug)) {
          fprintf(stderr, "[%s] ======== no lexing/parsing for built-in module\n", module->name.c_str());
        }

        module->phase = ModuleContext::Phase::Parsed;
        break;
      }

      case ModuleContext::Phase::Parsed: {
        if (module->ast_root.get()) {
          AnnotationVisitor v(global, module);
          try {
            module->ast_root->accept(&v);
          } catch (const compile_error& e) {
            global->print_compile_error(stderr, module, e);
            throw;
          }
        }

        // reserve space for this module's globals
        module->global_base_offset = global->reserve_global_space(
            sizeof(int64_t) * module->globals.size());

        if (debug_flags & DebugFlag::ShowAnnotateDebug) {
          fprintf(stderr, "[%s] ======== module annotated\n", module->name.c_str());
          if (module->ast_root.get()) {
            module->ast_root->print(stderr);

            fprintf(stderr, "# split count: %" PRIu64 "\n", module->root_scope_num_splits);
          }

          for (const auto& it : module->globals) {
            fprintf(stderr, "# global: %s\n", it.first.c_str());
          }
          fprintf(stderr, "# global space is now %p (%" PRId64 " bytes)\n",
              global->global_space, global->global_space_used);
          fputc('\n', stderr);
        }
        module->phase = ModuleContext::Phase::Annotated;
        break;
      }

      case ModuleContext::Phase::Annotated: {
        if (module->ast_root.get()) {
          AnalysisVisitor v(global, module);
          try {
            module->ast_root->accept(&v);
          } catch (const compile_error& e) {
            global->print_compile_error(stderr, module, e);
            throw;
          }
        }

        if (debug_flags & DebugFlag::ShowAnalyzeDebug) {
          fprintf(stderr, "[%s] ======== module analyzed\n", module->name.c_str());
          if (module->ast_root.get()) {
            module->ast_root->print(stderr);
          }

          int64_t offset = module->global_base_offset;
          for (const auto& it : module->globals) {
            string value_str = it.second.str();
            fprintf(stderr, "# global at r13+%" PRIX64 ": %s = %s\n",
                offset, it.first.c_str(), value_str.c_str());
            offset += 8;
          }
          fputc('\n', stderr);
        }

        initialize_global_space_for_module(global, module);

        if (debug_flags & DebugFlag::ShowAnalyzeDebug) {
          fprintf(stderr, "[%s] ======== global space updated\n",
              module->name.c_str());
          print_data(stderr, global->global_space, global->global_space_used,
              reinterpret_cast<uint64_t>(global->global_space));
          fputc('\n', stderr);
        }

        module->phase = ModuleContext::Phase::Analyzed;
        break;
      }

      case ModuleContext::Phase::Analyzed: {
        if (module->ast_root.get()) {
          auto fragment = compile_scope(global, module);
          module->compiled_root_scope = reinterpret_cast<void*(*)()>(const_cast<void*>(fragment.compiled));
          module->compiled_labels = move(fragment.compiled_labels);

          if (debug_flags & DebugFlag::ShowCompileDebug) {
            fprintf(stderr, "[%s] ======== executing root scope\n",
                module->name.c_str());
          }

          // all imports are done statically, so we can't translate this to a
          // python exception - just fail
          void* exc = module->compiled_root_scope();
          if (exc) {
            int64_t class_id = *reinterpret_cast<int64_t*>(reinterpret_cast<int64_t*>(exc) + 2);
            ClassContext* cls = global->context_for_class(class_id);
            const char* class_name = cls ? cls->name.c_str() : "<missing>";
            throw compile_error(string_printf(
                "module root scope raised exception of class %" PRId64 " (%s)",
                class_id, class_name));
          }
        }

        if (debug_flags & DebugFlag::ShowCompileDebug) {
          fprintf(stderr, "\n[%s] ======== import complete\n\n",
              module->name.c_str());
        }

        module->phase = ModuleContext::Phase::Imported;
        break;
      }

      case ModuleContext::Phase::Imported:
        break; // nothing to do
    }
  }

  global->modules_in_progress.erase(module);
}

FunctionContext::Fragment compile_scope(GlobalContext* global,
    ModuleContext* module, FunctionContext* fn,
    const unordered_map<string, Value>* local_overrides) {

  // if a context is given, then the module must match it
  if (fn && (fn->module != module)) {
    throw compile_error("module context incorrect for function");
  }

  // if a context is not given, local_overrides must not be given either
  if (!fn && local_overrides) {
    throw compile_error("local overrides cannot be given for module scope");
  }

  // create the compilation visitor
  string scope_name;
  multimap<size_t, string> compiled_labels;
  unordered_set<size_t> patch_offsets;
  unique_ptr<CompilationVisitor> v;
  if (fn) {
    auto* cls = global->context_for_class(fn->class_id);
    if (cls) {
      scope_name = string_printf("%s.%s.%s+%" PRId64, module->name.c_str(),
          cls->name.c_str(), fn->name.c_str(), fn->id);
    } else {
      scope_name = string_printf("%s.%s+%" PRId64, module->name.c_str(),
          fn->name.c_str(), fn->id);
    }
    if (local_overrides) {
      scope_name += '(';
      bool is_first = true;
      for (const auto& override : *local_overrides) {
        if (!is_first) {
          scope_name += ',';
        } else {
          is_first = false;
        }
        scope_name += override.first;
        scope_name += '=';
        scope_name += override.second.str();
      }
      scope_name += ')';
    }
    v.reset(new CompilationVisitor(global, module, fn->id, 0, local_overrides));
  } else {
    scope_name = module->name;
    v.reset(new CompilationVisitor(global, module));
  }

  // write the local overrides, if any
  if ((debug_flags & DebugFlag::ShowCompileDebug) && local_overrides &&
      !local_overrides->empty()) {
    fprintf(stderr, "[%s] ======== compiling with local overrides\n",
        scope_name.c_str());
    for (const auto& it : *local_overrides) {
      string value_str = it.second.str();
      fprintf(stderr, "%s = %s\n", it.first.c_str(), value_str.c_str());
    }
    fputc('\n', stderr);
  }

  static unordered_set<string> scopes_in_progress;
  if (!scopes_in_progress.emplace(scope_name).second) {
    throw compile_error("recursive compilation attempt");
  }

  // compile it
  try {
    if (fn) {
      fn->ast_root->accept(v.get());
    } else {
      module->ast_root->accept(v.get());
    }

  } catch (const compile_error& e) {
    scopes_in_progress.erase(scope_name);
    if (debug_flags & DebugFlag::ShowCodeSoFar) {
      fprintf(stderr, "[%s] ======== compilation failed\ncode so far:\n",
          scope_name.c_str());
      const string& compiled = v->assembler().assemble(patch_offsets,
          &compiled_labels, 0, true);
      string disassembly = AMD64Assembler::disassemble(compiled.data(),
          compiled.size(), 0, &compiled_labels);
      fprintf(stderr, "\n%s\n", disassembly.c_str());
    }
    global->print_compile_error(stderr, module, e);
    throw;
  }
  scopes_in_progress.erase(scope_name);

  if (debug_flags & DebugFlag::ShowCompileDebug) {
    fprintf(stderr, "[%s] ======== scope compiled\n\n",
        scope_name.c_str());
  }

  Value return_type(ValueType::None);
  if (v->return_types().size() > 1) {
    throw compile_error("scope has multiple return types");
  }
  if (!v->return_types().empty()) {
    // there's exactly one return type
    return_type = *v->return_types().begin();
  }

  string compiled = v->assembler().assemble(patch_offsets, &compiled_labels);
  const void* executable = global->code.append(compiled, &patch_offsets);
  module->compiled_size += compiled.size();

  if (debug_flags & DebugFlag::ShowAssembly) {
    fprintf(stderr, "[%s] ======== scope assembled\n", scope_name.c_str());
    uint64_t addr = reinterpret_cast<uint64_t>(executable);
    string disassembly = AMD64Assembler::disassemble(executable,
        compiled.size(), addr, &compiled_labels);
    fprintf(stderr, "\n%s\n", disassembly.c_str());
  }

  return FunctionContext::Fragment(return_type, executable, move(compiled_labels));
}