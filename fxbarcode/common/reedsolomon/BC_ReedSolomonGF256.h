// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef FXBARCODE_COMMON_REEDSOLOMON_BC_REEDSOLOMONGF256_H_
#define FXBARCODE_COMMON_REEDSOLOMON_BC_REEDSOLOMONGF256_H_

#include <stdint.h>

#include <array>
#include <memory>
#include <optional>

class CBC_ReedSolomonGF256Poly;

class CBC_ReedSolomonGF256 {
 public:
  explicit CBC_ReedSolomonGF256(int32_t primitive);
  ~CBC_ReedSolomonGF256();

  CBC_ReedSolomonGF256Poly* GetZero() const { return zero_.get(); }
  CBC_ReedSolomonGF256Poly* GetOne() const { return one_.get(); }

  std::unique_ptr<CBC_ReedSolomonGF256Poly> BuildMonomial(int32_t degree,
                                                          int32_t coefficient);
  static int32_t AddOrSubtract(int32_t a, int32_t b);
  int32_t Exp(int32_t a);
  std::optional<int32_t> Inverse(int32_t a);
  int32_t Multiply(int32_t a, int32_t b);
  void Init();

 private:
  std::unique_ptr<CBC_ReedSolomonGF256Poly> zero_;
  std::unique_ptr<CBC_ReedSolomonGF256Poly> one_;
  std::array<int32_t, 256> exp_table_;
  std::array<int32_t, 256> log_table_;
};

#endif  // FXBARCODE_COMMON_REEDSOLOMON_BC_REEDSOLOMONGF256_H_
