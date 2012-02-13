//===- LDSection.cpp ------------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <mcld/LD/LDSection.h>
#include <llvm/MC/SectionKind.h>

using namespace mcld;

LDSection::LDSection(const std::string& pName,
                     LDFileFormat::Kind pKind,
                     uint32_t pType,
                     uint32_t pFlag,
                     uint64_t pSize,
                     uint64_t pOffset,
                     uint64_t pAddr)
  : llvm::MCSection(llvm::MCSection::SV_LDContext, llvm::SectionKind::getMetadata()),
    m_Name(pName),
    m_Kind(pKind),
    m_Type(pType),
    m_Flag(pFlag),
    m_Size(pSize),
    m_Offset(pOffset),
    m_Addr(pAddr),
    m_pSectionData(NULL),
    m_pLink(NULL),
    m_Info(0) {
}

