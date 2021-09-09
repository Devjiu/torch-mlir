//===- VerifyInvariantsBeforeBackendLowering.cpp -----------------*- C++-*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "npcomp/Dialect/TorchConversion/IR/TorchConversionOps.h"
#include "npcomp/Dialect/TorchConversion/Transforms/Passes.h"
#include "torch-mlir/Dialect/Torch/IR/TorchOps.h"

using namespace mlir;
using namespace mlir::NPCOMP;
using namespace mlir::NPCOMP::TorchConversion;
using namespace mlir::torch;

static LogicalResult checkValueInvariants(Operation *errorReportOp, Value v) {
  // TODO: Make this an allowlist instead of a denylist.
  // TODO: Make this stricter.
  auto type = v.getType();
  if (auto valueTensorType = type.dyn_cast<Torch::ValueTensorType>()) {
    if (!valueTensorType.hasDtype() || !valueTensorType.hasSizes())
      return errorReportOp->emitError()
          .append("unsupported by backend lowering: tensor with unknown rank "
                  "or dtype")
          .attachNote()
          .append("this is likely due to a missing case in RefineTypes");
  }
  return success();
}

namespace {

class VerifyInvariantsBeforeBackendLoweringPass
    : public VerifyInvariantsBeforeBackendLoweringBase<
          VerifyInvariantsBeforeBackendLoweringPass> {
  void runOnOperation() override {
    // TODO: It seems that the walkers over blocks are not correctly
    // propagating `walkResult.wasInterrupted()` so use a manual `didFail`
    // boolean.
    bool didFail = false;
    getOperation().walk([&](Block *block) {
      // Check invariants on all the Value's in the program.
      // That is, check all BlockArgument's and OpResult's.
      for (BlockArgument arg : block->getArguments()) {
        if (failed(checkValueInvariants(block->getParentOp(), arg))) {
          didFail = true;
          return WalkResult::interrupt();
        }
      }
      for (Operation &op : *block) {
        if (isa<Torch::OperatorOp>(op)) {
          op.emitError()
              .append("unsupported by backend lowering: `torch.operator` op")
              .attachNote()
              .append("this is likely due to a missing op that needs to be "
                      "generated by torch_ods_gen.py");
          didFail = true;
          return WalkResult::interrupt();
        }
        for (OpResult result : op.getResults()) {
          if (failed(checkValueInvariants(&op, result))) {
            didFail = true;
            return WalkResult::interrupt();
          }
        }
      }
      return WalkResult::advance();
    });
    if (didFail)
      return signalPassFailure();
  }
};

} // namespace

std::unique_ptr<OperationPass<ModuleOp>> mlir::NPCOMP::TorchConversion::
    createVerifyInvariantsBeforeBackendLoweringPass() {
  return std::make_unique<VerifyInvariantsBeforeBackendLoweringPass>();
}
