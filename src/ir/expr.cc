/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file src/ir/expr.cc
 * \brief The expression AST nodes for the common IR infra.
 */
#include <tvm/arith/analyzer.h>
#include <tvm/ffi/function.h>
#include <tvm/ffi/reflection/registry.h>
#include <tvm/ir/expr.h>
#include <tvm/ir/function.h>
#include <tvm/te/tensor.h>
#include <tvm/tir/expr.h>

#include "../support/scalars.h"

namespace tvm {

TVM_FFI_STATIC_INIT_BLOCK({
  BaseExprNode::RegisterReflection();
  PrimExprNode::RegisterReflection();
  RelaxExprNode::RegisterReflection();
  BaseFuncNode::RegisterReflection();
  GlobalVarNode::RegisterReflection();
  IntImmNode::RegisterReflection();
  FloatImmNode::RegisterReflection();
  RangeNode::RegisterReflection();
});

PrimExpr::PrimExpr(int32_t value) : PrimExpr(IntImm(DataType::Int(32), value)) {}

PrimExpr::PrimExpr(float value) : PrimExpr(FloatImm(DataType::Float(32), value)) {}

PrimExpr PrimExpr::ConvertFallbackValue(String value) { return tir::StringImm(value); }

IntImm::IntImm(DataType dtype, int64_t value, Span span) {
  ICHECK(dtype.is_scalar()) << "ValueError: IntImm can only take scalar, but " << dtype
                            << " was supplied.";
  ICHECK(dtype.is_int() || dtype.is_uint())
      << "ValueError: IntImm supports only int or uint type, but " << dtype << " was supplied.";
  if (dtype.is_uint()) {
    ICHECK_GE(value, 0U) << "ValueError: Literal value " << value
                         << " is negative for unsigned integer type " << dtype;
    if (dtype.bits() < 64) {
      ICHECK_LT(value, 1LL << dtype.bits())
          << "ValueError: Literal value " << value << " exceeds maximum of " << dtype;
    }
  } else if (dtype.bits() == 1) {
    // int(1)
    ICHECK(value == 0 || value == 1) << "ValueError: " << value << " exceeds range of " << dtype;
  } else if (dtype.bits() < 64) {
    ICHECK_GE(value, -(1LL << (dtype.bits() - 1)))
        << "ValueError: Literal value " << value << " exceeds minimum of " << dtype;
    ICHECK_LT(value, 1LL << (dtype.bits() - 1))
        << "ValueError: Literal value " << value << " exceeds maximum of " << dtype;
  }
  ObjectPtr<IntImmNode> node = make_object<IntImmNode>();
  node->dtype = dtype;
  node->value = value;
  node->span = span;
  data_ = std::move(node);
}

TVM_FFI_STATIC_INIT_BLOCK({
  namespace refl = tvm::ffi::reflection;
  refl::GlobalDef().def("ir.IntImm", [](DataType dtype, int64_t value, Span span) {
    return IntImm(dtype, value, span);
  });
});

TVM_REGISTER_NODE_TYPE(IntImmNode);

FloatImm::FloatImm(DataType dtype, double value, Span span) {
  ICHECK_EQ(dtype.lanes(), 1) << "ValueError: FloatImm can only take scalar.";

  ICHECK(dtype.is_float() || dtype.is_bfloat16() || dtype.is_float8() || dtype.is_float6() ||
         dtype.is_float4() || dtype.code() >= DataType::kCustomBegin)
      << "ValueError: FloatImm supports only float, but " << dtype << " was supplied.";

  // check range for float32 and float16 since they have specified range.
  if (!std::isinf(value) && !std::isnan(value)) {
    if (dtype.bits() == 32) {
      ICHECK_GE(value, std::numeric_limits<float>::lowest())
          << "ValueError: Literal value " << value << " exceeds minimum of " << dtype;
      ICHECK_LE(value, std::numeric_limits<float>::max())
          << "ValueError: Literal value " << value << " exceeds maximum of " << dtype;
    } else if (dtype.is_float16()) {
      ICHECK_GE(value, -support::kMaxFloat16)
          << "ValueError: Literal value " << value << " exceeds minimum of " << dtype;
      ICHECK_LE(value, support::kMaxFloat16)
          << "ValueError: Literal value " << value << " exceeds maximum of " << dtype;
    } else if (dtype.is_bfloat16()) {
      ICHECK_GE(value, -support::kMaxBFloat16)
          << "ValueError: Literal value " << value << " exceeds minimum of " << dtype;
      ICHECK_LE(value, support::kMaxBFloat16)
          << "ValueError: Literal value " << value << " exceeds maximum of " << dtype;
    } else if (dtype.is_float8_e3m4() || dtype.is_float8_e4m3() || dtype.is_float8_e4m3b11fnuz() ||
               dtype.is_float8_e4m3fn() || dtype.is_float8_e4m3fnuz() || dtype.is_float8_e5m2() ||
               dtype.is_float8_e5m2fnuz() || dtype.is_float8_e8m0fnu()) {
      double bound = 0.0;
      bool nonneg = false;

      switch (dtype.code()) {
        case DataType::TypeCode::kFloat8_e3m4:
          bound = support::kMaxE3M4;
          break;
        case DataType::TypeCode::kFloat8_e4m3:
          bound = support::kMaxE4M3;
          break;
        case DataType::TypeCode::kFloat8_e4m3b11fnuz:
          bound = support::kMaxE4M3B11FNUZ;
          nonneg = true;
          break;
        case DataType::TypeCode::kFloat8_e4m3fn:
          bound = support::kMaxE4M3FN;
          break;
        case DataType::TypeCode::kFloat8_e4m3fnuz:
          bound = support::kMaxE4M3FNUZ;
          nonneg = true;
          break;
        case DataType::TypeCode::kFloat8_e5m2:
          bound = support::kMaxE5M2;
          break;
        case DataType::TypeCode::kFloat8_e5m2fnuz:
          bound = support::kMaxE5M2FNUZ;
          nonneg = true;
          break;
        case DataType::TypeCode::kFloat8_e8m0fnu:
          bound = support::kMaxE8M0FNU;
          nonneg = true;
          break;
        default:
          LOG(FATAL) << "Unhandled float8 type: " << dtype;
      }

      if (nonneg) {
        ICHECK_GE(value, 0) << "ValueError: Literal value " << value << " below zero for unsigned "
                            << dtype;
      } else {
        ICHECK_GE(value, -bound) << "ValueError: Literal value " << value << " below minimum of "
                                 << dtype;
      }
      ICHECK_LE(value, bound) << "ValueError: Literal value " << value << " exceeds maximum of "
                              << dtype;

    } else if (dtype.is_float6_e2m3fn() || dtype.is_float6_e3m2fn()) {
      double bound = (dtype.code() == DataType::TypeCode::kFloat6_e2m3fn) ? support::kMaxE2M3FN
                                                                          : support::kMaxE3M2FN;
      ICHECK_GE(value, -bound) << "ValueError: Literal value " << value << " below minimum of "
                               << dtype;
      ICHECK_LE(value, bound) << "ValueError: Literal value " << value << " exceeds maximum of "
                              << dtype;

    } else if (dtype.is_float4_e2m1fn()) {
      double bound = support::kMaxE2M1FN;
      ICHECK_GE(value, -bound) << "ValueError: Literal value " << value << " below minimum of "
                               << dtype;
      ICHECK_LE(value, bound) << "ValueError: Literal value " << value << " exceeds maximum of "
                              << dtype;
    }
  }
  ObjectPtr<FloatImmNode> node = make_object<FloatImmNode>();
  node->dtype = dtype;
  node->value = value;
  node->span = span;
  data_ = std::move(node);
}

TVM_FFI_STATIC_INIT_BLOCK({
  namespace refl = tvm::ffi::reflection;
  refl::GlobalDef().def("ir.FloatImm", [](DataType dtype, double value, Span span) {
    return FloatImm(dtype, value, span);
  });
});

TVM_REGISTER_NODE_TYPE(FloatImmNode);

Range::Range(PrimExpr begin, PrimExpr end, Span span)
    : Range(make_object<RangeNode>(begin, tir::is_zero(begin) ? end : (end - begin), span)) {}

Range Range::FromMinExtent(PrimExpr min, PrimExpr extent, Span span) {
  return Range(make_object<RangeNode>(min, extent, span));
}

TVM_FFI_STATIC_INIT_BLOCK({
  namespace refl = tvm::ffi::reflection;
  refl::GlobalDef()
      .def("ir.Range_from_min_extent", Range::FromMinExtent)
      .def("ir.Range", [](PrimExpr begin, Optional<PrimExpr> end, Span span) -> Range {
        if (end.defined()) {
          return Range(begin, end.value(), span);
        } else {
          return Range(IntImm(begin->dtype, 0), begin, span);
        }
      });
});

TVM_REGISTER_NODE_TYPE(RangeNode);

GlobalVar::GlobalVar(String name_hint, Span span) {
  ObjectPtr<GlobalVarNode> n = make_object<GlobalVarNode>();
  n->name_hint = std::move(name_hint);
  n->span = std::move(span);
  data_ = std::move(n);
}

TVM_REGISTER_NODE_TYPE(GlobalVarNode);

TVM_FFI_STATIC_INIT_BLOCK({
  namespace refl = tvm::ffi::reflection;
  refl::GlobalDef()
      .def("ir.GlobalVar", [](String name) { return GlobalVar(name); })
      .def("ir.DebugPrint", [](ObjectRef ref) {
        std::stringstream ss;
        ss << ref;
        return ss.str();
      });
});

}  // namespace tvm
