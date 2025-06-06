// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/cxfa_ffarc.h"

#include "xfa/fxfa/parser/cxfa_arc.h"
#include "xfa/fxfa/parser/cxfa_value.h"

CXFA_FFArc::CXFA_FFArc(CXFA_Node* pNode) : CXFA_FFWidget(pNode) {}

CXFA_FFArc::~CXFA_FFArc() = default;

void CXFA_FFArc::RenderWidget(CFGAS_GEGraphics* pGS,
                              const CFX_Matrix& matrix,
                              HighlightOption highlight) {
  if (!HasVisibleStatus()) {
    return;
  }

  CXFA_Value* value = node_->GetFormValueIfExists();
  if (!value) {
    return;
  }

  CFX_RectF rtArc = GetRectWithoutRotate();
  CXFA_Margin* margin = node_->GetMarginIfExists();
  XFA_RectWithoutMargin(&rtArc, margin);

  CFX_Matrix mtRotate = GetRotateMatrix();
  mtRotate.Concat(matrix);
  DrawBorder(pGS, value->GetArcIfExists(), rtArc, mtRotate);
}
