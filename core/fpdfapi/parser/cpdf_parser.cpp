// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfapi/parser/cpdf_parser.h"

#include <stdint.h>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_crypto_handler.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_linearized_header.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_object_stream.h"
#include "core/fpdfapi/parser/cpdf_read_validator.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_security_handler.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fpdfapi/parser/cpdf_syntax_parser.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"
#include "core/fxcrt/autorestorer.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/check_op.h"
#include "core/fxcrt/containers/contains.h"
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/fx_extension.h"
#include "core/fxcrt/fx_safe_types.h"
#include "core/fxcrt/fx_string.h"
#include "core/fxcrt/notreached.h"
#include "core/fxcrt/scoped_set_insertion.h"
#include "core/fxcrt/span.h"

using ObjectType = CPDF_CrossRefTable::ObjectType;
using ObjectInfo = CPDF_CrossRefTable::ObjectInfo;

namespace {

// A limit on the size of the xref table. Theoretical limits are higher, but
// this may be large enough in practice. The max size should always be 1 more
// than the max object number.
constexpr int32_t kMaxXRefSize = CPDF_Parser::kMaxObjectNumber + 1;

// "%PDF-1.7\n"
constexpr FX_FILESIZE kPDFHeaderSize = 9;

// The required number of fields in a /W array in a cross-reference stream
// dictionary.
constexpr size_t kMinFieldCount = 3;

// Trailers are inline.
constexpr uint32_t kNoTrailerObjectNumber = 0;

struct CrossRefStreamIndexEntry {
  uint32_t start_obj_num;
  uint32_t obj_count;
};

std::optional<ObjectType> GetObjectTypeFromCrossRefStreamType(
    uint32_t cross_ref_stream_type) {
  switch (cross_ref_stream_type) {
    case 0:
      return ObjectType::kFree;
    case 1:
      return ObjectType::kNormal;
    case 2:
      return ObjectType::kCompressed;
    default:
      return std::nullopt;
  }
}

// Use the Get*XRefStreamEntry() functions below, instead of calling this
// directly.
uint32_t GetVarInt(pdfium::span<const uint8_t> input) {
  uint32_t result = 0;
  for (uint8_t c : input) {
    result = result * 256 + c;
  }
  return result;
}

// The following 3 functions retrieve variable length entries from
// cross-reference streams, as described in ISO 32000-1:2008 table 18. There are
// only 3 fields for any given entry.
uint32_t GetFirstXRefStreamEntry(pdfium::span<const uint8_t> entry_span,
                                 pdfium::span<const uint32_t> field_widths) {
  return GetVarInt(entry_span.first(field_widths[0]));
}

uint32_t GetSecondXRefStreamEntry(pdfium::span<const uint8_t> entry_span,
                                  pdfium::span<const uint32_t> field_widths) {
  return GetVarInt(entry_span.subspan(field_widths[0], field_widths[1]));
}

uint32_t GetThirdXRefStreamEntry(pdfium::span<const uint8_t> entry_span,
                                 pdfium::span<const uint32_t> field_widths) {
  return GetVarInt(
      entry_span.subspan(field_widths[0] + field_widths[1], field_widths[2]));
}

std::vector<CrossRefStreamIndexEntry> GetCrossRefStreamIndices(
    const CPDF_Array* array,
    uint32_t size) {
  std::vector<CrossRefStreamIndexEntry> indices;
  if (array) {
    for (size_t i = 0; i < array->size() / 2; i++) {
      RetainPtr<const CPDF_Number> pStartNumObj = array->GetNumberAt(i * 2);
      if (!pStartNumObj) {
        continue;
      }

      RetainPtr<const CPDF_Number> pCountObj = array->GetNumberAt(i * 2 + 1);
      if (!pCountObj) {
        continue;
      }

      int nStartNum = pStartNumObj->GetInteger();
      int nCount = pCountObj->GetInteger();
      if (nStartNum < 0 || nCount <= 0) {
        continue;
      }

      indices.push_back(
          {static_cast<uint32_t>(nStartNum), static_cast<uint32_t>(nCount)});
    }
  }

  if (indices.empty()) {
    indices.push_back({0, size});
  }
  return indices;
}

std::vector<uint32_t> GetFieldWidths(const CPDF_Array* array) {
  std::vector<uint32_t> results;
  if (!array) {
    return results;
  }

  CPDF_ArrayLocker locker(array);
  for (const auto& obj : locker) {
    results.push_back(obj->GetInteger());
  }
  return results;
}

class ObjectsHolderStub final : public CPDF_Parser::ParsedObjectsHolder {
 public:
  ObjectsHolderStub() = default;
  ~ObjectsHolderStub() override = default;
  bool TryInit() override { return true; }
};

}  // namespace

CPDF_Parser::CPDF_Parser(ParsedObjectsHolder* holder)
    : objects_holder_(holder),
      cross_ref_table_(std::make_unique<CPDF_CrossRefTable>()) {
  if (!holder) {
    owned_objects_holder_ = std::make_unique<ObjectsHolderStub>();
    objects_holder_ = owned_objects_holder_.get();
  }
}

CPDF_Parser::CPDF_Parser() : CPDF_Parser(nullptr) {}

CPDF_Parser::~CPDF_Parser() = default;

uint32_t CPDF_Parser::GetLastObjNum() const {
  return cross_ref_table_->objects_info().empty()
             ? 0
             : cross_ref_table_->objects_info().rbegin()->first;
}

bool CPDF_Parser::IsValidObjectNumber(uint32_t objnum) const {
  return objnum <= GetLastObjNum();
}

FX_FILESIZE CPDF_Parser::GetObjectPositionOrZero(uint32_t objnum) const {
  const auto* info = cross_ref_table_->GetObjectInfo(objnum);
  return (info && info->type == ObjectType::kNormal) ? info->pos : 0;
}

bool CPDF_Parser::IsObjectFree(uint32_t objnum) const {
  DCHECK(IsValidObjectNumber(objnum));
  const auto* info = cross_ref_table_->GetObjectInfo(objnum);
  return !info || info->type == ObjectType::kFree;
}

bool CPDF_Parser::InitSyntaxParser(RetainPtr<CPDF_ReadValidator> validator) {
  const std::optional<FX_FILESIZE> header_offset = GetHeaderOffset(validator);
  if (!header_offset.has_value()) {
    return false;
  }
  if (validator->GetSize() < header_offset.value() + kPDFHeaderSize) {
    return false;
  }

  syntax_ = std::make_unique<CPDF_SyntaxParser>(std::move(validator),
                                                header_offset.value());
  return ParseFileVersion();
}

bool CPDF_Parser::ParseFileVersion() {
  file_version_ = 0;
  uint8_t ch;
  if (!syntax_->GetCharAt(5, ch)) {
    return false;
  }

  wchar_t wch = static_cast<wchar_t>(ch);
  if (FXSYS_IsDecimalDigit(wch)) {
    file_version_ = FXSYS_DecimalCharToInt(wch) * 10;
  }

  if (!syntax_->GetCharAt(7, ch)) {
    return false;
  }

  wch = static_cast<wchar_t>(ch);
  if (FXSYS_IsDecimalDigit(wch)) {
    file_version_ += FXSYS_DecimalCharToInt(wch);
  }
  return true;
}

CPDF_Parser::Error CPDF_Parser::StartParse(
    RetainPtr<IFX_SeekableReadStream> pFileAccess,
    const ByteString& password) {
  if (!InitSyntaxParser(pdfium::MakeRetain<CPDF_ReadValidator>(
          std::move(pFileAccess), nullptr))) {
    return FORMAT_ERROR;
  }
  SetPassword(password);
  return StartParseInternal();
}

CPDF_Parser::Error CPDF_Parser::StartParseInternal() {
  DCHECK(!has_parsed_);
  DCHECK(!xref_table_rebuilt_);
  has_parsed_ = true;
  xref_stream_ = false;

  last_xref_offset_ = ParseStartXRef();
  if (last_xref_offset_ >= kPDFHeaderSize) {
    if (!LoadAllCrossRefTablesAndStreams(last_xref_offset_)) {
      if (!RebuildCrossRef()) {
        return FORMAT_ERROR;
      }

      xref_table_rebuilt_ = true;
      last_xref_offset_ = 0;
    }
  } else {
    if (!RebuildCrossRef()) {
      return FORMAT_ERROR;
    }

    xref_table_rebuilt_ = true;
  }
  Error eRet = SetEncryptHandler();
  if (eRet != SUCCESS) {
    return eRet;
  }

  if (!GetRoot() || !objects_holder_->TryInit()) {
    if (xref_table_rebuilt_) {
      return FORMAT_ERROR;
    }

    ReleaseEncryptHandler();
    if (!RebuildCrossRef()) {
      return FORMAT_ERROR;
    }

    eRet = SetEncryptHandler();
    if (eRet != SUCCESS) {
      return eRet;
    }

    objects_holder_->TryInit();
    if (!GetRoot()) {
      return FORMAT_ERROR;
    }
  }
  if (GetRootObjNum() == CPDF_Object::kInvalidObjNum) {
    ReleaseEncryptHandler();
    if (!RebuildCrossRef() || GetRootObjNum() == CPDF_Object::kInvalidObjNum) {
      return FORMAT_ERROR;
    }

    eRet = SetEncryptHandler();
    if (eRet != SUCCESS) {
      return eRet;
    }
  }
  if (security_handler_ && !security_handler_->IsMetadataEncrypted()) {
    RetainPtr<const CPDF_Reference> pMetadata =
        ToReference(GetRoot()->GetObjectFor("Metadata"));
    if (pMetadata) {
      metadata_objnum_ = pMetadata->GetRefObjNum();
    }
  }
  return SUCCESS;
}

FX_FILESIZE CPDF_Parser::ParseStartXRef() {
  static constexpr auto kStartXRefKeyword =
      pdfium::span_from_cstring("startxref");
  syntax_->SetPos(syntax_->GetDocumentSize() - kStartXRefKeyword.size());
  if (!syntax_->BackwardsSearchToWord(ByteStringView(kStartXRefKeyword),
                                      4096)) {
    return 0;
  }

  // Skip "startxref" keyword.
  syntax_->GetKeyword();

  // Read XRef offset.
  const CPDF_SyntaxParser::WordResult xref_offset_result =
      syntax_->GetNextWord();
  if (!xref_offset_result.is_number || xref_offset_result.word.IsEmpty()) {
    return 0;
  }

  const FX_SAFE_FILESIZE result = FXSYS_atoi64(xref_offset_result.word.c_str());
  if (!result.IsValid() || result.ValueOrDie() >= syntax_->GetDocumentSize()) {
    return 0;
  }

  return result.ValueOrDie();
}

CPDF_Parser::Error CPDF_Parser::SetEncryptHandler() {
  ReleaseEncryptHandler();
  if (!GetTrailer()) {
    return FORMAT_ERROR;
  }

  RetainPtr<const CPDF_Dictionary> pEncryptDict = GetEncryptDict();
  if (!pEncryptDict) {
    return SUCCESS;
  }

  if (pEncryptDict->GetNameFor("Filter") != "Standard") {
    return HANDLER_ERROR;
  }

  auto pSecurityHandler = pdfium::MakeRetain<CPDF_SecurityHandler>();
  if (!pSecurityHandler->OnInit(pEncryptDict, GetIDArray(), GetPassword())) {
    return PASSWORD_ERROR;
  }

  security_handler_ = std::move(pSecurityHandler);
  return SUCCESS;
}

void CPDF_Parser::ReleaseEncryptHandler() {
  security_handler_.Reset();
}

// Ideally, all the cross reference entries should be verified.
// In reality, we rarely see well-formed cross references don't match
// with the objects. crbug/602650 showed a case where object numbers
// in the cross reference table are all off by one.
bool CPDF_Parser::VerifyCrossRefTable() {
  for (const auto& it : cross_ref_table_->objects_info()) {
    if (it.second.pos <= 0) {
      continue;
    }
    // Find the first non-zero position.
    FX_FILESIZE SavedPos = syntax_->GetPos();
    syntax_->SetPos(it.second.pos);
    CPDF_SyntaxParser::WordResult word_result = syntax_->GetNextWord();
    syntax_->SetPos(SavedPos);
    if (!word_result.is_number || word_result.word.IsEmpty() ||
        FXSYS_atoui(word_result.word.c_str()) != it.first) {
      // If the object number read doesn't match the one stored,
      // something is wrong with the cross reference table.
      return false;
    }
    break;
  }
  return true;
}

bool CPDF_Parser::LoadAllCrossRefTablesAndStreams(FX_FILESIZE xref_offset) {
  const bool is_xref_stream = !LoadCrossRefTable(xref_offset, /*skip=*/true);
  if (is_xref_stream) {
    // Use a copy of `xref_offset`, as LoadCrossRefStream() may change it.
    FX_FILESIZE xref_offset_copy = xref_offset;
    if (!LoadCrossRefStream(&xref_offset_copy, /*is_main_xref=*/true)) {
      return false;
    }

    // LoadCrossRefStream() sets the trailer when `is_main_xref` is true.
    // Thus no SetTrailer() call like the else-block below. Similarly,
    // LoadCrossRefStream() also calls SetObjectMapSize() itself, so no need to
    // call it again here.
  } else {
    RetainPtr<CPDF_Dictionary> trailer = LoadTrailer();
    if (!trailer) {
      return false;
    }

    cross_ref_table_->SetTrailer(std::move(trailer), kNoTrailerObjectNumber);

    const int32_t xrefsize = GetTrailer()->GetDirectIntegerFor("Size");
    if (xrefsize > 0 && xrefsize <= kMaxXRefSize) {
      cross_ref_table_->SetObjectMapSize(xrefsize);
    }
  }

  std::vector<FX_FILESIZE> xref_list;
  std::vector<FX_FILESIZE> xref_stream_list;

  if (is_xref_stream) {
    xref_list.push_back(0);
    xref_stream_list.push_back(xref_offset);
  } else {
    xref_list.push_back(xref_offset);
    xref_stream_list.push_back(GetTrailer()->GetDirectIntegerFor("XRefStm"));
  }

  if (!FindAllCrossReferenceTablesAndStream(xref_offset, xref_list,
                                            xref_stream_list)) {
    return false;
  }

  if (xref_list.front() > 0) {
    if (!LoadCrossRefTable(xref_list.front(), /*skip=*/false)) {
      return false;
    }

    if (!VerifyCrossRefTable()) {
      return false;
    }
  }

  // Cross reference table entries take precedence over cross reference stream
  // entries. So process the stream entries first and then give the cross
  // reference tables a chance to overwrite them.
  //
  // XRefStm entries should only be used in update sections, so skip
  // `xref_stream_list.front()`.
  //
  // See details in ISO 32000-1:2008, section 7.5.8.4.
  for (size_t i = 1; i < xref_list.size(); ++i) {
    if (xref_stream_list[i] > 0 &&
        !LoadCrossRefStream(&xref_stream_list[i], /*is_main_xref=*/false)) {
      return false;
    }
    if (xref_list[i] > 0 && !LoadCrossRefTable(xref_list[i], /*skip=*/false)) {
      return false;
    }
  }

  if (is_xref_stream) {
    object_stream_map_.clear();
    xref_stream_ = true;
  }

  return true;
}

bool CPDF_Parser::LoadLinearizedAllCrossRefTable(FX_FILESIZE main_xref_offset) {
  if (!LoadCrossRefTable(main_xref_offset, /*skip=*/false)) {
    return false;
  }

  RetainPtr<CPDF_Dictionary> main_trailer = LoadTrailer();
  if (!main_trailer) {
    return false;
  }

  // GetTrailer() currently returns the first-page trailer.
  if (GetTrailer()->GetDirectIntegerFor("Size") == 0) {
    return false;
  }

  // Read /XRefStm from the first-page trailer. No need to read /Prev for the
  // first-page trailer, as the caller already did that and passed it in as
  // |main_xref_offset|.
  FX_FILESIZE xref_stm = GetTrailer()->GetDirectIntegerFor("XRefStm");
  std::vector<FX_FILESIZE> xref_list{main_xref_offset};
  std::vector<FX_FILESIZE> xref_stream_list{xref_stm};

  // Merge the trailers. Now GetTrailer() returns the merged trailer, where
  // /Prev is from the main-trailer.
  cross_ref_table_ = CPDF_CrossRefTable::MergeUp(
      std::make_unique<CPDF_CrossRefTable>(std::move(main_trailer),
                                           kNoTrailerObjectNumber),
      std::move(cross_ref_table_));

  if (!FindAllCrossReferenceTablesAndStream(main_xref_offset, xref_list,
                                            xref_stream_list)) {
    return false;
  }

  // Unlike LoadAllCrossRefTablesAndStreams(), the first XRefStm entry in
  // `xref_stream_list` should be processed.
  if (xref_stream_list[0] > 0 &&
      !LoadCrossRefStream(&xref_stream_list[0], /*is_main_xref=*/false)) {
    return false;
  }

  // Cross reference table entries take precedence over cross reference stream
  // entries. So process the stream entries first and then give the cross
  // reference tables a chance to overwrite them.
  for (size_t i = 1; i < xref_list.size(); ++i) {
    if (xref_stream_list[i] > 0 &&
        !LoadCrossRefStream(&xref_stream_list[i], /*is_main_xref=*/false)) {
      return false;
    }
    if (xref_list[i] > 0 && !LoadCrossRefTable(xref_list[i], /*skip=*/false)) {
      return false;
    }
  }

  return true;
}

bool CPDF_Parser::ParseAndAppendCrossRefSubsectionData(
    uint32_t start_objnum,
    uint32_t count,
    std::vector<CrossRefObjData>* out_objects) {
  if (!count) {
    return true;
  }

  // Each entry shall be exactly 20 byte.
  // A sample entry looks like:
  // "0000000000 00007 f\r\n"
  static constexpr int32_t kEntrySize = 20;

  if (!out_objects) {
    FX_SAFE_FILESIZE pos = count;
    pos *= kEntrySize;
    pos += syntax_->GetPos();
    if (!pos.IsValid()) {
      return false;
    }
    syntax_->SetPos(pos.ValueOrDie());
    return true;
  }
  const size_t start_obj_index = out_objects->size();
  FX_SAFE_SIZE_T new_size = start_obj_index;
  new_size += count;
  if (!new_size.IsValid()) {
    return false;
  }

  if (new_size.ValueOrDie() > kMaxXRefSize) {
    return false;
  }

  const size_t max_entries_in_file = syntax_->GetDocumentSize() / kEntrySize;
  if (new_size.ValueOrDie() > max_entries_in_file) {
    return false;
  }

  out_objects->resize(new_size.ValueOrDie());

  DataVector<char> buf(1024 * kEntrySize + 1);
  buf.back() = '\0';

  uint32_t entries_to_read = count;
  while (entries_to_read > 0) {
    const uint32_t entries_in_block = std::min(entries_to_read, 1024u);
    const uint32_t bytes_to_read = entries_in_block * kEntrySize;
    auto block_span = pdfium::span(buf).first(bytes_to_read);
    if (!syntax_->ReadBlock(pdfium::as_writable_bytes(block_span))) {
      return false;
    }

    for (uint32_t i = 0; i < entries_in_block; i++) {
      uint32_t iObjectIndex = count - entries_to_read + i;
      CrossRefObjData& obj_data =
          (*out_objects)[start_obj_index + iObjectIndex];
      const uint32_t objnum = start_objnum + iObjectIndex;
      obj_data.obj_num = objnum;
      ObjectInfo& info = obj_data.info;

      pdfium::span<const char> pEntry =
          pdfium::span(buf).subspan(i * kEntrySize);
      if (pEntry[17] == 'f') {
        info.pos = 0;
        info.type = ObjectType::kFree;
      } else {
        const FX_SAFE_FILESIZE offset = FXSYS_atoi64(pEntry.data());
        if (!offset.IsValid()) {
          return false;
        }

        if (offset.ValueOrDie() == 0) {
          for (int32_t c = 0; c < 10; c++) {
            if (!FXSYS_IsDecimalDigit(pEntry[c])) {
              return false;
            }
          }
        }

        info.pos = offset.ValueOrDie();

        // TODO(art-snake): The info.gennum is uint16_t, but version may be
        // greater than max<uint16_t>. Need to solve this issue.
        const int32_t version =
            StringToInt(ByteStringView(pEntry.subspan<11u>()));
        info.gennum = version;
        info.type = ObjectType::kNormal;
      }
    }
    entries_to_read -= entries_in_block;
  }
  return true;
}

bool CPDF_Parser::ParseCrossRefTable(
    std::vector<CrossRefObjData>* out_objects) {
  if (out_objects) {
    out_objects->clear();
  }

  if (syntax_->GetKeyword() != "xref") {
    return false;
  }
  std::vector<CrossRefObjData> result_objects;
  while (true) {
    FX_FILESIZE saved_pos = syntax_->GetPos();
    CPDF_SyntaxParser::WordResult word_result = syntax_->GetNextWord();
    const ByteString& word = word_result.word;
    if (word.IsEmpty()) {
      return false;
    }

    if (!word_result.is_number) {
      syntax_->SetPos(saved_pos);
      break;
    }

    uint32_t start_objnum = FXSYS_atoui(word.c_str());
    if (start_objnum >= kMaxObjectNumber) {
      return false;
    }

    uint32_t count = syntax_->GetDirectNum();
    syntax_->ToNextWord();

    if (!ParseAndAppendCrossRefSubsectionData(
            start_objnum, count, out_objects ? &result_objects : nullptr)) {
      return false;
    }
  }
  if (out_objects) {
    *out_objects = std::move(result_objects);
  }
  return true;
}

bool CPDF_Parser::LoadCrossRefTable(FX_FILESIZE pos, bool skip) {
  syntax_->SetPos(pos);
  std::vector<CrossRefObjData> objects;
  if (!ParseCrossRefTable(skip ? nullptr : &objects)) {
    return false;
  }

  MergeCrossRefObjectsData(objects);
  return true;
}

void CPDF_Parser::MergeCrossRefObjectsData(
    const std::vector<CrossRefObjData>& objects) {
  for (const auto& obj : objects) {
    switch (obj.info.type) {
      case ObjectType::kFree:
        if (obj.info.gennum > 0) {
          cross_ref_table_->SetFree(obj.obj_num, obj.info.gennum);
        }
        break;
      case ObjectType::kNormal:
        cross_ref_table_->AddNormal(obj.obj_num, obj.info.gennum,
                                    obj.info.is_object_stream_flag,
                                    obj.info.pos);
        break;
      case ObjectType::kCompressed:
        cross_ref_table_->AddCompressed(obj.obj_num, obj.info.archive.obj_num,
                                        obj.info.archive.obj_index);
        break;
    }
  }
}

bool CPDF_Parser::FindAllCrossReferenceTablesAndStream(
    FX_FILESIZE main_xref_offset,
    std::vector<FX_FILESIZE>& xref_list,
    std::vector<FX_FILESIZE>& xref_stream_list) {
  std::set<FX_FILESIZE> seen_xref_offset{main_xref_offset};

  // When the trailer doesn't have Prev entry or Prev entry value is not
  // numerical, GetDirectInteger() returns 0. Loading will end.
  FX_FILESIZE xref_offset = GetTrailer()->GetDirectIntegerFor("Prev");
  while (xref_offset > 0) {
    // Check for circular references.
    if (pdfium::Contains(seen_xref_offset, xref_offset)) {
      return false;
    }

    seen_xref_offset.insert(xref_offset);

    // Use a copy of `xref_offset`, as LoadCrossRefStream() may change it.
    FX_FILESIZE xref_offset_copy = xref_offset;
    if (LoadCrossRefStream(&xref_offset_copy, /*is_main_xref=*/false)) {
      // Since `xref_offset` points to a cross reference stream, mark it
      // accordingly.
      xref_list.insert(xref_list.begin(), 0);
      xref_stream_list.insert(xref_stream_list.begin(), xref_offset);
      xref_offset = xref_offset_copy;

      // On success, LoadCrossRefStream() called CPDF_CrossRefTable::MergeUp()
      // when `is_main_xref` is false. Thus no explicit call here.
    } else {
      // SLOW ...
      LoadCrossRefTable(xref_offset, /*skip=*/true);

      RetainPtr<CPDF_Dictionary> trailer_dict = LoadTrailer();
      if (!trailer_dict) {
        return false;
      }

      // The trailer for cross reference tables may point to a cross reference
      // stream as well.
      xref_list.insert(xref_list.begin(), xref_offset);
      xref_stream_list.insert(xref_stream_list.begin(),
                              trailer_dict->GetIntegerFor("XRefStm"));
      xref_offset = trailer_dict->GetDirectIntegerFor("Prev");

      // SLOW ...
      cross_ref_table_ = CPDF_CrossRefTable::MergeUp(
          std::make_unique<CPDF_CrossRefTable>(std::move(trailer_dict),
                                               kNoTrailerObjectNumber),
          std::move(cross_ref_table_));
    }
  }
  return true;
}

bool CPDF_Parser::RebuildCrossRef() {
  auto cross_ref_table = std::make_unique<CPDF_CrossRefTable>();

  const uint32_t kBufferSize = 4096;
  syntax_->SetReadBufferSize(kBufferSize);
  syntax_->SetPos(0);

  std::vector<std::pair<uint32_t, FX_FILESIZE>> numbers;
  for (CPDF_SyntaxParser::WordResult result = syntax_->GetNextWord();
       !result.word.IsEmpty(); result = syntax_->GetNextWord()) {
    const ByteString& word = result.word;
    if (result.is_number) {
      numbers.emplace_back(FXSYS_atoui(word.c_str()),
                           syntax_->GetPos() - word.GetLength());
      if (numbers.size() > 2u) {
        numbers.erase(numbers.begin());
      }
      continue;
    }

    if (word == "(") {
      syntax_->ReadString();
    } else if (word == "<") {
      syntax_->ReadHexString();
    } else if (word == "trailer") {
      RetainPtr<CPDF_Object> pTrailer = syntax_->GetObjectBody(nullptr);
      if (pTrailer) {
        CPDF_Stream* stream_trailer = pTrailer->AsMutableStream();
        // Grab the object number from `pTrailer` before potentially calling
        // std::move(pTrailer) below.
        const uint32_t trailer_object_number = pTrailer->GetObjNum();
        RetainPtr<CPDF_Dictionary> trailer_dict =
            stream_trailer ? stream_trailer->GetMutableDict()
                           : ToDictionary(std::move(pTrailer));
        cross_ref_table = CPDF_CrossRefTable::MergeUp(
            std::move(cross_ref_table),
            std::make_unique<CPDF_CrossRefTable>(std::move(trailer_dict),
                                                 trailer_object_number));
      }
    } else if (word == "obj" && numbers.size() == 2u) {
      const FX_FILESIZE obj_pos = numbers[0].second;
      const uint32_t obj_num = numbers[0].first;
      const uint32_t gen_num = numbers[1].first;

      syntax_->SetPos(obj_pos);
      RetainPtr<CPDF_Stream> pStream = ToStream(syntax_->GetIndirectObject(
          nullptr, CPDF_SyntaxParser::ParseType::kStrict));

      if (pStream && pStream->GetDict()->GetNameFor("Type") == "XRef") {
        cross_ref_table = CPDF_CrossRefTable::MergeUp(
            std::move(cross_ref_table),
            std::make_unique<CPDF_CrossRefTable>(
                ToDictionary(pStream->GetDict()->Clone()),
                pStream->GetObjNum()));
      }

      if (obj_num < kMaxObjectNumber) {
        cross_ref_table->AddNormal(obj_num, gen_num, /*is_object_stream=*/false,
                                   obj_pos);
        const auto object_stream =
            CPDF_ObjectStream::Create(std::move(pStream));
        if (object_stream) {
          const auto& object_info = object_stream->object_info();
          for (size_t i = 0; i < object_info.size(); ++i) {
            const auto& info = object_info[i];
            if (info.obj_num < kMaxObjectNumber) {
              cross_ref_table->AddCompressed(info.obj_num, obj_num, i);
            }
          }
        }
      }
    }
    numbers.clear();
  }

  cross_ref_table_ = CPDF_CrossRefTable::MergeUp(std::move(cross_ref_table_),
                                                 std::move(cross_ref_table));
  // Resore default buffer size.
  syntax_->SetReadBufferSize(CPDF_Stream::kFileBufSize);

  return GetTrailer() && !cross_ref_table_->objects_info().empty();
}

bool CPDF_Parser::LoadCrossRefStream(FX_FILESIZE* pos, bool is_main_xref) {
  RetainPtr<const CPDF_Stream> pStream =
      ToStream(ParseIndirectObjectAt(*pos, 0));
  if (!pStream || !pStream->GetObjNum()) {
    return false;
  }

  RetainPtr<const CPDF_Dictionary> pDict = pStream->GetDict();
  int32_t prev = pDict->GetIntegerFor("Prev");
  if (prev < 0) {
    return false;
  }

  int32_t size = pDict->GetIntegerFor("Size");
  if (size < 0) {
    return false;
  }

  *pos = prev;

  auto new_cross_ref_table = std::make_unique<CPDF_CrossRefTable>(
      /*trailer=*/ToDictionary(pDict->Clone()),
      /*trailer_object_number=*/pStream->GetObjNum());
  if (is_main_xref) {
    cross_ref_table_ = std::move(new_cross_ref_table);
    cross_ref_table_->SetObjectMapSize(size);
  } else {
    cross_ref_table_ = CPDF_CrossRefTable::MergeUp(
        std::move(new_cross_ref_table), std::move(cross_ref_table_));
  }

  std::vector<CrossRefStreamIndexEntry> indices =
      GetCrossRefStreamIndices(pDict->GetArrayFor("Index").Get(), size);

  std::vector<uint32_t> field_widths =
      GetFieldWidths(pDict->GetArrayFor("W").Get());
  if (field_widths.size() < kMinFieldCount) {
    return false;
  }

  FX_SAFE_UINT32 dwAccWidth;
  for (uint32_t width : field_widths) {
    dwAccWidth += width;
  }
  if (!dwAccWidth.IsValid()) {
    return false;
  }

  uint32_t total_width = dwAccWidth.ValueOrDie();
  auto pAcc = pdfium::MakeRetain<CPDF_StreamAcc>(pStream);
  pAcc->LoadAllDataFiltered();

  pdfium::span<const uint8_t> data_span = pAcc->GetSpan();
  uint32_t segindex = 0;
  for (const auto& index : indices) {
    FX_SAFE_UINT32 seg_end = segindex;
    seg_end += index.obj_count;
    seg_end *= total_width;
    if (!seg_end.IsValid() || seg_end.ValueOrDie() > data_span.size()) {
      continue;
    }

    pdfium::span<const uint8_t> seg_span = data_span.subspan(
        segindex * total_width, index.obj_count * total_width);
    FX_SAFE_UINT32 safe_new_size = index.start_obj_num;
    safe_new_size += index.obj_count;
    if (!safe_new_size.IsValid()) {
      continue;
    }

    // Until SetObjectMapSize() below has been called by a prior loop iteration,
    // `current_size` is based on the /Size value parsed in
    // LoadCrossRefStream(). PDFs may not always have the correct /Size. In this
    // case, other PDF implementations ignore the incorrect size, and PDFium
    // also ignores incorrect size in trailers for cross reference tables.
    const uint32_t current_size =
        cross_ref_table_->objects_info().empty() ? 0 : GetLastObjNum() + 1;
    // So allow `new_size` to be greater than `current_size`, but avoid going
    // over `kMaxXRefSize`. This works just fine because the loop below checks
    // against `kMaxObjectNumber`, and the two "max" constants are in sync.
    const uint32_t new_size =
        std::min<uint32_t>(safe_new_size.ValueOrDie(), kMaxXRefSize);
    if (new_size > current_size) {
      cross_ref_table_->SetObjectMapSize(new_size);
    }

    for (uint32_t i = 0; i < index.obj_count; ++i) {
      const uint32_t obj_num = index.start_obj_num + i;
      if (obj_num >= kMaxObjectNumber) {
        break;
      }

      ProcessCrossRefStreamEntry(seg_span.subspan(i * total_width, total_width),
                                 field_widths, obj_num);
    }

    segindex += index.obj_count;
  }
  return true;
}

void CPDF_Parser::ProcessCrossRefStreamEntry(
    pdfium::span<const uint8_t> entry_span,
    pdfium::span<const uint32_t> field_widths,
    uint32_t obj_num) {
  DCHECK_GE(field_widths.size(), kMinFieldCount);
  ObjectType type;
  if (field_widths[0]) {
    const uint32_t cross_ref_stream_obj_type =
        GetFirstXRefStreamEntry(entry_span, field_widths);
    std::optional<ObjectType> maybe_type =
        GetObjectTypeFromCrossRefStreamType(cross_ref_stream_obj_type);
    if (!maybe_type.has_value()) {
      return;
    }
    type = maybe_type.value();
  } else {
    // Per ISO 32000-1:2008 table 17, use the default value of 1 for the xref
    // stream entry when it is not specified. The `type` assignment is the
    // equivalent to calling GetObjectTypeFromCrossRefStreamType(1).
    type = ObjectType::kNormal;
  }

  if (type == ObjectType::kFree) {
    const uint32_t gen_num = GetThirdXRefStreamEntry(entry_span, field_widths);
    if (pdfium::IsValueInRangeForNumericType<uint16_t>(gen_num)) {
      cross_ref_table_->SetFree(obj_num, gen_num);
    }
    return;
  }

  if (type == ObjectType::kNormal) {
    const uint32_t offset = GetSecondXRefStreamEntry(entry_span, field_widths);
    const uint32_t gen_num = GetThirdXRefStreamEntry(entry_span, field_widths);
    if (pdfium::IsValueInRangeForNumericType<FX_FILESIZE>(offset) &&
        pdfium::IsValueInRangeForNumericType<uint16_t>(gen_num)) {
      cross_ref_table_->AddNormal(obj_num, gen_num, /*is_object_stream=*/false,
                                  offset);
    }
    return;
  }

  DCHECK_EQ(type, ObjectType::kCompressed);
  const uint32_t archive_obj_num =
      GetSecondXRefStreamEntry(entry_span, field_widths);
  if (!IsValidObjectNumber(archive_obj_num)) {
    return;
  }

  const uint32_t archive_obj_index =
      GetThirdXRefStreamEntry(entry_span, field_widths);
  cross_ref_table_->AddCompressed(obj_num, archive_obj_num, archive_obj_index);
}

RetainPtr<const CPDF_Array> CPDF_Parser::GetIDArray() const {
  return GetTrailer() ? GetTrailer()->GetArrayFor("ID") : nullptr;
}

RetainPtr<const CPDF_Dictionary> CPDF_Parser::GetRoot() const {
  RetainPtr<CPDF_Object> obj =
      objects_holder_->GetOrParseIndirectObject(GetRootObjNum());
  return obj ? obj->GetDict() : nullptr;
}

RetainPtr<const CPDF_Dictionary> CPDF_Parser::GetEncryptDict() const {
  if (!GetTrailer()) {
    return nullptr;
  }

  RetainPtr<const CPDF_Object> pEncryptObj =
      GetTrailer()->GetObjectFor("Encrypt");
  if (!pEncryptObj) {
    return nullptr;
  }

  if (pEncryptObj->IsDictionary()) {
    return pdfium::WrapRetain(pEncryptObj->AsDictionary());
  }

  if (pEncryptObj->IsReference()) {
    return ToDictionary(objects_holder_->GetOrParseIndirectObject(
        pEncryptObj->AsReference()->GetRefObjNum()));
  }
  return nullptr;
}

ByteString CPDF_Parser::GetEncodedPassword() const {
  return GetSecurityHandler()->GetEncodedPassword(GetPassword().AsStringView());
}

const CPDF_Dictionary* CPDF_Parser::GetTrailer() const {
  return cross_ref_table_->trailer();
}

CPDF_Dictionary* CPDF_Parser::GetMutableTrailerForTesting() {
  return cross_ref_table_->GetMutableTrailerForTesting();
}

uint32_t CPDF_Parser::GetTrailerObjectNumber() const {
  return cross_ref_table_->trailer_object_number();
}

RetainPtr<CPDF_Dictionary> CPDF_Parser::GetCombinedTrailer() const {
  return cross_ref_table_->trailer()
             ? ToDictionary(cross_ref_table_->trailer()->Clone())
             : RetainPtr<CPDF_Dictionary>();
}

uint32_t CPDF_Parser::GetInfoObjNum() const {
  RetainPtr<const CPDF_Reference> pRef =
      ToReference(cross_ref_table_->trailer()
                      ? cross_ref_table_->trailer()->GetObjectFor("Info")
                      : nullptr);
  return pRef ? pRef->GetRefObjNum() : CPDF_Object::kInvalidObjNum;
}

uint32_t CPDF_Parser::GetRootObjNum() const {
  RetainPtr<const CPDF_Reference> pRef =
      ToReference(cross_ref_table_->trailer()
                      ? cross_ref_table_->trailer()->GetObjectFor("Root")
                      : nullptr);
  return pRef ? pRef->GetRefObjNum() : CPDF_Object::kInvalidObjNum;
}

RetainPtr<CPDF_Object> CPDF_Parser::ParseIndirectObject(uint32_t objnum) {
  if (!IsValidObjectNumber(objnum)) {
    return nullptr;
  }

  // Prevent circular parsing the same object.
  if (pdfium::Contains(parsing_obj_nums_, objnum)) {
    return nullptr;
  }

  ScopedSetInsertion local_insert(&parsing_obj_nums_, objnum);
  const auto* info = cross_ref_table_->GetObjectInfo(objnum);
  if (!info) {
    return nullptr;
  }

  switch (info->type) {
    case ObjectType::kFree: {
      return nullptr;
    }
    case ObjectType::kNormal: {
      if (info->pos <= 0) {
        return nullptr;
      }
      return ParseIndirectObjectAt(info->pos, objnum);
    }
    case ObjectType::kCompressed: {
      const auto* obj_stream = GetObjectStream(info->archive.obj_num);
      if (!obj_stream) {
        return nullptr;
      }
      return obj_stream->ParseObject(objects_holder_, objnum,
                                     info->archive.obj_index);
    }
  }
}

const CPDF_ObjectStream* CPDF_Parser::GetObjectStream(uint32_t object_number) {
  // Prevent circular parsing the same object.
  if (pdfium::Contains(parsing_obj_nums_, object_number)) {
    return nullptr;
  }

  auto it = object_stream_map_.find(object_number);
  if (it != object_stream_map_.end()) {
    return it->second.get();
  }

  const auto* info = cross_ref_table_->GetObjectInfo(object_number);
  if (!info || !info->is_object_stream_flag) {
    return nullptr;
  }

  const FX_FILESIZE object_pos = info->pos;
  if (object_pos <= 0) {
    return nullptr;
  }

  // Keep track of `object_number` before doing more parsing.
  ScopedSetInsertion local_insert(&parsing_obj_nums_, object_number);

  RetainPtr<CPDF_Object> object =
      ParseIndirectObjectAt(object_pos, object_number);
  if (!object) {
    return nullptr;
  }

  std::unique_ptr<CPDF_ObjectStream> objs_stream =
      CPDF_ObjectStream::Create(ToStream(object));
  const CPDF_ObjectStream* result = objs_stream.get();
  object_stream_map_[object_number] = std::move(objs_stream);

  return result;
}

RetainPtr<CPDF_Object> CPDF_Parser::ParseIndirectObjectAt(FX_FILESIZE pos,
                                                          uint32_t objnum) {
  const FX_FILESIZE saved_pos = syntax_->GetPos();
  syntax_->SetPos(pos);

  auto result = syntax_->GetIndirectObject(
      objects_holder_, CPDF_SyntaxParser::ParseType::kLoose);
  syntax_->SetPos(saved_pos);
  if (result && objnum && result->GetObjNum() != objnum) {
    return nullptr;
  }

  const bool should_decrypt = security_handler_ &&
                              security_handler_->GetCryptoHandler() &&
                              objnum != metadata_objnum_;
  if (should_decrypt &&
      !security_handler_->GetCryptoHandler()->DecryptObjectTree(result)) {
    return nullptr;
  }
  return result;
}

FX_FILESIZE CPDF_Parser::GetDocumentSize() const {
  return syntax_->GetDocumentSize();
}

uint32_t CPDF_Parser::GetFirstPageNo() const {
  return linearized_ ? linearized_->GetFirstPageNo() : 0;
}

void CPDF_Parser::SetLinearizedHeaderForTesting(
    std::unique_ptr<CPDF_LinearizedHeader> pLinearized) {
  linearized_ = std::move(pLinearized);
}

RetainPtr<CPDF_Dictionary> CPDF_Parser::LoadTrailer() {
  if (syntax_->GetKeyword() != "trailer") {
    return nullptr;
  }

  return ToDictionary(syntax_->GetObjectBody(objects_holder_));
}

uint32_t CPDF_Parser::GetPermissions(bool get_owner_perms) const {
  return security_handler_ ? security_handler_->GetPermissions(get_owner_perms)
                           : 0xFFFFFFFF;
}

std::unique_ptr<CPDF_LinearizedHeader> CPDF_Parser::ParseLinearizedHeader() {
  return CPDF_LinearizedHeader::Parse(syntax_.get());
}

CPDF_Parser::Error CPDF_Parser::StartLinearizedParse(
    RetainPtr<CPDF_ReadValidator> validator,
    const ByteString& password) {
  DCHECK(!has_parsed_);
  DCHECK(!xref_table_rebuilt_);
  SetPassword(password);
  xref_stream_ = false;
  last_xref_offset_ = 0;

  if (!InitSyntaxParser(std::move(validator))) {
    return FORMAT_ERROR;
  }

  linearized_ = ParseLinearizedHeader();
  if (!linearized_) {
    return StartParseInternal();
  }

  has_parsed_ = true;

  last_xref_offset_ = linearized_->GetLastXRefOffset();
  FX_FILESIZE dwFirstXRefOffset = last_xref_offset_;
  const bool loaded_xref_table =
      LoadCrossRefTable(dwFirstXRefOffset, /*skip=*/false);
  if (!loaded_xref_table &&
      !LoadCrossRefStream(&dwFirstXRefOffset, /*is_main_xref=*/true)) {
    if (!RebuildCrossRef()) {
      return FORMAT_ERROR;
    }

    xref_table_rebuilt_ = true;
    last_xref_offset_ = 0;
  }
  if (loaded_xref_table) {
    RetainPtr<CPDF_Dictionary> trailer = LoadTrailer();
    if (!trailer) {
      return SUCCESS;
    }

    cross_ref_table_->SetTrailer(std::move(trailer), kNoTrailerObjectNumber);
    const int32_t xrefsize = GetTrailer()->GetDirectIntegerFor("Size");
    if (xrefsize > 0) {
      // Check if `xrefsize` is correct. If it is incorrect, give up and rebuild
      // the xref table.
      const uint32_t expected_last_obj_num = xrefsize - 1;
      if (GetLastObjNum() != expected_last_obj_num && !RebuildCrossRef()) {
        return FORMAT_ERROR;
      }
    }
  }

  Error eRet = SetEncryptHandler();
  if (eRet != SUCCESS) {
    return eRet;
  }

  if (!GetRoot() || !objects_holder_->TryInit()) {
    if (xref_table_rebuilt_) {
      return FORMAT_ERROR;
    }

    ReleaseEncryptHandler();
    if (!RebuildCrossRef()) {
      return FORMAT_ERROR;
    }

    eRet = SetEncryptHandler();
    if (eRet != SUCCESS) {
      return eRet;
    }

    objects_holder_->TryInit();
    if (!GetRoot()) {
      return FORMAT_ERROR;
    }
  }

  if (GetRootObjNum() == CPDF_Object::kInvalidObjNum) {
    ReleaseEncryptHandler();
    if (!RebuildCrossRef() || GetRootObjNum() == CPDF_Object::kInvalidObjNum) {
      return FORMAT_ERROR;
    }

    eRet = SetEncryptHandler();
    if (eRet != SUCCESS) {
      return eRet;
    }
  }

  if (security_handler_ && security_handler_->IsMetadataEncrypted()) {
    RetainPtr<const CPDF_Reference> pMetadata =
        ToReference(GetRoot()->GetObjectFor("Metadata"));
    if (pMetadata) {
      metadata_objnum_ = pMetadata->GetRefObjNum();
    }
  }
  return SUCCESS;
}

bool CPDF_Parser::LoadLinearizedAllCrossRefStream(
    FX_FILESIZE main_xref_offset) {
  FX_FILESIZE xref_offset = main_xref_offset;
  if (!LoadCrossRefStream(&xref_offset, /*is_main_xref=*/false)) {
    return false;
  }

  std::set<FX_FILESIZE> seen_xref_offset;
  while (xref_offset) {
    seen_xref_offset.insert(xref_offset);
    if (!LoadCrossRefStream(&xref_offset, /*is_main_xref=*/false)) {
      return false;
    }

    // Check for circular references.
    if (pdfium::Contains(seen_xref_offset, xref_offset)) {
      return false;
    }
  }
  object_stream_map_.clear();
  xref_stream_ = true;
  return true;
}

CPDF_Parser::Error CPDF_Parser::LoadLinearizedMainXRefTable() {
  const FX_SAFE_FILESIZE prev = GetTrailer()->GetIntegerFor("Prev");
  const FX_FILESIZE main_xref_offset = prev.ValueOrDefault(-1);
  if (main_xref_offset < 0) {
    return FORMAT_ERROR;
  }

  if (main_xref_offset == 0) {
    return SUCCESS;
  }

  const AutoRestorer<uint32_t> save_metadata_objnum(&metadata_objnum_);
  metadata_objnum_ = 0;
  object_stream_map_.clear();

  if (!LoadLinearizedAllCrossRefTable(main_xref_offset) &&
      !LoadLinearizedAllCrossRefStream(main_xref_offset)) {
    last_xref_offset_ = 0;
    return FORMAT_ERROR;
  }

  return SUCCESS;
}

void CPDF_Parser::SetSyntaxParserForTesting(
    std::unique_ptr<CPDF_SyntaxParser> parser) {
  syntax_ = std::move(parser);
}

std::vector<unsigned int> CPDF_Parser::GetTrailerEnds() {
  std::vector<unsigned int> trailer_ends;
  syntax_->SetTrailerEnds(&trailer_ends);

  // Traverse the document.
  syntax_->SetPos(0);
  while (true) {
    CPDF_SyntaxParser::WordResult word_result = syntax_->GetNextWord();
    if (word_result.is_number) {
      // The object number was read. Read the generation number.
      word_result = syntax_->GetNextWord();
      if (!word_result.is_number) {
        break;
      }

      word_result = syntax_->GetNextWord();
      if (word_result.word != "obj") {
        break;
      }

      syntax_->GetObjectBody(nullptr);

      word_result = syntax_->GetNextWord();
      if (word_result.word != "endobj") {
        break;
      }
    } else if (word_result.word == "trailer") {
      syntax_->GetObjectBody(nullptr);
    } else if (word_result.word == "startxref") {
      syntax_->GetNextWord();
    } else if (word_result.word == "xref") {
      while (true) {
        word_result = syntax_->GetNextWord();
        if (word_result.word.IsEmpty() || word_result.word == "startxref") {
          break;
        }
      }
      syntax_->GetNextWord();
    } else {
      break;
    }
  }

  // Stop recording trailer ends.
  syntax_->SetTrailerEnds(nullptr);
  return trailer_ends;
}

bool CPDF_Parser::WriteToArchive(IFX_ArchiveStream* archive,
                                 FX_FILESIZE src_size) {
  static constexpr FX_FILESIZE kBufferSize = 4096;
  DataVector<uint8_t> buffer(kBufferSize);
  syntax_->SetPos(0);
  while (src_size) {
    const uint32_t block_size =
        static_cast<uint32_t>(std::min(kBufferSize, src_size));
    auto block_span = pdfium::span(buffer).first(block_size);
    if (!syntax_->ReadBlock(block_span)) {
      return false;
    }
    if (!archive->WriteBlock(pdfium::span(buffer).first(block_size))) {
      return false;
    }
    src_size -= block_size;
  }
  return true;
}
