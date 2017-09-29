/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstring>
#include <limits>

#include <xbyak.h>

#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/jitstate.h"
#include "common/assert.h"
#include "dynarmic/callbacks.h"

namespace Dynarmic {
namespace BackendX64 {

constexpr size_t TOTAL_CODE_SIZE = 128 * 1024 * 1024;
constexpr size_t FAR_CODE_OFFSET = 100 * 1024 * 1024;

BlockOfCode::BlockOfCode(UserCallbacks cb, LookupBlockCallback lookup_block, void* lookup_block_arg)
        : Xbyak::CodeGenerator(TOTAL_CODE_SIZE)
        , cb(cb)
        , lookup_block(lookup_block)
        , lookup_block_arg(lookup_block_arg)
        , constant_pool(this, 256)
{
    GenRunCode();
    GenMemoryAccessors();
    exception_handler.Register(this);
    near_code_begin = getCurr();
    far_code_begin = getCurr() + FAR_CODE_OFFSET;
    ClearCache();
}

void BlockOfCode::ClearCache() {
    in_far_code = false;
    near_code_ptr = near_code_begin;
    far_code_ptr = far_code_begin;
    SetCodePtr(near_code_begin);
}

size_t BlockOfCode::RunCode(JitState* jit_state, size_t cycles_to_run) const {
    constexpr size_t max_cycles_to_run = static_cast<size_t>(std::numeric_limits<decltype(jit_state->cycles_remaining)>::max());
    ASSERT(cycles_to_run <= max_cycles_to_run);

    jit_state->cycles_remaining = cycles_to_run;
    run_code(jit_state);
    return cycles_to_run - jit_state->cycles_remaining; // Return number of cycles actually run.
}

void BlockOfCode::ReturnFromRunCode(bool MXCSR_switch) {
    size_t index = 0;
    if (!MXCSR_switch)
        index |= NO_SWITCH_MXCSR;
    jmp(return_from_run_code[index]);
}

void BlockOfCode::ForceReturnFromRunCode(bool MXCSR_switch) {
    size_t index = FORCE_RETURN;
    if (!MXCSR_switch)
        index |= NO_SWITCH_MXCSR;
    jmp(return_from_run_code[index]);
}

void BlockOfCode::GenRunCode() {
    Xbyak::Label loop;

    align();
    run_code = getCurr<RunCodeFuncType>();

    // As we currently do not emit AVX instructions, AVX-SSE transition may occur.
    // We avoid the transition penality by calling vzeroupper.
    MaybeVZEROUPPER();

    // This serves two purposes:
    // 1. It saves all the registers we as a callee need to save.
    // 2. It aligns the stack so that the code the JIT emits can assume
    //    that the stack is appropriately aligned for CALLs.
    ABI_PushCalleeSaveRegistersAndAdjustStack(this);

    mov(r15, ABI_PARAM1);

    L(loop);
    mov(ABI_PARAM1, u64(lookup_block_arg));
    CallFunction(lookup_block);

    SwitchMxcsrOnEntry();
    jmp(ABI_RETURN);

    // Return from run code variants
    const auto emit_return_from_run_code = [this, &loop](bool no_mxcsr_switch, bool force_return){
        if (!no_mxcsr_switch) {
            SwitchMxcsrOnExit();
        }

        if (!force_return) {
            cmp(qword[r15 + offsetof(JitState, cycles_remaining)], 0);
            jg(loop);
        }

        ABI_PopCalleeSaveRegistersAndAdjustStack(this);
        ret();
    };

    align();
    return_from_run_code[0] = getCurr<const void*>();
    emit_return_from_run_code(false, false);

    align();
    return_from_run_code[NO_SWITCH_MXCSR] = getCurr<const void*>();
    emit_return_from_run_code(true, false);

    align();
    return_from_run_code[FORCE_RETURN] = getCurr<const void*>();
    emit_return_from_run_code(false, true);

    align();
    return_from_run_code[NO_SWITCH_MXCSR | FORCE_RETURN] = getCurr<const void*>();
    emit_return_from_run_code(true, true);
}

void BlockOfCode::GenMemoryAccessors() {
    align();
    read_memory_8 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Read8);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    read_memory_16 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Read16);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    read_memory_32 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Read32);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    read_memory_64 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Read64);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    write_memory_8 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Write8);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    write_memory_16 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Write16);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    write_memory_32 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Write32);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    write_memory_64 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Write64);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();
}

void BlockOfCode::SwitchMxcsrOnEntry() {
    stmxcsr(dword[r15 + offsetof(JitState, save_host_MXCSR)]);
    ldmxcsr(dword[r15 + offsetof(JitState, guest_MXCSR)]);
}

void BlockOfCode::SwitchMxcsrOnExit() {
    stmxcsr(dword[r15 + offsetof(JitState, guest_MXCSR)]);
    ldmxcsr(dword[r15 + offsetof(JitState, save_host_MXCSR)]);
}

void BlockOfCode::MaybeVZEROUPPER() {
    if (cpu_info.has(Xbyak::util::Cpu::tAVX)) {
        vzeroupper();
    }
}

Xbyak::Address BlockOfCode::MConst(u64 constant) {
    return constant_pool.GetConstant(constant);
}

void BlockOfCode::SwitchToFarCode() {
    ASSERT(!in_far_code);
    in_far_code = true;
    near_code_ptr = getCurr();
    SetCodePtr(far_code_ptr);

    ASSERT_MSG(near_code_ptr < far_code_begin, "Near code has overwritten far code!");
}

void BlockOfCode::SwitchToNearCode() {
    ASSERT(in_far_code);
    in_far_code = false;
    far_code_ptr = getCurr();
    SetCodePtr(near_code_ptr);
}

void* BlockOfCode::AllocateFromCodeSpace(size_t alloc_size) {
    if (size_ + alloc_size >= maxSize_) {
        throw Xbyak::Error(Xbyak::ERR_CODE_IS_TOO_BIG);
    }

    void* ret = getCurr<void*>();
    size_ += alloc_size;
    memset(ret, 0, alloc_size);
    return ret;
}

void BlockOfCode::SetCodePtr(CodePtr code_ptr) {
    // The "size" defines where top_, the insertion point, is.
    size_t required_size = reinterpret_cast<const u8*>(code_ptr) - getCode();
    setSize(required_size);
}

void BlockOfCode::EnsurePatchLocationSize(CodePtr begin, size_t size) {
    size_t current_size = getCurr<const u8*>() - reinterpret_cast<const u8*>(begin);
    ASSERT(current_size <= size);
    nop(size - current_size);
}

} // namespace BackendX64
} // namespace Dynarmic
