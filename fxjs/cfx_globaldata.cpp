// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "fxjs/cfx_globaldata.h"

#include <utility>

#include "core/fdrm/fx_crypt.h"
#include "core/fxcrt/fx_memcpy_wrappers.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include "core/fxcrt/stl_util.h"

namespace {

constexpr size_t kMinGlobalDataBytes = 12;
constexpr size_t kMaxGlobalDataBytes = 4 * 1024 - 8;
constexpr uint16_t kMagic = ('X' << 8) | 'F';
constexpr uint16_t kMaxVersion = 2;

const uint8_t kRC4KEY[] = {
    0x19, 0xa8, 0xe8, 0x01, 0xf6, 0xa8, 0xb6, 0x4d, 0x82, 0x04, 0x45, 0x6d,
    0xb4, 0xcf, 0xd7, 0x77, 0x67, 0xf9, 0x75, 0x9f, 0xf0, 0xe0, 0x1e, 0x51,
    0xee, 0x46, 0xfd, 0x0b, 0xc9, 0x93, 0x25, 0x55, 0x4a, 0xee, 0xe0, 0x16,
    0xd0, 0xdf, 0x8c, 0xfa, 0x2a, 0xa9, 0x49, 0xfd, 0x97, 0x1c, 0x0e, 0x22,
    0x13, 0x28, 0x7c, 0xaf, 0xc4, 0xfc, 0x9c, 0x12, 0x65, 0x8c, 0x4e, 0x5b,
    0x04, 0x75, 0x89, 0xc9, 0xb1, 0xed, 0x50, 0xca, 0x96, 0x6f, 0x1a, 0x7a,
    0xfe, 0x58, 0x5d, 0xec, 0x19, 0x4a, 0xf6, 0x35, 0x6a, 0x97, 0x14, 0x00,
    0x0e, 0xd0, 0x6b, 0xbb, 0xd5, 0x75, 0x55, 0x8b, 0x6e, 0x6b, 0x19, 0xa0,
    0xf8, 0x77, 0xd5, 0xa3};

CFX_GlobalData* g_pInstance = nullptr;

// Returns true if non-empty, setting sPropName
bool TrimPropName(ByteString* sPropName) {
  sPropName->TrimWhitespace();
  return sPropName->GetLength() != 0;
}

void MakeNameTypeString(const ByteString& name,
                        CFX_Value::DataType eType,
                        BinaryBuffer* result) {
  uint32_t dwNameLen = pdfium::checked_cast<uint32_t>(name.GetLength());
  result->AppendUint32(dwNameLen);
  result->AppendString(name);
  result->AppendUint16(static_cast<uint16_t>(eType));
}

bool MakeByteString(const ByteString& name,
                    const CFX_KeyValue& pData,
                    BinaryBuffer* result) {
  switch (pData.nType) {
    case CFX_Value::DataType::kNumber: {
      MakeNameTypeString(name, pData.nType, result);
      result->AppendDouble(pData.dData);
      return true;
    }
    case CFX_Value::DataType::kBoolean: {
      MakeNameTypeString(name, pData.nType, result);
      result->AppendUint16(static_cast<uint16_t>(pData.bData));
      return true;
    }
    case CFX_Value::DataType::kString: {
      MakeNameTypeString(name, pData.nType, result);
      uint32_t dwDataLen =
          pdfium::checked_cast<uint32_t>(pData.sData.GetLength());
      result->AppendUint32(dwDataLen);
      result->AppendString(pData.sData);
      return true;
    }
    case CFX_Value::DataType::kNull: {
      MakeNameTypeString(name, pData.nType, result);
      return true;
    }
    // Arrays don't get persisted per JS spec page 484.
    case CFX_Value::DataType::kObject:
      break;
  }
  return false;
}

}  // namespace

// static
CFX_GlobalData* CFX_GlobalData::GetRetainedInstance(Delegate* pDelegate) {
  if (!g_pInstance) {
    g_pInstance = new CFX_GlobalData(pDelegate);
  }
  ++g_pInstance->ref_count_;
  return g_pInstance;
}

bool CFX_GlobalData::Release() {
  if (--ref_count_) {
    return false;
  }

  delete g_pInstance;
  g_pInstance = nullptr;
  return true;
}

CFX_GlobalData::CFX_GlobalData(Delegate* pDelegate) : delegate_(pDelegate) {
  LoadGlobalPersistentVariables();
}

CFX_GlobalData::~CFX_GlobalData() {
  SaveGlobalPersisitentVariables();
}

CFX_GlobalData::iterator CFX_GlobalData::FindGlobalVariable(
    const ByteString& propname) {
  for (auto it = array_global_data_.begin(); it != array_global_data_.end();
       ++it) {
    if ((*it)->data.sKey == propname) {
      return it;
    }
  }
  return array_global_data_.end();
}

CFX_GlobalData::Element* CFX_GlobalData::GetGlobalVariable(
    const ByteString& propname) {
  auto iter = FindGlobalVariable(propname);
  return iter != array_global_data_.end() ? iter->get() : nullptr;
}

void CFX_GlobalData::SetGlobalVariableNumber(ByteString sPropName,
                                             double dData) {
  if (!TrimPropName(&sPropName)) {
    return;
  }

  CFX_GlobalData::Element* pData = GetGlobalVariable(sPropName);
  if (pData) {
    pData->data.nType = CFX_Value::DataType::kNumber;
    pData->data.dData = dData;
    return;
  }
  auto pNewData = std::make_unique<CFX_GlobalData::Element>();
  pNewData->data.sKey = std::move(sPropName);
  pNewData->data.nType = CFX_Value::DataType::kNumber;
  pNewData->data.dData = dData;
  array_global_data_.push_back(std::move(pNewData));
}

void CFX_GlobalData::SetGlobalVariableBoolean(ByteString sPropName,
                                              bool bData) {
  if (!TrimPropName(&sPropName)) {
    return;
  }

  CFX_GlobalData::Element* pData = GetGlobalVariable(sPropName);
  if (pData) {
    pData->data.nType = CFX_Value::DataType::kBoolean;
    pData->data.bData = bData;
    return;
  }
  auto pNewData = std::make_unique<CFX_GlobalData::Element>();
  pNewData->data.sKey = std::move(sPropName);
  pNewData->data.nType = CFX_Value::DataType::kBoolean;
  pNewData->data.bData = bData;
  array_global_data_.push_back(std::move(pNewData));
}

void CFX_GlobalData::SetGlobalVariableString(ByteString sPropName,
                                             const ByteString& sData) {
  if (!TrimPropName(&sPropName)) {
    return;
  }

  CFX_GlobalData::Element* pData = GetGlobalVariable(sPropName);
  if (pData) {
    pData->data.nType = CFX_Value::DataType::kString;
    pData->data.sData = sData;
    return;
  }
  auto pNewData = std::make_unique<CFX_GlobalData::Element>();
  pNewData->data.sKey = std::move(sPropName);
  pNewData->data.nType = CFX_Value::DataType::kString;
  pNewData->data.sData = sData;
  array_global_data_.push_back(std::move(pNewData));
}

void CFX_GlobalData::SetGlobalVariableObject(
    ByteString sPropName,
    std::vector<std::unique_ptr<CFX_KeyValue>> array) {
  if (!TrimPropName(&sPropName)) {
    return;
  }

  CFX_GlobalData::Element* pData = GetGlobalVariable(sPropName);
  if (pData) {
    pData->data.nType = CFX_Value::DataType::kObject;
    pData->data.objData = std::move(array);
    return;
  }
  auto pNewData = std::make_unique<CFX_GlobalData::Element>();
  pNewData->data.sKey = std::move(sPropName);
  pNewData->data.nType = CFX_Value::DataType::kObject;
  pNewData->data.objData = std::move(array);
  array_global_data_.push_back(std::move(pNewData));
}

void CFX_GlobalData::SetGlobalVariableNull(ByteString sPropName) {
  if (!TrimPropName(&sPropName)) {
    return;
  }

  CFX_GlobalData::Element* pData = GetGlobalVariable(sPropName);
  if (pData) {
    pData->data.nType = CFX_Value::DataType::kNull;
    return;
  }
  auto pNewData = std::make_unique<CFX_GlobalData::Element>();
  pNewData->data.sKey = std::move(sPropName);
  pNewData->data.nType = CFX_Value::DataType::kNull;
  array_global_data_.push_back(std::move(pNewData));
}

bool CFX_GlobalData::SetGlobalVariablePersistent(ByteString sPropName,
                                                 bool bPersistent) {
  if (!TrimPropName(&sPropName)) {
    return false;
  }

  CFX_GlobalData::Element* pData = GetGlobalVariable(sPropName);
  if (!pData) {
    return false;
  }

  pData->bPersistent = bPersistent;
  return true;
}

bool CFX_GlobalData::DeleteGlobalVariable(ByteString sPropName) {
  if (!TrimPropName(&sPropName)) {
    return false;
  }

  auto iter = FindGlobalVariable(sPropName);
  if (iter == array_global_data_.end()) {
    return false;
  }

  array_global_data_.erase(iter);
  return true;
}

int32_t CFX_GlobalData::GetSize() const {
  return fxcrt::CollectionSize<int32_t>(array_global_data_);
}

CFX_GlobalData::Element* CFX_GlobalData::GetAt(int index) {
  if (index < 0 || index >= GetSize()) {
    return nullptr;
  }
  return array_global_data_[index].get();
}

bool CFX_GlobalData::LoadGlobalPersistentVariables() {
  if (!delegate_) {
    return false;
  }

  bool ret;
  {
    // Span can't outlive call to BufferDone().
    std::optional<pdfium::span<uint8_t>> buffer = delegate_->LoadBuffer();
    if (!buffer.has_value() || buffer.value().empty()) {
      return false;
    }

    ret = LoadGlobalPersistentVariablesFromBuffer(buffer.value());
  }
  delegate_->BufferDone();
  return ret;
}

bool CFX_GlobalData::LoadGlobalPersistentVariablesFromBuffer(
    pdfium::span<uint8_t> buffer) {
  if (buffer.size() < kMinGlobalDataBytes) {
    return false;
  }

  CRYPT_ArcFourCryptBlock(buffer, kRC4KEY);

  UNSAFE_TODO({
    uint8_t* p = buffer.data();
    uint16_t wType = *((uint16_t*)p);
    p += sizeof(uint16_t);
    if (wType != kMagic) {
      return false;
    }

    uint16_t wVersion = *((uint16_t*)p);
    p += sizeof(uint16_t);
    if (wVersion > kMaxVersion) {
      return false;
    }

    uint32_t dwCount = *((uint32_t*)p);
    p += sizeof(uint32_t);

    uint32_t dwSize = *((uint32_t*)p);
    p += sizeof(uint32_t);

    if (dwSize != buffer.size() - sizeof(uint16_t) * 2 - sizeof(uint32_t) * 2) {
      return false;
    }

    for (int32_t i = 0, sz = dwCount; i < sz; i++) {
      if (p + sizeof(uint32_t) >= buffer.end()) {
        break;
      }

      uint32_t dwNameLen = 0;
      FXSYS_memcpy(&dwNameLen, p, sizeof(uint32_t));
      p += sizeof(uint32_t);
      if (p + dwNameLen > buffer.end()) {
        break;
      }

      ByteString sEntry = ByteString(p, dwNameLen);
      p += sizeof(char) * dwNameLen;

      uint16_t wDataType = 0;
      FXSYS_memcpy(&wDataType, p, sizeof(uint16_t));
      p += sizeof(uint16_t);

      CFX_Value::DataType eDataType =
          static_cast<CFX_Value::DataType>(wDataType);

      switch (eDataType) {
        case CFX_Value::DataType::kNumber: {
          double dData = 0;
          switch (wVersion) {
            case 1: {
              uint32_t dwData = 0;
              FXSYS_memcpy(&dwData, p, sizeof(uint32_t));
              p += sizeof(uint32_t);
              dData = dwData;
            } break;
            case 2: {
              dData = 0;
              FXSYS_memcpy(&dData, p, sizeof(double));
              p += sizeof(double);
            } break;
          }
          SetGlobalVariableNumber(sEntry, dData);
          SetGlobalVariablePersistent(sEntry, true);
        } break;
        case CFX_Value::DataType::kBoolean: {
          uint16_t wData = 0;
          FXSYS_memcpy(&wData, p, sizeof(uint16_t));
          p += sizeof(uint16_t);
          SetGlobalVariableBoolean(sEntry, (bool)(wData == 1));
          SetGlobalVariablePersistent(sEntry, true);
        } break;
        case CFX_Value::DataType::kString: {
          uint32_t dwLength = 0;
          FXSYS_memcpy(&dwLength, p, sizeof(uint32_t));
          p += sizeof(uint32_t);
          if (p + dwLength > buffer.end()) {
            break;
          }
          SetGlobalVariableString(sEntry, ByteString(p, dwLength));
          SetGlobalVariablePersistent(sEntry, true);
          p += sizeof(char) * dwLength;
        } break;
        case CFX_Value::DataType::kNull: {
          SetGlobalVariableNull(sEntry);
          SetGlobalVariablePersistent(sEntry, true);
        } break;
        case CFX_Value::DataType::kObject:
          // Arrays aren't allowed in these buffers, nor are unrecognized tags.
          return false;
      }
    }
  });
  return true;
}

bool CFX_GlobalData::SaveGlobalPersisitentVariables() {
  if (!delegate_) {
    return false;
  }

  uint32_t nCount = 0;
  BinaryBuffer sData;
  for (const auto& pElement : array_global_data_) {
    if (!pElement->bPersistent) {
      continue;
    }

    BinaryBuffer sElement;
    if (!MakeByteString(pElement->data.sKey, pElement->data, &sElement)) {
      continue;
    }

    if (sData.GetSize() + sElement.GetSize() > kMaxGlobalDataBytes) {
      break;
    }

    sData.AppendSpan(sElement.GetSpan());
    nCount++;
  }

  BinaryBuffer sFile;
  sFile.AppendUint16(kMagic);
  sFile.AppendUint16(kMaxVersion);
  sFile.AppendUint32(nCount);

  uint32_t dwSize = pdfium::checked_cast<uint32_t>(sData.GetSize());
  sFile.AppendUint32(dwSize);
  sFile.AppendSpan(sData.GetSpan());

  CRYPT_ArcFourCryptBlock(sFile.GetMutableSpan(), kRC4KEY);
  return delegate_->StoreBuffer(sFile.GetSpan());
}

CFX_GlobalData::Element::Element() = default;

CFX_GlobalData::Element::~Element() = default;
