#include "config.h"
#include "instructions.hpp"
#include "interpreter.hpp"
#include "machine_code.hpp"

#include "interpreter/addresses.hpp"

#include "class/call_site.hpp"
#include "class/compiled_code.hpp"
#include "class/constant_cache.hpp"
#include "class/location.hpp"
#include "class/unwind_site.hpp"

#include "diagnostics/measurement.hpp"

namespace rubinius {
  using namespace diagnostics;

  void Interpreter::prepare(STATE, CompiledCode* compiled_code, MachineCode* machine_code) {
    Tuple* lits = compiled_code->literals();
    Tuple* ops = compiled_code->iseq()->opcodes();

    opcode* opcodes = machine_code->opcodes;
    size_t total = machine_code->total;
    size_t stack_size = machine_code->stack_size;

    size_t rcount = 0;
    size_t rindex = 0;
    size_t calls_count = 0;
    size_t constants_count = 0;
    size_t unwind_count = 0;

    for(size_t width = 0, ip = 0; ip < total; ip += width) {
      opcode op = as<Fixnum>(ops->at(ip))->to_native();
      width = Instructions::instruction_data(op).width;

      opcodes[ip] =
        reinterpret_cast<intptr_t>(Instructions::instruction_data(op).interpreter_address);

      switch(width) {
      case 4:
        opcodes[ip + 3] = as<Fixnum>(ops->at(state, ip + 3))->to_native();
        // fall through
      case 3:
        opcodes[ip + 2] = as<Fixnum>(ops->at(state, ip + 2))->to_native();
        // fall through
      case 2:
        opcodes[ip + 1] = as<Fixnum>(ops->at(state, ip + 1))->to_native();
        break;
      case 1:
        continue;
      }

      switch(op) {
      case instructions::data_create_block.id:
      case instructions::data_push_literal.id:
      case instructions::data_push_memo.id:
      case instructions::data_check_serial.id:
      case instructions::data_check_serial_private.id:
      case instructions::data_send_super_stack_with_block.id:
      case instructions::data_send_super_stack_with_splat.id:
      case instructions::data_zsuper.id:
      case instructions::data_send_vcall.id:
      case instructions::data_send_method.id:
      case instructions::data_send_stack.id:
      case instructions::data_send_stack_with_block.id:
      case instructions::data_send_stack_with_splat.id:
      case instructions::data_object_to_s.id:
      case instructions::data_push_const.id:
      case instructions::data_find_const.id:
      case instructions::data_setup_unwind.id:
      case instructions::data_unwind.id:
      case instructions::data_b_if_serial.id:
      case instructions::data_r_load_literal.id:
        rcount++;
        break;
      }
    }

    machine_code->references_count(rcount);
    machine_code->references(new size_t[rcount]);

    bool allow_private = false;
    bool is_super = false;

    for(size_t width = 0, ip = 0; ip < total; ip += width) {
      opcode op = as<Fixnum>(ops->at(ip))->to_native();
      width = Instructions::instruction_data(op).width;

      // Fix register offsets
      switch(op) {
      case instructions::data_b_if_serial.id:
        opcodes[ip + 2] += stack_size;
        break;
      case instructions::data_b_if.id:
      case instructions::data_r_load_local.id:
      case instructions::data_r_store_local.id:
      case instructions::data_r_load_local_depth.id:
      case instructions::data_r_store_local_depth.id:
      case instructions::data_r_load_stack.id:
      case instructions::data_r_store_stack.id:
      case instructions::data_r_load_self.id:
      case instructions::data_r_load_neg1.id:
      case instructions::data_r_load_0.id:
      case instructions::data_r_load_1.id:
      case instructions::data_r_load_2.id:
      case instructions::data_r_load_false.id:
      case instructions::data_r_load_true.id:
      case instructions::data_r_ret.id:
      case instructions::data_m_log.id:
        opcodes[ip + 1] += stack_size;
        break;
      case instructions::data_r_load_nil.id:
        opcodes[ip + 1] += stack_size;
        opcodes[ip + 2] = reinterpret_cast<opcode>(APPLY_NIL_TAG(machine_code->nil_id(), ip));
        break;
      case instructions::data_r_load_literal.id: {
        machine_code->references()[rindex++] = ip + 2;

        Object* value = as<Object>(lits->at(opcodes[ip + 2]));
        opcodes[ip + 2] = reinterpret_cast<opcode>(value);

        opcodes[ip + 1] += stack_size;
        break;
      }
      case instructions::data_n_ineg.id:
      case instructions::data_n_ineg_o.id:
      case instructions::data_n_inot.id:
      case instructions::data_n_iinc.id:
      case instructions::data_n_idec.id:
      case instructions::data_n_ibits.id:
      case instructions::data_n_isize.id:
      case instructions::data_n_iflt.id:
      case instructions::data_n_eneg.id:
      case instructions::data_n_enot.id:
      case instructions::data_n_ebits.id:
      case instructions::data_n_esize.id:
      case instructions::data_n_eflt.id:
      case instructions::data_n_dneg.id:
      case instructions::data_b_if_int.id:
      case instructions::data_b_if_eint.id:
      case instructions::data_b_if_float.id:
      case instructions::data_r_load_int.id:
      case instructions::data_r_store_int.id:
      case instructions::data_r_load_float.id:
      case instructions::data_r_store_float.id:
      case instructions::data_r_load_bool.id:
      case instructions::data_r_load_m_binops.id:
      case instructions::data_r_load_f_binops.id:
      case instructions::data_r_copy.id:
      case instructions::data_n_ipopcnt.id:
      case instructions::data_a_instance.id:
      case instructions::data_a_kind.id:
      case instructions::data_a_method.id:
      case instructions::data_a_receiver_method.id:
      case instructions::data_a_type.id:
      case instructions::data_a_function.id:
      case instructions::data_a_equal.id:
      case instructions::data_a_not_equal.id:
      case instructions::data_a_less.id:
      case instructions::data_a_less_equal.id:
      case instructions::data_a_greater.id:
      case instructions::data_a_greater_equal.id:
        opcodes[ip + 1] += stack_size;
        opcodes[ip + 2] += stack_size;
        break;
      case instructions::data_n_iadd.id:
      case instructions::data_n_isub.id:
      case instructions::data_n_imul.id:
      case instructions::data_n_idiv.id:
      case instructions::data_n_imod.id:
      case instructions::data_n_iand.id:
      case instructions::data_n_ior.id:
      case instructions::data_n_ixor.id:
      case instructions::data_n_ishl.id:
      case instructions::data_n_ishr.id:
      case instructions::data_n_iadd_o.id:
      case instructions::data_n_isub_o.id:
      case instructions::data_n_imul_o.id:
      case instructions::data_n_idiv_o.id:
      case instructions::data_n_imod_o.id:
      case instructions::data_n_idivmod.id:
      case instructions::data_n_ipow_o.id:
      case instructions::data_n_ishl_o.id:
      case instructions::data_n_ishr_o.id:
      case instructions::data_n_icmp.id:
      case instructions::data_n_ieq.id:
      case instructions::data_n_ine.id:
      case instructions::data_n_ilt.id:
      case instructions::data_n_ile.id:
      case instructions::data_n_igt.id:
      case instructions::data_n_ige.id:
      case instructions::data_n_istr.id:
      case instructions::data_n_promote.id:
      case instructions::data_n_demote.id:
      case instructions::data_n_eadd.id:
      case instructions::data_n_esub.id:
      case instructions::data_n_emul.id:
      case instructions::data_n_ediv.id:
      case instructions::data_n_emod.id:
      case instructions::data_n_edivmod.id:
      case instructions::data_n_epow.id:
      case instructions::data_n_eand.id:
      case instructions::data_n_eor.id:
      case instructions::data_n_exor.id:
      case instructions::data_n_eshl.id:
      case instructions::data_n_eshr.id:
      case instructions::data_n_epopcnt.id:
      case instructions::data_n_ecmp.id:
      case instructions::data_n_eeq.id:
      case instructions::data_n_ene.id:
      case instructions::data_n_elt.id:
      case instructions::data_n_ele.id:
      case instructions::data_n_egt.id:
      case instructions::data_n_ege.id:
      case instructions::data_n_estr.id:
      case instructions::data_n_dadd.id:
      case instructions::data_n_dsub.id:
      case instructions::data_n_dmul.id:
      case instructions::data_n_ddiv.id:
      case instructions::data_n_dmod.id:
      case instructions::data_n_ddivmod.id:
      case instructions::data_n_dpow.id:
      case instructions::data_n_dcmp.id:
      case instructions::data_n_deq.id:
      case instructions::data_n_dne.id:
      case instructions::data_n_dlt.id:
      case instructions::data_n_dle.id:
      case instructions::data_n_dgt.id:
      case instructions::data_n_dge.id:
      case instructions::data_n_dstr.id:
        opcodes[ip + 1] += stack_size;
        opcodes[ip + 2] += stack_size;
        opcodes[ip + 3] += stack_size;
        break;
      };

      switch(op) {
      case instructions::data_push_int.id:
        opcodes[ip + 1] = reinterpret_cast<opcode>(Fixnum::from(opcodes[ip + 1]));
        break;
      case instructions::data_push_tagged_nil.id:
        opcodes[ip + 1] = reinterpret_cast<opcode>(APPLY_NIL_TAG(machine_code->nil_id(), ip));
        break;
      case instructions::data_create_block.id: {
        machine_code->references()[rindex++] = ip + 1;

        Object* value = reinterpret_cast<Object*>(lits->at(opcodes[ip + 1]));

        if(CompiledCode* code = try_as<CompiledCode>(value)) {
          opcodes[ip + 1] = reinterpret_cast<opcode>(code);
        } else {
          opcodes[ip + 1] = reinterpret_cast<opcode>(as<String>(value));
        }
        break;
      }
      case instructions::data_push_memo.id:
      case instructions::data_push_literal.id: {
        machine_code->references()[rindex++] = ip + 1;

        Object* value = as<Object>(lits->at(opcodes[ip + 1]));
        opcodes[ip + 1] = reinterpret_cast<opcode>(value);
        break;
      }
      case instructions::data_set_ivar.id:
      case instructions::data_push_ivar.id:
      case instructions::data_set_const.id:
      case instructions::data_set_const_at.id: {
        Symbol* sym = as<Symbol>(lits->at(opcodes[ip + 1]));
        opcodes[ip + 1] = reinterpret_cast<opcode>(sym);
        break;
      }
      case instructions::data_invoke_primitive.id: {
        Symbol* name = as<Symbol>(lits->at(opcodes[ip + 1]));

        InvokePrimitive invoker = Primitives::get_invoke_stub(state, name);
        opcodes[ip + 1] = reinterpret_cast<intptr_t>(invoker);
        break;
      }
      case instructions::data_allow_private.id:
        allow_private = true;

        break;
      case instructions::data_send_super_stack_with_block.id:
      case instructions::data_send_super_stack_with_splat.id:
      case instructions::data_zsuper.id:
        is_super = true;
        // fall through
      case instructions::data_send_vcall.id:
      case instructions::data_send_method.id:
      case instructions::data_send_stack.id:
      case instructions::data_send_stack_with_block.id:
      case instructions::data_send_stack_with_splat.id:
      case instructions::data_object_to_s.id:
      case instructions::data_check_serial.id:
      case instructions::data_check_serial_private.id:
      case instructions::data_b_if_serial.id: {
        machine_code->references()[rindex++] = ip + 1;
        calls_count++;

        Symbol* name = try_as<Symbol>(lits->at(opcodes[ip + 1]));
        if(!name) name = nil<Symbol>();

        CallSite* call_site = CallSite::create(state, name, machine_code->serial(), ip);

        if(op == instructions::data_send_vcall.id) {
          allow_private = true;
          call_site->set_is_vcall();
        } else if(op == instructions::data_object_to_s.id) {
          allow_private = true;
        } else if(op == instructions::data_b_if_serial.id) {
          allow_private = true;
        }

        if(allow_private) call_site->set_is_private();
        if(is_super) call_site->set_is_super();

        machine_code->store_call_site(state, compiled_code, ip, call_site);
        is_super = false;
        allow_private = false;

        break;
      }
      case instructions::data_push_const.id:
      case instructions::data_find_const.id: {
        machine_code->references()[rindex++] = ip + 1;
        constants_count++;

        Symbol* name = as<Symbol>(lits->at(opcodes[ip + 1]));

        ConstantCache* cache = ConstantCache::empty(state, name, compiled_code, ip);
        machine_code->store_constant_cache(state, compiled_code, ip, cache);

        break;
      }
      case instructions::data_setup_unwind.id: {
        machine_code->references()[rindex++] = ip + 1;
        unwind_count++;

        int handler = static_cast<int>(opcodes[ip + 1]);
        UnwindSite::UnwindType type =
            static_cast<UnwindSite::UnwindType>(opcodes[ip + 2]);

        UnwindSite* unwind_site = UnwindSite::create(state, handler, type);

        machine_code->store_unwind_site(state, compiled_code, ip, unwind_site);

        break;
      }
      case instructions::data_unwind.id: {
        machine_code->references()[rindex++] = ip + 1;
        unwind_count++;

        UnwindSite* unwind_site = UnwindSite::create(state, 0, UnwindSite::eNone);

        machine_code->store_unwind_site(state, compiled_code, ip, unwind_site);

        break;
      }
      case instructions::data_m_counter.id: {
        machine_code->store_measurement(state,
            compiled_code, ip, new diagnostics::Counter(state, compiled_code, ip));

        break;
      }
      }
    }

    machine_code->call_site_count(calls_count);
    machine_code->constant_cache_count(constants_count);
    machine_code->unwind_site_count(unwind_count);
  }

  intptr_t Interpreter::execute(STATE, MachineCode* const machine_code) {
    InterpreterState is;
    Exception* exception = 0;
    intptr_t* opcodes = (intptr_t*)machine_code->opcodes;

    CallFrame* call_frame = state->vm()->call_frame();
    call_frame->stack_ptr_ = call_frame->stk - 1;
    call_frame->machine_code = machine_code;
    call_frame->is = &is;

    try {
      return ((instructions::Instruction)opcodes[call_frame->ip()])(state, call_frame, opcodes);
    } catch(TypeError& e) {
      exception = Exception::make_type_error(state, e.type, e.object, e.reason);
      exception->locations(state, Location::from_call_stack(state));

      call_frame->scope->flush_to_heap(state);
    } catch(RubyException& exc) {
      if(exc.exception->locations()->nil_p()) {
        exc.exception->locations(state, Location::from_call_stack(state));
      }
      exception = exc.exception;
    } catch(std::exception& e) {
      exception = Exception::make_interpreter_error(state, e.what());
      exception->locations(state, Location::from_call_stack(state));

      call_frame->scope->flush_to_heap(state);
    } catch(...) {
      exception = Exception::make_interpreter_error(state, "unknown C++ exception thrown");
      exception->locations(state, Location::from_call_stack(state));

      call_frame->scope->flush_to_heap(state);
    }

    state->raise_exception(exception);
    return 0;
  }
}
