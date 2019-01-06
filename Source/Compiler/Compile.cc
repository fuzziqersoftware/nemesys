#include "Compile.hh"

#include <phosg/Strings.hh>

#include "../Debug.hh"
#include "../AST/PythonLexer.hh"
#include "../AST/PythonParser.hh"
#include "../Compiler/AnnotationVisitor.hh"
#include "../Compiler/AnalysisVisitor.hh"
#include "../Compiler/BuiltinFunctions.hh"
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
  string scope_name = module->name + "+ADVANCE";
  if (!global->scopes_in_progress.emplace(scope_name).second) {
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

            fprintf(stderr, "# split count: %" PRIu64 "\n", module->root_fragment_num_splits);
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
          compile_fragment(global, module, &module->root_fragment);

          if (debug_flags & DebugFlag::ShowCompileDebug) {
            fprintf(stderr, "[%s] ======== executing root scope\n",
                module->name.c_str());
          }

          // all imports are done statically, so we can't translate this to a
          // python exception - just fail
          void* (*compiled_root_scope)() = reinterpret_cast<void* (*)()>(const_cast<void*>(module->root_fragment.compiled));
          void* exc = compiled_root_scope();
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

  global->scopes_in_progress.erase(scope_name);
}


void compile_fragment(GlobalContext* global, ModuleContext* module,
    Fragment* f) {
  if (f->function && (f->function->module != module)) {
    throw compile_error("module context does not match fragment function module");
  }
  if (f->function && (f->arg_types.size() != f->function->args.size())) {
    throw compile_error("function and fragment have different argument counts");
  }

  // generate the scope name
  string scope_name;
  if (f->function) {
    auto* cls = global->context_for_class(f->function->class_id);
    if (cls) {
      scope_name = string_printf("%s.%s.%s+%" PRId64, module->name.c_str(),
          cls->name.c_str(), f->function->name.c_str(), f->function->id);
    } else {
      scope_name = string_printf("%s.%s+%" PRId64, module->name.c_str(),
          f->function->name.c_str(), f->function->id);
    }

    scope_name += '(';
    bool is_first = true;
    for (size_t x = 0; x < f->arg_types.size(); x++) {
      if (!is_first) {
        scope_name += ',';
      } else {
        is_first = false;
      }
      scope_name += f->function->args[x].name;
      scope_name += '=';
      scope_name += f->arg_types[x].str();
    }
    scope_name += ')';

  } else {
    scope_name = module->name + "+ROOT";
  }

  // create the compilation visitor
  CompilationVisitor v(global, module, f);

  if (!global->scopes_in_progress.emplace(scope_name).second) {
    throw compile_error("recursive compilation attempt");
  }

  // compile it
  try {
    if (f->function) {
      f->function->ast_root->accept(&v);
    } else {
      module->ast_root->accept(&v);
    }

  } catch (const CompilationVisitor::terminated_by_split&) {
    // ignore this; the fragment was compiled but is incomplete (contains calls
    // to the JIT compiler)

  } catch (compile_error& e) {
    if (e.where < 0) {
      e.where = v.get_file_offset();
    }

    global->scopes_in_progress.erase(scope_name);
    if (debug_flags & DebugFlag::ShowCodeSoFar) {
      fprintf(stderr, "[%s] ======== compilation failed\ncode so far:\n",
          scope_name.c_str());

      unordered_set<size_t> patch_offsets;
      const string& compiled = v.assembler().assemble(&patch_offsets,
          &f->compiled_labels, 0, true);
      string disassembly = AMD64Assembler::disassemble(compiled.data(),
          compiled.size(), 0, &f->compiled_labels);
      fprintf(stderr, "\n%s\n", disassembly.c_str());
    }
    global->print_compile_error(stderr, module, e);
    throw;
  }
  global->scopes_in_progress.erase(scope_name);

  if (debug_flags & DebugFlag::ShowCompileDebug) {
    fprintf(stderr, "[%s] ======== scope compiled\n\n",
        scope_name.c_str());
  }

  // modules cannot return values
  if (!f->function && !v.return_types().empty()) {
    throw compile_error("module root scope provided a return type");
  }

  if (v.return_types().size() > 1) {
    throw compile_error("scope has multiple return types");
  }
  if (!v.return_types().empty()) {
    // there's exactly one return type
    f->return_type = *v.return_types().begin();
  } else {
    f->return_type = Value(ValueType::None);
  }

  unordered_set<size_t> patch_offsets;
  f->compiled_labels.clear();
  string compiled = v.assembler().assemble(&patch_offsets, &f->compiled_labels);
  f->compiled = global->code.append(compiled, &patch_offsets);
  module->compiled_size += compiled.size();

  f->resolve_call_split_labels();

  if (debug_flags & DebugFlag::ShowAssembly) {
    fprintf(stderr, "[%s] ======== scope assembled\n", scope_name.c_str());
    uint64_t addr = reinterpret_cast<uint64_t>(f->compiled);
    string disassembly = AMD64Assembler::disassemble(f->compiled,
        compiled.size(), addr, &f->compiled_labels);
    fprintf(stderr, "\n%s", disassembly.c_str());

    for (size_t x = 0; x < f->call_split_offsets.size(); x++) {
      ssize_t offset = f->call_split_offsets[x];
      if (offset < 0) {
        fprintf(stderr, "# split %zu is missing\n", x);
      } else {
        uint64_t addr = reinterpret_cast<uint64_t>(reinterpret_cast<const uint8_t*>(f->compiled) + offset);
        fprintf(stderr, "# split %zu at offset %zu (%016" PRIX64 ")\n", x, offset, addr);
      }
    }
  }
}


const void* jit_compile_scope(GlobalContext* global, int64_t callsite_token,
    uint64_t* int_args, void** raise_exception) {
  if (debug_flags & DebugFlag::ShowJITEvents) {
    fprintf(stderr, "[jit_callsite:%" PRId64 "] ======== jit compile call\n",
        callsite_token);
  }

  auto create_compiler_error_exception = [&](const char* what) -> void* {
    if (debug_flags & DebugFlag::ShowJITEvents) {
      fprintf(stderr, "[jit_callsite:%" PRId64 "] failed: %s\n",
          callsite_token, what);
    }
    // TODO: this is a memory leak! we need to call delete_reference
    // appropriately here based on the contents of int_args and their types
    return create_single_attr_instance(NemesysCompilerError_class_id,
        reinterpret_cast<int64_t>(what));
  };

  // get the callsite object
  GlobalContext::UnresolvedFunctionCall* callsite;
  try {
    callsite = &global->unresolved_callsites.at(callsite_token);
  } catch (const out_of_range&) {
    *raise_exception = create_compiler_error_exception(
        "callsite reference object is missing");
    return NULL;
  }

  if (debug_flags & DebugFlag::ShowJITEvents) {
    string s = callsite->str();
    fprintf(stderr, "[jit_callsite:%" PRId64 "] callsite is %s\n",
        callsite_token, s.c_str());
  }

  // get the caller function object (if it's not a module root scope)
  FunctionContext* caller_fn = NULL;
  if (callsite->caller_function_id) {
    try {
      caller_fn = &global->function_id_to_context.at(callsite->caller_function_id);
    } catch (const out_of_range&) {
      *raise_exception = create_compiler_error_exception(
          "caller function context is missing");
      return NULL;
    }
  }

  // get the caller fragment object
  Fragment* caller_fragment;
  if (caller_fn) {
    try {
      caller_fragment = &caller_fn->fragments.at(callsite->caller_fragment_index);
    } catch (const out_of_range&) {
      *raise_exception = create_compiler_error_exception(
          "caller fragment is missing");
      return NULL;
    }
  } else {
    caller_fragment = &callsite->caller_module->root_fragment;
  }

  if (caller_fragment->call_split_offsets[callsite->caller_split_id] < 0) {
    if (debug_flags & DebugFlag::ShowJITEvents) {
      fprintf(stderr, "[jit_callsite:%" PRId64 "] caller fragment does not contain split %" PRId64 "; recompiling\n",
          callsite_token, callsite->caller_split_id);
    }

    // get the callee function object
    FunctionContext* callee_fn;
    try {
      callee_fn = &global->function_id_to_context.at(callsite->callee_function_id);
    } catch (const out_of_range&) {
      *raise_exception = create_compiler_error_exception(
          "callee function context is missing");
      return NULL;
    }

    if (debug_flags & DebugFlag::ShowJITEvents) {
      fprintf(stderr, "[jit_callsite:%" PRId64 "] callee function id is %" PRId64 " (%s)\n",
          callsite_token, callsite->callee_function_id, callee_fn->name.c_str());
      fprintf(stderr, "[jit_callsite:%" PRId64 "] advancing module to Analyzed phase\n",
          callsite_token);
    }

    // make sure the callee module is in a reasonable state. note that we don't
    // advance it to Imported here because its root scope could currently be
    // running (which would mean it's still in Analyzed)
    advance_module_phase(global, callee_fn->module, ModuleContext::Phase::Analyzed);

    // check if a fragment already exists - someone else might have compiled it
    // before us
    int64_t callee_fragment_index = callee_fn->fragment_index_for_call_args(callsite->arg_types);
    if (callee_fragment_index == -1) {
      // there's no appropriate fragment

      if (debug_flags & DebugFlag::ShowJITEvents) {
        fprintf(stderr, "[jit_callsite:%" PRId64 "] creating new fragment\n",
            callsite_token);
      }

      int64_t callee_fragment_index = callee_fn->fragments.size();
      callee_fn->fragments.emplace_back(callee_fn, callee_fragment_index, callsite->arg_types);

      if (debug_flags & DebugFlag::ShowJITEvents) {
        fprintf(stderr, "[jit_callsite:%" PRId64 "] compiling fragment\n",
            callsite_token);
      }

      // compile the thing
      try {
        compile_fragment(global, callee_fn->module, &callee_fn->fragments.back());
      } catch (const exception& e) {
        *raise_exception = create_compiler_error_exception(e.what());
        return NULL;
      }
    }

    if (debug_flags & DebugFlag::ShowJITEvents) {
      fprintf(stderr, "[jit_callsite:%" PRId64 "] using callee fragment %" PRId64 "\n",
          callsite_token, callee_fragment_index);
      fprintf(stderr, "[jit_callsite:%" PRId64 "] recompiling caller fragment\n",
          callsite_token);
    }

    try {
      compile_fragment(global, callsite->caller_module, caller_fragment);
    } catch (const exception& e) {
      *raise_exception = create_compiler_error_exception(e.what());
      return NULL;
    }
  }

  // now the caller fragment should have more splits than needed
  size_t caller_split_offset = caller_fragment->call_split_offsets[callsite->caller_split_id];
  if (caller_split_offset < 0) {
    throw compile_error(string_printf(
        "caller fragment did not have enough splits after recompilation (have %zu, need id %" PRId64 ")",
        caller_fragment->call_split_offsets.size(), callsite->caller_split_id));
  }
  const void* split_location = reinterpret_cast<const uint8_t*>(caller_fragment->compiled) + caller_split_offset;

  if (debug_flags & DebugFlag::ShowJITEvents) {
    fprintf(stderr, "[jit_callsite:%" PRId64 "] compilation successful; returning to %p\n",
        callsite_token, split_location);
  }

  return split_location;
}
