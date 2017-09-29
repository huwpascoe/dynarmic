/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <memory>
#include <type_traits>

#include <xbyak.h>
#include <xbyak_util.h>

#include "backend_x64/constant_pool.h"
#include "backend_x64/jitstate.h"
#include "common/common_types.h"
#include "dynarmic/callbacks.h"

namespace Dynarmic {
namespace BackendX64 {

using LookupBlockCallback = CodePtr(*)(void*);

class BlockOfCode final : public Xbyak::CodeGenerator {
public:
    BlockOfCode(UserCallbacks cb, LookupBlockCallback lookup_block, void* lookup_block_arg);

    /// Clears this block of code and resets code pointer to beginning.
    void ClearCache();

    /// Runs emulated code for approximately `cycles_to_run` cycles.
    size_t RunCode(JitState* jit_state, size_t cycles_to_run) const;
    /// Code emitter: Returns to dispatcher
    void ReturnFromRunCode(bool MXCSR_switch = true);
    /// Code emitter: Returns to dispatcher, forces return to host
    void ForceReturnFromRunCode(bool MXCSR_switch = true);
    /// Code emitter: Makes guest MXCSR the current MXCSR
    void SwitchMxcsrOnEntry();
    /// Code emitter: Makes saved host MXCSR the current MXCSR
    void SwitchMxcsrOnExit();
    /// Code emitter: If the CPU supports AVX, emit a VZEROUPPER instruction.
    void MaybeVZEROUPPER();

    /// Code emitter: Calls the function
    template <typename FunctionPointer>
    void CallFunction(FunctionPointer fn) {
        static_assert(std::is_pointer<FunctionPointer>() && std::is_function<std::remove_pointer_t<FunctionPointer>>(),
                      "Supplied type must be a pointer to a function");

        const u64 address  = reinterpret_cast<u64>(fn);
        const u64 distance = address - (getCurr<u64>() + 5);

        // Potential SSE-AVX transition.
        MaybeVZEROUPPER();

        if (distance >= 0x0000000080000000ULL && distance < 0xFFFFFFFF80000000ULL) {
            // Far call
            mov(rax, address);
            call(rax);
        } else {
            call(fn);
        }
    }

    Xbyak::Address MConst(u64 constant);

    /// Far code sits far away from the near code. Execution remains primarily in near code.
    /// "Cold" / Rarely executed instructions sit in far code, so the CPU doesn't fetch them unless necessary.
    void SwitchToFarCode();
    void SwitchToNearCode();

    const void* GetReturnFromRunCodeAddress() const {
        return return_from_run_code[0];
    }

    const void* GetForceReturnFromRunCodeAddress() const {
        return return_from_run_code[FORCE_RETURN];
    }

    const void* GetMemoryReadCallback(size_t bit_size) const {
        switch (bit_size) {
        case 8:
            return read_memory_8;
        case 16:
            return read_memory_16;
        case 32:
            return read_memory_32;
        case 64:
            return read_memory_64;
        default:
            return nullptr;
        }
    }

    const void* GetMemoryWriteCallback(size_t bit_size) const {
        switch (bit_size) {
        case 8:
            return write_memory_8;
        case 16:
            return write_memory_16;
        case 32:
            return write_memory_32;
        case 64:
            return write_memory_64;
        default:
            return nullptr;
        }
    }

    void int3() { db(0xCC); }

    /// Allocate memory of `size` bytes from the same block of memory the code is in.
    /// This is useful for objects that need to be placed close to or within code.
    /// The lifetime of this memory is the same as the code around it.
    void* AllocateFromCodeSpace(size_t size);

    void SetCodePtr(CodePtr code_ptr);
    void EnsurePatchLocationSize(CodePtr begin, size_t size);

#ifdef _WIN32
    Xbyak::Reg64 ABI_RETURN = rax;
    Xbyak::Reg64 ABI_PARAM1 = rcx;
    Xbyak::Reg64 ABI_PARAM2 = rdx;
    Xbyak::Reg64 ABI_PARAM3 = r8;
    Xbyak::Reg64 ABI_PARAM4 = r9;
#else
    Xbyak::Reg64 ABI_RETURN = rax;
    Xbyak::Reg64 ABI_PARAM1 = rdi;
    Xbyak::Reg64 ABI_PARAM2 = rsi;
    Xbyak::Reg64 ABI_PARAM3 = rdx;
    Xbyak::Reg64 ABI_PARAM4 = rcx;
#endif

    const Xbyak::util::Cpu cpu_info;

private:
    UserCallbacks cb;
    LookupBlockCallback lookup_block;
    void* lookup_block_arg;

    CodePtr near_code_begin;
    CodePtr far_code_begin;

    ConstantPool constant_pool;

    bool in_far_code = false;
    CodePtr near_code_ptr;
    CodePtr far_code_ptr;

    using RunCodeFuncType = void(*)(JitState*);
    RunCodeFuncType run_code = nullptr;
    static constexpr size_t NO_SWITCH_MXCSR = 1 << 0;
    static constexpr size_t FORCE_RETURN = 1 << 1;
    std::array<const void*, 4> return_from_run_code;
    void GenRunCode();

    const void* read_memory_8 = nullptr;
    const void* read_memory_16 = nullptr;
    const void* read_memory_32 = nullptr;
    const void* read_memory_64 = nullptr;
    const void* write_memory_8 = nullptr;
    const void* write_memory_16 = nullptr;
    const void* write_memory_32 = nullptr;
    const void* write_memory_64 = nullptr;
    void GenMemoryAccessors();

    class ExceptionHandler final {
    public:
        ExceptionHandler();
        ~ExceptionHandler();

        void Register(BlockOfCode* code);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
    ExceptionHandler exception_handler;
};

} // namespace BackendX64
} // namespace Dynarmic
