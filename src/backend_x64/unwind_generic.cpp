/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstdint>
#include <vector>

#include "backend_x64/block_of_code.h"

// The below requires the use of a custom GDB JIT Debug Info reader.
// More information: https://sourceware.org/gdb/download/onlinedocs/gdb/Using-JIT-Debug-Info-Readers.html

extern "C" {

typedef enum {
    JIT_NOACTION = 0,
    JIT_REGISTER,
    JIT_UNREGISTER
} jit_actions_t;

struct jit_code_entry {
    jit_code_entry* next_entry;
    jit_code_entry* prev_entry;
    void *symfile_addr;
    std::uint64_t symfile_size;
};

struct jit_descriptor {
    std::uint32_t version;
    std::uint32_t action_flag;
    jit_code_entry* relevant_entry;
    jit_code_entry* first_entry;
};

void __attribute__((noinline)) __jit_debug_register_code () {
    __asm__ __volatile__ ("" ::: "memory");
}

jit_descriptor __jit_debug_descriptor = { 1, 0, 0, 0 };

} // extern "C"

namespace Dynarmic {
namespace BackendX64 {

namespace GdbProtocol {
struct SymFile {
    u64 size = sizeof(SymFile);
    // Version information for this struct
    // Update if there is an incompatible change to this struct
    u64 version = 1;

    void *list_pointer = nullptr;
    u64 list_size = 0;

    u64 user_code_begin_offset = 0;
    u64 start_of_code_block_offset = 0;
    u64 total_size_offset = 0;
};
} // namespace GdbProtocol

static jit_code_entry only_code_entry;
static GdbProtocol::SymFile only_sym_file;
static std::vector<BlockOfCode*> block_of_code_list;
static bool already_registered = false;

static void UpdateSymFile() {
    only_sym_file.list_pointer = block_of_code_list.data();
    only_sym_file.list_size = block_of_code_list.size();
}

struct BlockOfCode::UnwindHandler::Impl final {
    explicit Impl(BlockOfCode* code_) : code(code_) {
        block_of_code_list.emplace_back(code);
        UpdateSymFile();
    }

    ~Impl() {
        auto& l = block_of_code_list;
        l.erase(std::remove(l.begin(), l.end(), code), l.end());
        UpdateSymFile();
    }

    BlockOfCode* code;
};

BlockOfCode::UnwindHandler::UnwindHandler() = default;
BlockOfCode::UnwindHandler::~UnwindHandler() = default;

void BlockOfCode::UnwindHandler::Register(BlockOfCode* code) {
    impl = std::make_unique<Impl>(code);

    if (!already_registered) {
        only_code_entry.next_entry = nullptr;
        only_code_entry.prev_entry = nullptr;
        only_code_entry.symfile_addr = &only_sym_file;
        only_code_entry.symfile_size = sizeof(only_sym_file);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#elif defined(__GNUC__)
#pragma gcc diagnostic push
#pragma gcc diagnostic ignored "-Winvalid-offsetof"
#endif
        only_sym_file.user_code_begin_offset = offsetof(BlockOfCode, user_code_begin);
        only_sym_file.start_of_code_block_offset = offsetof(BlockOfCode, top_);
        only_sym_file.total_size_offset = offsetof(BlockOfCode, maxSize_);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma gcc diagnostic pop
#endif

        __jit_debug_descriptor.action_flag = JIT_REGISTER;
        __jit_debug_descriptor.version = 1;
        __jit_debug_descriptor.first_entry = &only_code_entry;
        __jit_debug_descriptor.relevant_entry = &only_code_entry;

        __jit_debug_register_code();
        already_registered = true;
    }
}

} // namespace BackendX64
} // namespace Dynarmic
