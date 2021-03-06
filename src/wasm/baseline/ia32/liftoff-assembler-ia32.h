// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_IA32_H_
#define V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_IA32_H_

#include "src/wasm/baseline/liftoff-assembler.h"

#include "src/assembler.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace liftoff {

inline Operand GetStackSlot(uint32_t index) {
  // ebp-8 holds the stack marker, ebp-16 is the wasm context, first stack slot
  // is located at ebp-24.
  constexpr int32_t kStackSlotSize = 8;
  constexpr int32_t kFirstStackSlotOffset = -24;
  return Operand(ebp, kFirstStackSlotOffset - index * kStackSlotSize);
}

// TODO(clemensh): Make this a constexpr variable once Operand is constexpr.
inline Operand GetContextOperand() { return Operand(ebp, -16); }

}  // namespace liftoff

void LiftoffAssembler::ReserveStackSpace(uint32_t space) {
  stack_space_ = space;
  sub(esp, Immediate(space));
}

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value) {
  switch (value.type()) {
    case kWasmI32:
      if (value.to_i32() == 0) {
        xor_(reg.gp(), reg.gp());
      } else {
        mov(reg.gp(), Immediate(value.to_i32()));
      }
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::LoadFromContext(Register dst, uint32_t offset,
                                       int size) {
  DCHECK_LE(offset, kMaxInt);
  mov(dst, liftoff::GetContextOperand());
  DCHECK_EQ(4, size);
  mov(dst, Operand(dst, offset));
}

void LiftoffAssembler::SpillContext(Register context) {
  mov(liftoff::GetContextOperand(), context);
}

void LiftoffAssembler::Load(LiftoffRegister dst, Register src_addr,
                            uint32_t offset_imm, int size,
                            LiftoffRegList pinned) {
  Operand src_op = Operand(src_addr, offset_imm);
  if (offset_imm > kMaxInt) {
    // The immediate can not be encoded in the operand. Load it to a register
    // first.
    Register src = GetUnusedRegister(kGpReg, pinned).gp();
    mov(src, Immediate(offset_imm));
    src_op = Operand(src_addr, src, times_1, 0);
  }
  DCHECK_EQ(4, size);
  mov(dst.gp(), src_op);
}

void LiftoffAssembler::Store(Register dst_addr, uint32_t offset_imm,
                             LiftoffRegister src, int size,
                             LiftoffRegList pinned) {
  Operand dst_op = Operand(dst_addr, offset_imm);
  if (offset_imm > kMaxInt) {
    // The immediate can not be encoded in the operand. Load it to a register
    // first.
    Register dst = GetUnusedRegister(kGpReg, pinned).gp();
    mov(dst, Immediate(offset_imm));
    dst_op = Operand(dst_addr, dst, times_1, 0);
  }
  DCHECK_EQ(4, size);
  mov(dst_op, src.gp());
}

void LiftoffAssembler::LoadCallerFrameSlot(LiftoffRegister dst,
                                           uint32_t caller_slot_idx) {
  constexpr int32_t kCallerStackSlotSize = 4;
  mov(dst.gp(), Operand(ebp, kCallerStackSlotSize * (caller_slot_idx + 1)));
}

void LiftoffAssembler::MoveStackValue(uint32_t dst_index, uint32_t src_index) {
  DCHECK_NE(dst_index, src_index);
  if (cache_state_.has_unused_register(kGpReg)) {
    LiftoffRegister reg = GetUnusedRegister(kGpReg);
    Fill(reg, src_index);
    Spill(dst_index, reg);
  } else {
    push(liftoff::GetStackSlot(src_index));
    pop(liftoff::GetStackSlot(dst_index));
  }
}

void LiftoffAssembler::MoveToReturnRegister(LiftoffRegister reg) {
  if (reg.gp() != eax) mov(eax, reg.gp());
}

void LiftoffAssembler::Move(LiftoffRegister dst, LiftoffRegister src) {
  // The caller should check that the registers are not equal. For most
  // occurences, this is already guaranteed, so no need to check within this
  // method.
  DCHECK_NE(dst, src);
  DCHECK_EQ(dst.reg_class(), src.reg_class());
  // TODO(clemensh): Handle different sizes here.
  if (dst.is_gp()) {
    mov(dst.gp(), src.gp());
  } else {
    movsd(dst.fp(), src.fp());
  }
}

void LiftoffAssembler::Spill(uint32_t index, LiftoffRegister reg) {
  mov(liftoff::GetStackSlot(index), reg.gp());
}

void LiftoffAssembler::Spill(uint32_t index, WasmValue value) {
  // TODO(clemensh): Handle different types here.
  mov(liftoff::GetStackSlot(index), Immediate(value.to_i32()));
}

void LiftoffAssembler::Fill(LiftoffRegister reg, uint32_t index) {
  mov(reg.gp(), liftoff::GetStackSlot(index));
}

void LiftoffAssembler::emit_i32_add(Register dst, Register lhs, Register rhs) {
  if (lhs != dst) {
    lea(dst, Operand(lhs, rhs, times_1, 0));
  } else {
    add(dst, rhs);
  }
}

void LiftoffAssembler::emit_i32_sub(Register dst, Register lhs, Register rhs) {
  if (dst == rhs) {
    neg(dst);
    add(dst, lhs);
  } else {
    if (dst != lhs) mov(dst, lhs);
    sub(dst, rhs);
  }
}

#define COMMUTATIVE_I32_BINOP(name, instruction)                     \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register lhs, \
                                         Register rhs) {             \
    if (dst == rhs) {                                                \
      instruction(dst, lhs);                                         \
    } else {                                                         \
      if (dst != lhs) mov(dst, lhs);                                 \
      instruction(dst, rhs);                                         \
    }                                                                \
  }

// clang-format off
COMMUTATIVE_I32_BINOP(mul, imul)
COMMUTATIVE_I32_BINOP(and, and_)
COMMUTATIVE_I32_BINOP(or, or_)
COMMUTATIVE_I32_BINOP(xor, xor_)
// clang-format on

#undef DEFAULT_I32_BINOP

void LiftoffAssembler::JumpIfZero(Register reg, Label* label) {
  test(reg, reg);
  j(zero, label);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_IA32_H_
