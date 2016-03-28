#ifndef RBX_BUILTIN_JIT_HPP
#define RBX_BUILTIN_JIT_HPP

#include "object_utils.hpp"

#include "builtin/array.hpp"
#include "builtin/block_environment.hpp"
#include "builtin/compiled_code.hpp"
#include "builtin/class.hpp"
#include "builtin/list.hpp"
#include "builtin/module.hpp"
#include "builtin/object.hpp"

#include "util/thread.hpp"

namespace rubinius {
  class JITCompileRequest : public Object {
  public:
    const static object_type type = JITCompileRequestType;

    attr_accessor(method, CompiledCode);
    attr_accessor(receiver_class, Class);
    attr_accessor(block_env, BlockEnvironment);

  private:
    utilities::thread::Condition* waiter_;
    int hits_;
    bool is_block_;

  public:
    static void initialize(STATE, JITCompileRequest* obj) {
      obj->method_ = nil<CompiledCode>();
      obj->receiver_class_ = nil<Class>();
      obj->block_env_ = nil<BlockEnvironment>();
      obj->waiter_ = NULL;
      obj->hits_ = 0;
      obj->is_block_ = false;
    }

    static JITCompileRequest* create(STATE, CompiledCode* code, Class* receiver_class,
        int hits, BlockEnvironment* block_env = NULL, bool is_block = false);

    MachineCode* machine_code() {
      return method()->machine_code();
    }

    bool is_block() {
      return is_block_;
    }

    int hits() {
      return hits_;
    }

    void set_waiter(utilities::thread::Condition* cond) {
      waiter_ = cond;
    }

    utilities::thread::Condition* waiter() {
      return waiter_;
    }

    class Info : public TypeInfo {
    public:
      BASIC_TYPEINFO(TypeInfo)
    };
  };

  class JIT : public Module {
  public:
    const static object_type type = JITType;

    attr_accessor(compile_class, Class);
    attr_accessor(compile_list, List);
    attr_accessor(available, Object);
    attr_accessor(enabled, Object);
    attr_accessor(properties, Array);

    static void bootstrap(STATE);
    static void initialize(STATE, JIT* obj) {
      obj->compile_class_ = nil<Class>();
      obj->compile_list_ = nil<List>();
      obj->available_ = nil<Object>();
      obj->enabled_ = nil<Object>();
      obj->properties_ = nil<Array>();
    }

    static void initialize(STATE, JIT* obj, Module* under, const char* name);

    Object* enable(STATE);

    Object* compile_soon(STATE, CompiledCode* code,
        Class* receiver_class, BlockEnvironment* block_env=NULL, bool is_block=false);
    Object* compile_callframe(STATE, CompiledCode* code,
        int primitive=-1);
    Object* start_method_update(STATE);
    Object* end_method_update(STATE);

    // Rubinius.primitive :jit_compile
    Object* compile(STATE, Object* object, CompiledCode* code, Object* block_environment);

    // Rubinius.primitive :jit_compile_threshold
    Object* compile_threshold(STATE);

    // Rubinius.primitive :jit_sync_set
    Object* sync_set(STATE, Object* flag);

    // Rubinius.primitive :jit_sync_get
    Object* sync_get(STATE);

    class Info : public Module::Info {
    public:
      BASIC_TYPEINFO(Module::Info)
    };
  };
}

#endif
