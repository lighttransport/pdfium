// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfapi/parser/cpdf_encryptor.h"

#include <stdint.h>

#include "core/fpdfapi/parser/cpdf_crypto_handler.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/data_vector.h"

CPDF_Encryptor::CPDF_Encryptor(const CPDF_CryptoHandler* pHandler, int objnum)
    : handler_(pHandler), obj_num_(objnum) {
  DCHECK(handler_);
}

DataVector<uint8_t> CPDF_Encryptor::Encrypt(
    pdfium::span<const uint8_t> src_data) const {
  if (src_data.empty()) {
    return DataVector<uint8_t>();
  }
  return handler_->EncryptContent(obj_num_, 0, src_data);
}

CPDF_Encryptor::~CPDF_Encryptor() = default;
