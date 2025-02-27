/*
$info$
tags: backend|arm64
$end_info$
*/

#include "Interface/Core/ArchHelpers/CodeEmitter/Emitter.h"
#include "Interface/Core/JIT/Arm64/JITClass.h"

namespace FEXCore::CPU {
#define DEF_OP(x) void Arm64JITCore::Op_##x(IR::IROp_Header const *IROp, IR::NodeID Node)
DEF_OP(VInsGPR) {
  const auto Op = IROp->C<IR::IROp_VInsGPR>();
  const auto OpSize = IROp->Size;

  const auto DestIdx = Op->DestIdx;
  const auto ElementSize = Op->Header.ElementSize;
  const auto Is256Bit = OpSize == Core::CPUState::XMM_AVX_REG_SIZE;

  LOGMAN_THROW_AA_FMT(ElementSize == 8 || ElementSize == 4 || ElementSize == 2 || ElementSize == 1, "Unexpected {} size", __func__);
  const auto SubEmitSize = ElementSize == 8 ? ARMEmitter::SubRegSize::i64Bit :
    ElementSize == 4 ? ARMEmitter::SubRegSize::i32Bit :
    ElementSize == 2 ? ARMEmitter::SubRegSize::i16Bit :
    ElementSize == 1 ? ARMEmitter::SubRegSize::i8Bit : ARMEmitter::SubRegSize::i8Bit;
  const auto ElementsPer128Bit = 16 / ElementSize;

  const auto Dst = GetVReg(Node);
  const auto DestVector = GetVReg(Op->DestVector.ID());
  const auto Src = GetReg(Op->Src.ID());

  if (HostSupportsSVE && Is256Bit) {
    const auto ElementSizeBits = ElementSize * 8;
    const auto Offset = ElementSizeBits * DestIdx;

    const auto SSEBitSize = Core::CPUState::XMM_SSE_REG_SIZE * 8;
    const auto InUpperLane = Offset >= SSEBitSize;

    // This is going to be a little gross. Pls forgive me.
    // Since SVE has the whole vector length agnostic programming
    // thing going on, we can't exactly freely insert entries into
    // arbitrary locations in the vector.
    //
    // SVE *does* have INSR, however this only shifts the entire
    // vector to the left by an element size and inserts a value
    // at the beginning of the vector. Not *quite* what we need.
    // (though INSR *is* very useful for other things).
    //
    // The idea is (in the case of the upper lane), move the upper
    // lane down, insert into it and recombine with the lower lane.
    //
    // In the case of the lower lane, insert and then recombine with
    // the upper lane.

    if (InUpperLane) {
      // Move the upper lane down for the insertion.
      const auto CompactPred = ARMEmitter::PReg::p0;
      not_(CompactPred, PRED_TMP_32B.Zeroing(), PRED_TMP_16B);
      compact(ARMEmitter::SubRegSize::i64Bit, VTMP1.Z(), CompactPred, DestVector);
    }

    // Put data in place for destructive SPLICE below.
    mov(Dst.Z(), DestVector.Z());

    // Inserts the GPR value into the given V register.
    // Also automatically adjusts the index in the case of using the
    // moved upper lane.
    const auto Insert = [&](const FEXCore::ARMEmitter::VRegister& reg, int index) {
      if (InUpperLane) {
        index -= ElementsPer128Bit;
      }
      ins(SubEmitSize, reg, index, Src);
    };

    if (InUpperLane) {
      Insert(VTMP1, DestIdx);
      splice<ARMEmitter::OpType::Destructive>(ARMEmitter::SubRegSize::i64Bit, Dst.Z(), PRED_TMP_16B, Dst.Z(), VTMP1.Z());
    } else {
      Insert(Dst, DestIdx);
      splice<ARMEmitter::OpType::Destructive>(ARMEmitter::SubRegSize::i64Bit, Dst.Z(), PRED_TMP_16B, Dst.Z(), DestVector.Z());
    }
  } else {
    mov(Dst.Q(), DestVector.Q());
    ins(SubEmitSize, Dst, DestIdx, Src);
  }
}

DEF_OP(VCastFromGPR) {
  auto Op = IROp->C<IR::IROp_VCastFromGPR>();
  auto Dst = GetVReg(Node);
  auto Src = GetReg(Op->Src.ID());

  switch (Op->Header.ElementSize) {
    case 1:
      uxtb(ARMEmitter::Size::i32Bit, TMP1, Src);
      fmov(ARMEmitter::Size::i32Bit, Dst.S(), TMP1);
      break;
    case 2:
      uxth(ARMEmitter::Size::i32Bit, TMP1, Src);
      fmov(ARMEmitter::Size::i32Bit, Dst.S(), TMP1);
      break;
    case 4:
      fmov(ARMEmitter::Size::i32Bit, Dst.S(), Src);
      break;
    case 8:
      fmov(ARMEmitter::Size::i64Bit, Dst.D(), Src);
      break;
    default: LOGMAN_MSG_A_FMT("Unknown castGPR element size: {}", Op->Header.ElementSize);
  }
}

DEF_OP(Float_FromGPR_S) {
  const auto Op = IROp->C<IR::IROp_Float_FromGPR_S>();

  const uint16_t ElementSize = Op->Header.ElementSize;
  const uint16_t Conv = (ElementSize << 8) | Op->SrcElementSize;

  auto Dst = GetVReg(Node);
  auto Src = GetReg(Op->Src.ID());

  switch (Conv) {
    case 0x0404: { // Float <- int32_t
      scvtf(ARMEmitter::Size::i32Bit, Dst.S(), Src);
      break;
    }
    case 0x0408: { // Float <- int64_t
      scvtf(ARMEmitter::Size::i64Bit, Dst.S(), Src);
      break;
    }
    case 0x0804: { // Double <- int32_t
      scvtf(ARMEmitter::Size::i32Bit, Dst.D(), Src);
      break;
    }
    case 0x0808: { // Double <- int64_t
      scvtf(ARMEmitter::Size::i64Bit, Dst.D(), Src);
      break;
    }
    default:
      LOGMAN_MSG_A_FMT("Unhandled conversion mask: Mask=0x{:04x}, ElementSize={}, SrcElementSize={}",
                       Conv, ElementSize, Op->SrcElementSize);
      break;
  }
}

DEF_OP(Float_FToF) {
  auto Op = IROp->C<IR::IROp_Float_FToF>();
  const uint16_t Conv = (Op->Header.ElementSize << 8) | Op->SrcElementSize;

  auto Dst = GetVReg(Node);
  auto Src = GetVReg(Op->Scalar.ID());

  switch (Conv) {
    case 0x0804: { // Double <- Float
      fcvt(Dst.D(), Src.S());
      break;
    }
    case 0x0408: { // Float <- Double
      fcvt(Dst.S(), Src.D());
      break;
    }
    default: LOGMAN_MSG_A_FMT("Unknown FCVT sizes: 0x{:x}", Conv);
  }
}

DEF_OP(Vector_SToF) {
  const auto Op = IROp->C<IR::IROp_Vector_SToF>();
  const auto OpSize = IROp->Size;

  const auto ElementSize = Op->Header.ElementSize;
  const auto Is256Bit = OpSize == Core::CPUState::XMM_AVX_REG_SIZE;

  LOGMAN_THROW_AA_FMT(ElementSize == 8 || ElementSize == 4 || ElementSize == 2, "Unexpected {} size", __func__);
  const auto SubEmitSize = ElementSize == 8 ? ARMEmitter::SubRegSize::i64Bit :
    ElementSize == 4 ? ARMEmitter::SubRegSize::i32Bit :
    ElementSize == 2 ? ARMEmitter::SubRegSize::i16Bit : ARMEmitter::SubRegSize::i16Bit;

  const auto Dst = GetVReg(Node);
  const auto Vector = GetVReg(Op->Vector.ID());
  if (HostSupportsSVE && Is256Bit) {
    const auto Mask = PRED_TMP_32B;
    scvtf(Dst.Z(), SubEmitSize, Mask.Merging(), Vector.Z(), SubEmitSize);
  } else {
    scvtf(SubEmitSize, Dst.Q(), Vector.Q());
  }
}

DEF_OP(Vector_FToZS) {
  const auto Op = IROp->C<IR::IROp_Vector_FToZS>();
  const auto OpSize = IROp->Size;

  const auto ElementSize = Op->Header.ElementSize;
  const auto Is256Bit = OpSize == Core::CPUState::XMM_AVX_REG_SIZE;

  LOGMAN_THROW_AA_FMT(ElementSize == 8 || ElementSize == 4 || ElementSize == 2, "Unexpected {} size", __func__);
  const auto SubEmitSize = ElementSize == 8 ? ARMEmitter::SubRegSize::i64Bit :
    ElementSize == 4 ? ARMEmitter::SubRegSize::i32Bit :
    ElementSize == 2 ? ARMEmitter::SubRegSize::i16Bit : ARMEmitter::SubRegSize::i16Bit;

  const auto Dst = GetVReg(Node);
  const auto Vector = GetVReg(Op->Vector.ID());
  if (HostSupportsSVE && Is256Bit) {
    const auto Mask = PRED_TMP_32B;
    fcvtzs(Dst, SubEmitSize, Mask.Merging(), Vector, SubEmitSize);
  } else {
    fcvtzs(SubEmitSize, Dst.Q(), Vector.Q());
  }
}

DEF_OP(Vector_FToS) {
  const auto Op = IROp->C<IR::IROp_Vector_FToS>();
  const auto OpSize = IROp->Size;

  const auto ElementSize = Op->Header.ElementSize;
  const auto Is256Bit = OpSize == Core::CPUState::XMM_AVX_REG_SIZE;

  LOGMAN_THROW_AA_FMT(ElementSize == 8 || ElementSize == 4 || ElementSize == 2, "Unexpected {} size", __func__);
  const auto SubEmitSize = ElementSize == 8 ? ARMEmitter::SubRegSize::i64Bit :
    ElementSize == 4 ? ARMEmitter::SubRegSize::i32Bit :
    ElementSize == 2 ? ARMEmitter::SubRegSize::i16Bit : ARMEmitter::SubRegSize::i16Bit;

  const auto Dst = GetVReg(Node);
  const auto Vector = GetVReg(Op->Vector.ID());

  if (HostSupportsSVE && Is256Bit) {
    const auto Mask = PRED_TMP_32B;
    frinti(SubEmitSize, Dst, Mask.Merging(), Vector);
    fcvtzs(Dst, SubEmitSize, Mask.Merging(), Dst, SubEmitSize);
  } else {
    const auto Dst = GetVReg(Node);
    const auto Vector = GetVReg(Op->Vector.ID());
    frinti(SubEmitSize, Dst.Q(), Vector.Q());
    fcvtzs(SubEmitSize, Dst.Q(), Dst.Q());
  }
}

DEF_OP(Vector_FToF) {
  const auto Op = IROp->C<IR::IROp_Vector_FToF>();
  const auto OpSize = IROp->Size;

  const auto ElementSize = Op->Header.ElementSize;
  const auto Is256Bit = OpSize == Core::CPUState::XMM_AVX_REG_SIZE;
  const auto Conv = (ElementSize << 8) | Op->SrcElementSize;

  LOGMAN_THROW_AA_FMT(ElementSize == 8 || ElementSize == 4 || ElementSize == 2, "Unexpected {} size", __func__);
  const auto SubEmitSize = ElementSize == 8 ? ARMEmitter::SubRegSize::i64Bit :
    ElementSize == 4 ? ARMEmitter::SubRegSize::i32Bit :
    ElementSize == 2 ? ARMEmitter::SubRegSize::i16Bit : ARMEmitter::SubRegSize::i16Bit;

  const auto Dst = GetVReg(Node);
  const auto Vector = GetVReg(Op->Vector.ID());

  if (HostSupportsSVE && Is256Bit) {
    // Curiously, FCVTLT and FCVTNT have no bottom variants,
    // and also interesting is that FCVTLT will iterate the
    // source vector by accessing each odd element and storing
    // them consecutively in the destination.
    //
    // FCVTNT is somewhat like the opposite. It will read each
    // consecutive element, but store each result into every odd
    // element in the destination vector.
    //
    // We need to undo the behavior of FCVTNT with UZP2. In the case
    // of FCVTLT, we instead need to set the vector up with ZIP1, so
    // that the elements will be processed correctly.

    const auto Mask = PRED_TMP_32B.Merging();

    switch (Conv) {
      case 0x0402: { // Float <- Half
        zip1(FEXCore::ARMEmitter::SubRegSize::i16Bit, Dst.Z(), Vector.Z(), Vector.Z());
        fcvtlt(FEXCore::ARMEmitter::SubRegSize::i32Bit, Dst.Z(), Mask, Dst.Z());
        break;
      }
      case 0x0804: { // Double <- Float
        zip1(FEXCore::ARMEmitter::SubRegSize::i32Bit, Dst.Z(), Vector.Z(), Vector.Z());
        fcvtlt(FEXCore::ARMEmitter::SubRegSize::i64Bit, Dst.Z(), Mask, Dst.Z());
        break;
      }
      case 0x0204: { // Half <- Float
        fcvtnt(FEXCore::ARMEmitter::SubRegSize::i16Bit, Dst, Mask, Vector);
        uzp2(FEXCore::ARMEmitter::SubRegSize::i16Bit, Dst.Z(), Dst.Z(), Dst.Z());
        break;
      }
      case 0x0408: { // Float <- Double
        fcvtnt(FEXCore::ARMEmitter::SubRegSize::i32Bit, Dst, Mask, Vector);
        uzp2(FEXCore::ARMEmitter::SubRegSize::i32Bit, Dst.Z(), Dst.Z(), Dst.Z());
        break;
      }
      default:
        LOGMAN_MSG_A_FMT("Unknown Vector_FToF Type : 0x{:04x}", Conv);
        break;
    }
  } else {
    switch (Conv) {
      case 0x0402: // Float <- Half
      case 0x0804: { // Double <- Float
        fcvtl(SubEmitSize, Dst.D(), Vector.D());
        break;
      }
      case 0x0204: // Half <- Float
      case 0x0408: { // Float <- Double
        fcvtn(SubEmitSize, Dst.D(), Vector.D());
        break;
      }
      default:
        LOGMAN_MSG_A_FMT("Unknown Vector_FToF Type : 0x{:04x}", Conv);
        break;
    }
  }
}

DEF_OP(Vector_FToI) {
  const auto Op = IROp->C<IR::IROp_Vector_FToI>();
  const auto OpSize = IROp->Size;

  const auto ElementSize = Op->Header.ElementSize;
  const auto Is256Bit = OpSize == Core::CPUState::XMM_AVX_REG_SIZE;
  LOGMAN_THROW_AA_FMT(ElementSize == 8 || ElementSize == 4 || ElementSize == 2, "Unexpected {} size", __func__);

  const auto SubEmitSize = ElementSize == 8 ? ARMEmitter::SubRegSize::i64Bit :
    ElementSize == 4 ? ARMEmitter::SubRegSize::i32Bit :
    ElementSize == 2 ? ARMEmitter::SubRegSize::i16Bit : ARMEmitter::SubRegSize::i16Bit;

  const auto Dst = GetVReg(Node);
  const auto Vector = GetVReg(Op->Vector.ID());

  if (HostSupportsSVE && Is256Bit) {
    const auto Mask = PRED_TMP_32B.Merging();

    switch (Op->Round) {
      case FEXCore::IR::Round_Nearest.Val:
        frintn(SubEmitSize, Dst.Z(), Mask, Vector.Z());
        break;
      case FEXCore::IR::Round_Negative_Infinity.Val:
        frintm(SubEmitSize, Dst.Z(), Mask, Vector.Z());
        break;
      case FEXCore::IR::Round_Positive_Infinity.Val:
        frintp(SubEmitSize, Dst.Z(), Mask, Vector.Z());
        break;
      case FEXCore::IR::Round_Towards_Zero.Val:
        frintz(SubEmitSize, Dst.Z(), Mask, Vector.Z());
        break;
      case FEXCore::IR::Round_Host.Val:
        frinti(SubEmitSize, Dst.Z(), Mask, Vector.Z());
        break;
    }
  } else {
    switch (Op->Round) {
      case FEXCore::IR::Round_Nearest.Val:
        frinti(SubEmitSize, Dst.Q(), Vector.Q());
        break;
      case FEXCore::IR::Round_Negative_Infinity.Val:
        frintm(SubEmitSize, Dst.Q(), Vector.Q());
        break;
      case FEXCore::IR::Round_Positive_Infinity.Val:
        frintp(SubEmitSize, Dst.Q(), Vector.Q());
        break;
      case FEXCore::IR::Round_Towards_Zero.Val:
        frintz(SubEmitSize, Dst.Q(), Vector.Q());
        break;
      case FEXCore::IR::Round_Host.Val:
        frinti(SubEmitSize, Dst.Q(), Vector.Q());
        break;
    }
  }
}

#undef DEF_OP
}

