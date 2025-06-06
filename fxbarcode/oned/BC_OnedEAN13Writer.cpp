// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com
// Original code is licensed as follows:
/*
 * Copyright 2009 ZXing authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fxbarcode/oned/BC_OnedEAN13Writer.h"

#include <math.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#include "core/fxcrt/fx_extension.h"
#include "core/fxge/cfx_defaultrenderdevice.h"
#include "core/fxge/text_char_pos.h"
#include "fxbarcode/BC_Writer.h"
#include "fxbarcode/oned/BC_OneDimWriter.h"
#include "fxbarcode/oned/BC_OnedEANChecksum.h"

namespace {

constexpr std::array<const int8_t, 10> kFirstDigitEncodings = {
    {0x00, 0x0B, 0x0D, 0xE, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A}};

const uint8_t kOnedEAN13StartPattern[3] = {1, 1, 1};
const uint8_t kOnedEAN13MiddlePattern[5] = {1, 1, 1, 1, 1};

using LPatternRow = std::array<uint8_t, 4>;
constexpr std::array<const LPatternRow, 10> kOnedEAN13LPatternTable = {
    {{3, 2, 1, 1},
     {2, 2, 2, 1},
     {2, 1, 2, 2},
     {1, 4, 1, 1},
     {1, 1, 3, 2},
     {1, 2, 3, 1},
     {1, 1, 1, 4},
     {1, 3, 1, 2},
     {1, 2, 1, 3},
     {3, 1, 1, 2}}};

using LGPatternRow = std::array<uint8_t, 4>;
constexpr std::array<const LGPatternRow, 20> kOnedEAN13LGPatternTable = {
    {{3, 2, 1, 1}, {2, 2, 2, 1}, {2, 1, 2, 2}, {1, 4, 1, 1}, {1, 1, 3, 2},
     {1, 2, 3, 1}, {1, 1, 1, 4}, {1, 3, 1, 2}, {1, 2, 1, 3}, {3, 1, 1, 2},
     {1, 1, 2, 3}, {1, 2, 2, 2}, {2, 2, 1, 2}, {1, 1, 4, 1}, {2, 3, 1, 1},
     {1, 3, 2, 1}, {4, 1, 1, 1}, {2, 1, 3, 1}, {3, 1, 2, 1}, {2, 1, 1, 3}}};

}  // namespace

CBC_OnedEAN13Writer::CBC_OnedEAN13Writer() {
  left_padding_ = true;
  code_width_ = 3 + (7 * 6) + 5 + (7 * 6) + 3;
}
CBC_OnedEAN13Writer::~CBC_OnedEAN13Writer() = default;

bool CBC_OnedEAN13Writer::CheckContentValidity(WideStringView contents) {
  return HasValidContentSize(contents) &&
         std::all_of(contents.begin(), contents.end(),
                     [](wchar_t c) { return FXSYS_IsDecimalDigit(c); });
}

WideString CBC_OnedEAN13Writer::FilterContents(WideStringView contents) {
  WideString filtercontents;
  filtercontents.Reserve(contents.GetLength());
  wchar_t ch;
  for (size_t i = 0; i < contents.GetLength(); i++) {
    ch = contents[i];
    if (ch > 175) {
      i++;
      continue;
    }
    if (FXSYS_IsDecimalDigit(ch)) {
      filtercontents += ch;
    }
  }
  return filtercontents;
}

int32_t CBC_OnedEAN13Writer::CalcChecksum(const ByteString& contents) {
  return EANCalcChecksum(contents);
}

DataVector<uint8_t> CBC_OnedEAN13Writer::Encode(const ByteString& contents) {
  if (contents.GetLength() != 13) {
    return DataVector<uint8_t>();
  }

  data_length_ = 13;
  int32_t firstDigit = FXSYS_DecimalCharToInt(contents.Front());
  int32_t parities = kFirstDigitEncodings[firstDigit];
  DataVector<uint8_t> result(code_width_);
  auto result_span = pdfium::span(result);
  result_span = AppendPattern(result_span, kOnedEAN13StartPattern, true);

  for (int i = 1; i <= 6; i++) {
    int32_t digit = FXSYS_DecimalCharToInt(contents[i]);
    if ((parities >> (6 - i) & 1) == 1) {
      digit += 10;
    }
    result_span =
        AppendPattern(result_span, kOnedEAN13LGPatternTable[digit], false);
  }
  result_span = AppendPattern(result_span, kOnedEAN13MiddlePattern, false);

  for (int i = 7; i <= 12; i++) {
    int32_t digit = FXSYS_DecimalCharToInt(contents[i]);
    result_span =
        AppendPattern(result_span, kOnedEAN13LPatternTable[digit], true);
  }
  AppendPattern(result_span, kOnedEAN13StartPattern, true);
  return result;
}

bool CBC_OnedEAN13Writer::ShowChars(WideStringView contents,
                                    CFX_RenderDevice* device,
                                    const CFX_Matrix& matrix,
                                    int32_t barWidth) {
  if (!device) {
    return false;
  }

  static constexpr float kLeftPosition = 10.0f;
  ByteString str = FX_UTF8Encode(contents);
  size_t length = str.GetLength();
  std::vector<TextCharPos> charpos(length);
  int32_t iFontSize = static_cast<int32_t>(fabs(font_size_));
  int32_t iTextHeight = iFontSize + 1;
  ByteString tempStr = str.Substr(1, 6);
  static constexpr int32_t kWidth = 42;

  CFX_Matrix matr(output_hscale_, 0.0, 0.0, 1.0, 0.0, 0.0);
  CFX_FloatRect rect(kLeftPosition, (float)(height_ - iTextHeight),
                     kLeftPosition + kWidth - 0.5, (float)height_);
  matr.Concat(matrix);
  FX_RECT re = matr.TransformRect(rect).GetOuterRect();
  device->FillRect(re, kBackgroundColor);
  CFX_FloatRect rect1(kLeftPosition + 47, (float)(height_ - iTextHeight),
                      kLeftPosition + 47 + kWidth - 0.5, (float)height_);
  CFX_Matrix matr1(output_hscale_, 0.0, 0.0, 1.0, 0.0, 0.0);
  matr1.Concat(matrix);
  re = matr1.TransformRect(rect1).GetOuterRect();
  device->FillRect(re, kBackgroundColor);
  CFX_Matrix matr2(output_hscale_, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
  CFX_FloatRect rect2(0.0f, (float)(height_ - iTextHeight), 6.5f,
                      (float)height_);
  matr2.Concat(matrix);
  re = matr2.TransformRect(rect2).GetOuterRect();
  device->FillRect(re, kBackgroundColor);

  float blank = 0.0f;
  length = tempStr.GetLength();
  int32_t strWidth = static_cast<int32_t>(kWidth * output_hscale_);

  pdfium::span<TextCharPos> charpos_span = pdfium::span(charpos);
  CalcTextInfo(tempStr, charpos_span.subspan<1u>(), font_, (float)strWidth,
               iFontSize, blank);
  {
    CFX_Matrix affine_matrix1(1.0, 0.0, 0.0, -1.0,
                              kLeftPosition * output_hscale_,
                              (float)(height_ - iTextHeight) + iFontSize);
    affine_matrix1.Concat(matrix);
    device->DrawNormalText(charpos_span.subspan(1u, length), font_,
                           static_cast<float>(iFontSize), affine_matrix1,
                           font_color_, GetTextRenderOptions());
  }
  tempStr = str.Substr(7, 6);
  length = tempStr.GetLength();
  CalcTextInfo(tempStr, charpos_span.subspan<7u>(), font_, (float)strWidth,
               iFontSize, blank);
  {
    CFX_Matrix affine_matrix1(1.0, 0.0, 0.0, -1.0,
                              (kLeftPosition + 47) * output_hscale_,
                              (float)(height_ - iTextHeight + iFontSize));
    affine_matrix1.Concat(matrix);
    device->DrawNormalText(charpos_span.subspan(7u, length), font_,
                           static_cast<float>(iFontSize), affine_matrix1,
                           font_color_, GetTextRenderOptions());
  }
  tempStr = str.First(1);
  length = tempStr.GetLength();
  strWidth = 7 * static_cast<int32_t>(strWidth * output_hscale_);

  CalcTextInfo(tempStr, charpos, font_, (float)strWidth, iFontSize, blank);
  {
    CFX_Matrix affine_matrix1(1.0, 0.0, 0.0, -1.0, 0.0,
                              (float)(height_ - iTextHeight + iFontSize));
    affine_matrix1.Concat(matrix);
    device->DrawNormalText(charpos_span.first(length), font_,
                           static_cast<float>(iFontSize), affine_matrix1,
                           font_color_, GetTextRenderOptions());
  }
  return true;
}
