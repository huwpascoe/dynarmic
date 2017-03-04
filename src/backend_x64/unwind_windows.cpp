/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstring>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "backend_x64/block_of_code.h"
#include "common/assert.h"
#include "common/common_types.h"
#include <boost/optional/optional.hpp>

using UBYTE = u8;

enum UNWIND_REGISTER_CODES {
    UWRC_RAX,
    UWRC_RCX,
    UWRC_RDX,
    UWRC_RBX,
    UWRC_RSP,
    UWRC_RBP,
    UWRC_RSI,
    UWRC_RDI,
    UWRC_R8,
    UWRC_R9,
    UWRC_R10,
    UWRC_R11,
    UWRC_R12,
    UWRC_R13,
    UWRC_R14,
    UWRC_R15,
};

enum UNWIND_OPCODE {
    UWOP_PUSH_NONVOL = 0,
    UWOP_ALLOC_LARGE = 1,
    UWOP_ALLOC_SMALL = 2,
    UWOP_SET_FPREG = 3,
    UWOP_SAVE_NONVOL = 4,
    UWOP_SAVE_NONVOL_FAR = 5,
    UWOP_SAVE_XMM128 = 8,
    UWOP_SAVE_XMM128_FAR = 9,
    UWOP_PUSH_MACHFRAME = 10,
};

union UNWIND_CODE {
    struct {
        UBYTE CodeOffset;
        UBYTE UnwindOp : 4;
        UBYTE OpInfo : 4;
    } code;
    USHORT FrameOffset;
};

// UNWIND_INFO is a tail-padded structure
struct UNWIND_INFO {
    UBYTE Version : 3;
    UBYTE Flags : 5;
    UBYTE SizeOfProlog;
    UBYTE CountOfCodes;
    UBYTE FrameRegister : 4;
    UBYTE FrameOffset : 4;
    // UNWIND_CODE UnwindCode[];
    // OPTIONAL UNW_EXCEPTION_INFO ExceptionInfo;
};

struct UNW_EXCEPTION_INFO {
    ULONG ExceptionHandler;
    // OPTIONAL ARBITRARY HandlerData;
};

namespace Dynarmic {
namespace BackendX64 {

struct PrologueInformation {
    std::vector<UNWIND_CODE> unwind_code;
    size_t number_of_unwind_code_entries;
    u8 prolog_size;
};

static PrologueInformation GetPrologueInformation() {
    PrologueInformation ret;

    const auto next_entry = [&]() -> UNWIND_CODE& {
        ret.unwind_code.emplace_back();
        return ret.unwind_code.back();
    };
    const auto push_nonvol = [&](u8 offset, UNWIND_REGISTER_CODES reg) {
        auto& entry = next_entry();
        entry.code.CodeOffset = offset;
        entry.code.UnwindOp = UWOP_PUSH_NONVOL;
        entry.code.OpInfo = reg;
    };
    const auto alloc_large = [&](u8 offset, size_t size) {
        ASSERT(size % 8 == 0);
        size /= 8;

        auto& entry = next_entry();
        entry.code.CodeOffset = offset;
        entry.code.UnwindOp = UWOP_ALLOC_LARGE;
        if (size <= 0xFFFF) {
            entry.code.OpInfo = 0;
            auto& size_entry = next_entry();
            size_entry.FrameOffset = static_cast<USHORT>(size);
        } else {
            entry.code.OpInfo = 1;
            auto& size_entry_1 = next_entry();
            size_entry_1.FrameOffset = static_cast<USHORT>(size);
            auto& size_entry_2 = next_entry();
            size_entry_2.FrameOffset = static_cast<USHORT>(size >> 16);
        }
    };
    const auto save_xmm128 = [&](u8 offset, u8 reg, size_t frame_offset) {
        ASSERT(frame_offset % 16 == 0);

        auto& entry = next_entry();
        entry.code.CodeOffset = offset;
        entry.code.UnwindOp = UWOP_SAVE_XMM128;
        entry.code.OpInfo = reg;
        auto& offset_entry = next_entry();
        offset_entry.FrameOffset = static_cast<USHORT>(frame_offset / 16);
    };

    // This is a list of operations that occur in the prologue.
    // The debugger uses this information to retrieve register values and
    // to calculate the size of the stack frame.
    ret.prolog_size = 89;
    save_xmm128(89, 15, 0xB0);  // +050  44 0F 29 BC 24 B0 00 00 00  movaps  xmmword ptr [rsp+0B0h],xmm15
    save_xmm128(80, 14, 0xA0);  // +047  44 0F 29 B4 24 A0 00 00 00  movaps  xmmword ptr [rsp+0A0h],xmm14
    save_xmm128(71, 13, 0x90);  // +03E  44 0F 29 AC 24 90 00 00 00  movaps  xmmword ptr [rsp+90h],xmm13
    save_xmm128(62, 12, 0x80);  // +035  44 0F 29 A4 24 80 00 00 00  movaps  xmmword ptr [rsp+80h],xmm12
    save_xmm128(53, 11, 0x70);  // +02F  44 0F 29 5C 24 70           movaps  xmmword ptr [rsp+70h],xmm11
    save_xmm128(47, 10, 0x60);  // +029  44 0F 29 54 24 60           movaps  xmmword ptr [rsp+60h],xmm10
    save_xmm128(41, 9, 0x50);   // +023  44 0F 29 4C 24 50           movaps  xmmword ptr [rsp+50h],xmm9
    save_xmm128(35, 8, 0x40);   // +01D  44 0F 29 44 24 40           movaps  xmmword ptr [rsp+40h],xmm8
    save_xmm128(29, 7, 0x30);   // +018  0F 29 7C 24 30              movaps  xmmword ptr [rsp+30h],xmm7
    save_xmm128(24, 6, 0x20);   // +013  0F 29 74 24 20              movaps  xmmword ptr [rsp+20h],xmm6
    alloc_large(19, 0xC8);      // +00C  48 81 EC C8 00 00 00        sub     rsp,0C8h
    push_nonvol(12, UWRC_R15);  // +00A  41 57                       push    r15
    push_nonvol(10, UWRC_R14);  // +008  41 56                       push    r14
    push_nonvol(8, UWRC_R13);   // +006  41 55                       push    r13
    push_nonvol(6, UWRC_R12);   // +004  41 54                       push    r12
    push_nonvol(4, UWRC_RBP);   // +003  55                          push    rbp
    push_nonvol(3, UWRC_RDI);   // +002  57                          push    rdi
    push_nonvol(2, UWRC_RSI);   // +001  56                          push    rsi
    push_nonvol(1, UWRC_RBX);   // +000  53                          push    rbx

    ret.number_of_unwind_code_entries = ret.unwind_code.size();

    // The Windows API requires the size of the unwind_code array
    // to be a multiple of two for alignment reasons.
    if (ret.unwind_code.size() % 2 == 1) {
        auto& last_entry = next_entry();
        last_entry.FrameOffset = 0;
    }
    ASSERT(ret.unwind_code.size() % 2 == 0);

    return ret;
}

struct EmulatedMemoryAccess {
    bool is_write;
    size_t bit_size;
    size_t vaddr_x64_register;
    size_t value_x64_register;
    const u8* after_instruction;
};

static boost::optional<EmulatedMemoryAccess> ParseX64MovInstruction(const u8* code) {
    // We're only interested in a small number of mov/movzx instructions:
    // * 0x66 is the only legacy prefix and only appears at most once.
    // * REX prefix may or may not appear.
    // * Only [sib] addressing is used.
    // * Both the base and index registers are required.
    // * Scale must be 1.
    // If any of the above are violated, this function returns boost::none.
    //
    // An additonal unverified assumption is also made:
    // * Displacement is assumed to be zero.

    bool opsize_prefix = false;
    if (*code == 0x66) {
        opsize_prefix = true;
        code++;
    }

    bool rex_w = false;
    bool rex_r = false;
    bool rex_x = false;
    bool rex_b = false;
    if ((*code & 0xF0) == 0x40) {
        rex_w = (*code & 0b1000) != 0;
        rex_r = (*code & 0b0100) != 0;
        rex_x = (*code & 0b0010) != 0;
        rex_b = (*code & 0b0001) != 0;
        code++;
    }

    EmulatedMemoryAccess ret;

    // Supported instructions:
    // mov r/m8, r8
    // mov r/m16, r16
    // mov r/m32, r32
    // mov r/m64, r64
    // movzx r32, r/m8
    // movzx r32, r/m16
    // mov r32, r/m32
    // mov r64, r/m64
    switch (*code) {
    case 0x88:
        ret.is_write = true;
        ret.bit_size = 8;
        break;
    case 0x89:
        ret.is_write = true;
        ret.bit_size = opsize_prefix ? 16 : (!rex_w ? 32 : 64);
        break;
    case 0x8B:
        if (opsize_prefix) {
            // mov r16, r/m16 not supported
            return {};
        }
        ret.is_write = false;
        ret.bit_size = !rex_w ? 32 : 64;
        break;
    case 0x0F:
        code++;
        switch (*code) {
        case 0xB6:
            ret.is_write = false;
            ret.bit_size = 8;
            break;
        case 0xB7:
            ret.is_write = false;
            ret.bit_size = 16;
            break;
        default:
            return {}; // Unsupported opcode
        }
        break;
    default:
        return {}; // Unsupported opcode
    }
    code++;

    u8 modrm = *code;
    u8 modrm_mod = (modrm & 0b11000000) >> 6;
    u8 modrm_reg = (modrm & 0b00111000) >> 3;
    u8 modrm_rm = (modrm & 0b00000111);
    code++;
    if (modrm_rm != 0b100 || modrm_mod == 0b11) {
        // Only [sib] addressing supported
        return {};
    }
    ret.value_x64_register = modrm_reg + (rex_r ? 8 : 0);

    u8 sib = *code;
    u8 sib_scale = (sib & 0b11000000) >> 6;
    u8 sib_index = (sib & 0b00111000) >> 3;
    u8 sib_base = (sib & 0b00000111);
    code++;
    if (sib_scale != 0b00) {
        // Only scale == 1 is supported
        return {};
    }
    if (modrm_mod == 0b00 && sib_base == 0b101) {
        // Base register is required
        return {};
    }
    if (!rex_x && sib_index == 0b100) {
        // Index register is required
        return {};
    }
    size_t index = sib_index + (rex_x ? 8 : 0);
    size_t base = sib_base + (rex_b ? 8 : 0);
    if (base == 14) {
        ret.vaddr_x64_register = index;
    } else if (index == 14) {
        ret.vaddr_x64_register = base;
    } else {
        // We only support [r14 + vaddr_reg] or [vaddr_reg + r14]
        return {};
    }

    // HACK: We assume displacement == 0 if there is any.
    switch (modrm_mod) {
    case 0b01:
        code += 1;
        break;
    case 0b10:
        code += 4;
        break;
    }

    ret.after_instruction = code;
    return ret;
}

static u64& GetRegister(PCONTEXT ContextRecord, size_t reg_id) {
    switch (reg_id) {
    case 0:
        return ContextRecord->Rax;
    case 1:
        return ContextRecord->Rcx;
    case 2:
        return ContextRecord->Rdx;
    case 3:
        return ContextRecord->Rbx;
    case 4:
        return ContextRecord->Rsp;
    case 5:
        return ContextRecord->Rbp;
    case 6:
        return ContextRecord->Rsi;
    case 7:
        return ContextRecord->Rdi;
    case 8:
        return ContextRecord->R8;
    case 9:
        return ContextRecord->R9;
    case 10:
        return ContextRecord->R10;
    case 11:
        return ContextRecord->R11;
    case 12:
        return ContextRecord->R12;
    case 13:
        return ContextRecord->R13;
    case 14:
        return ContextRecord->R14;
    case 15:
        return ContextRecord->R15;
    default:
        std::terminate();
    }
}

// https://msdn.microsoft.com/en-us/library/b6sf5kbd.aspx
static EXCEPTION_DISPOSITION ExceptionHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    ULONG64 EstablisherFrame,
    PCONTEXT ContextRecord,
    PDISPATCHER_CONTEXT DispatcherContext
) {
    UNUSED(ExceptionRecord, EstablisherFrame);

    const u8* code = reinterpret_cast<u8*>(ContextRecord->Rip);
    auto mov_inst = ParseX64MovInstruction(code);
    if (!mov_inst) {
        printf("Could not parse mov!\n");
        return ExceptionContinueSearch;
    }

    // printf("direction = %s\n", mem_access->is_write ? "write" : "read");
    // printf("bit_size  = %zu\n", mem_access->bit_size);
    // printf("vaddr_reg = %zu\n", mem_access->vaddr_x64_register);
    // printf("value_reg = %zu\n", mem_access->value_x64_register);

    const UserCallbacks* cb = reinterpret_cast<UserCallbacks*>(DispatcherContext->HandlerData);
    if (mov_inst->is_write) {
        u64 src = GetRegister(ContextRecord, mov_inst->value_x64_register);
        u32 vaddr = static_cast<u32>(GetRegister(ContextRecord, mov_inst->vaddr_x64_register));
        switch (mov_inst->bit_size) {
        case 8:
            cb->memory.Write8(vaddr, static_cast<u8>(src));
            break;
        case 16:
            cb->memory.Write16(vaddr, static_cast<u16>(src));
            break;
        case 32:
            cb->memory.Write32(vaddr, static_cast<u32>(src));
            break;
        case 64:
            cb->memory.Write64(vaddr, src);
            break;
        default:
            return ExceptionContinueSearch;
        }
    } else {
        u64& dest = GetRegister(ContextRecord, mov_inst->value_x64_register);
        u32 vaddr = static_cast<u32>(GetRegister(ContextRecord, mov_inst->vaddr_x64_register));
        switch (mov_inst->bit_size) {
        case 8:
            dest = cb->memory.Read8(vaddr);
            break;
        case 16:
            dest = cb->memory.Read16(vaddr);
            break;
        case 32:
            dest = cb->memory.Read32(vaddr);
            break;
        case 64:
            dest = cb->memory.Read64(vaddr);
            break;
        default:
            return ExceptionContinueSearch;
        }
    }

    ContextRecord->Rip = reinterpret_cast<DWORD64>(mov_inst->after_instruction);
    return ExceptionContinueExecution;
}

static const u8* EmitExceptionHandler(BlockOfCode* code) {
    code->align(16);
    const u8* except_handler = code->getCurr();
    code->mov(code->rax, reinterpret_cast<u64>(&ExceptionHandler));
    code->jmp(code->rax);
    printf("except_handler: %llx\n\n", reinterpret_cast<u64>(except_handler));
    return except_handler;
}

struct BlockOfCode::UnwindHandler::Impl final {
    Impl(RUNTIME_FUNCTION* rfuncs_, const u8* base_ptr) : rfuncs(rfuncs_) {
        RtlAddFunctionTable(rfuncs, 1, reinterpret_cast<DWORD64>(base_ptr));
    }

    ~Impl() {
        RtlDeleteFunctionTable(rfuncs);
    }

private:
    RUNTIME_FUNCTION* rfuncs = nullptr;
};

BlockOfCode::UnwindHandler::UnwindHandler() = default;
BlockOfCode::UnwindHandler::~UnwindHandler() = default;

void BlockOfCode::UnwindHandler::Register(BlockOfCode* code, const UserCallbacks& cb) {
    const u8* except_handler = EmitExceptionHandler(code);
    const auto prolog_info = GetPrologueInformation();

    code->align(16);
    UNWIND_INFO* unwind_info = static_cast<UNWIND_INFO*>(code->AllocateFromCodeSpace(sizeof(UNWIND_INFO)));
    unwind_info->Version = 1;
    unwind_info->Flags = UNW_FLAG_EHANDLER;
    unwind_info->SizeOfProlog = prolog_info.prolog_size;
    unwind_info->CountOfCodes = static_cast<UBYTE>(prolog_info.number_of_unwind_code_entries);
    unwind_info->FrameRegister = 0; // No frame register present
    unwind_info->FrameOffset = 0; // Unused because FrameRegister == 0
    // UNWIND_INFO::UnwindCode field:
    const size_t size_of_unwind_code = sizeof(UNWIND_CODE) * prolog_info.unwind_code.size();
    UNWIND_CODE* unwind_code = static_cast<UNWIND_CODE*>(code->AllocateFromCodeSpace(size_of_unwind_code));
    memcpy(unwind_code, prolog_info.unwind_code.data(), size_of_unwind_code);
    // UNWIND_INFO::ExceptionInfo field:
    UNW_EXCEPTION_INFO* except_info = static_cast<UNW_EXCEPTION_INFO*>(code->AllocateFromCodeSpace(sizeof(UNW_EXCEPTION_INFO)));
    except_info->ExceptionHandler = static_cast<ULONG>(except_handler - code->getCode());
    // UNW_EXCEPTION_INFO::HandlerData field:
    void* cb_copy = code->AllocateFromCodeSpace(sizeof(UserCallbacks));
    memcpy(cb_copy, &cb, sizeof(UserCallbacks));

    code->align(16);
    RUNTIME_FUNCTION* rfuncs = static_cast<RUNTIME_FUNCTION*>(code->AllocateFromCodeSpace(sizeof(RUNTIME_FUNCTION)));
    rfuncs->BeginAddress = static_cast<DWORD>(reinterpret_cast<u8*>(code->run_code) - code->getCode());
    rfuncs->EndAddress = static_cast<DWORD>(code->maxSize_);
    rfuncs->UnwindData = static_cast<DWORD>(reinterpret_cast<u8*>(unwind_info) - code->getCode());

    impl = std::make_unique<Impl>(rfuncs, code->getCode());
}

} // namespace BackendX64
} // namespace Dynarmic
