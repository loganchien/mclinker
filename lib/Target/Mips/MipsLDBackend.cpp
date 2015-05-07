//===- MipsLDBackend.cpp --------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "Mips.h"
#include "MipsGNUInfo.h"
#include "MipsELFDynamic.h"
#include "MipsLA25Stub.h"
#include "MipsLDBackend.h"
#include "MipsRelocator.h"

#include "mcld/IRBuilder.h"
#include "mcld/LinkerConfig.h"
#include "mcld/Module.h"
#include "mcld/Fragment/AlignFragment.h"
#include "mcld/Fragment/FillFragment.h"
#include "mcld/LD/BranchIslandFactory.h"
#include "mcld/LD/LDContext.h"
#include "mcld/LD/StubFactory.h"
#include "mcld/LD/ELFFileFormat.h"
#include "mcld/MC/Attribute.h"
#include "mcld/Object/ObjectBuilder.h"
#include "mcld/Support/MemoryRegion.h"
#include "mcld/Support/MemoryArea.h"
#include "mcld/Support/MsgHandling.h"
#include "mcld/Support/TargetRegistry.h"
#include "mcld/Target/OutputRelocSection.h"

#include <llvm/ADT/Triple.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/ELF.h>
#include <llvm/Support/Host.h>

namespace mcld {

//===----------------------------------------------------------------------===//
// MipsGNULDBackend
//===----------------------------------------------------------------------===//
MipsGNULDBackend::MipsGNULDBackend(const LinkerConfig& pConfig,
                                   MipsGNUInfo* pInfo)
    : GNULDBackend(pConfig, pInfo),
      m_pRelocator(NULL),
      m_pGOT(NULL),
      m_pPLT(NULL),
      m_pGOTPLT(NULL),
      m_pInfo(*pInfo),
      m_pRelPlt(NULL),
      m_pRelDyn(NULL),
      m_pDynamic(NULL),
      m_pGOTSymbol(NULL),
      m_pPLTSymbol(NULL),
      m_pGpDispSymbol(NULL) {
}

MipsGNULDBackend::~MipsGNULDBackend() {
  delete m_pRelocator;
  delete m_pPLT;
  delete m_pRelPlt;
  delete m_pRelDyn;
  delete m_pDynamic;
}

bool MipsGNULDBackend::needsLA25Stub(Relocation::Type pType,
                                     const mcld::ResolveInfo* pSym) {
  if (config().isCodeIndep())
    return false;

  if (llvm::ELF::R_MIPS_26 != pType)
    return false;

  if (pSym->isLocal())
    return false;

  return true;
}

void MipsGNULDBackend::addNonPICBranchSym(ResolveInfo* rsym) {
  m_HasNonPICBranchSyms.insert(rsym);
}

bool MipsGNULDBackend::hasNonPICBranch(const ResolveInfo* rsym) const {
  return m_HasNonPICBranchSyms.count(rsym);
}

void MipsGNULDBackend::initTargetSections(Module& pModule,
                                          ObjectBuilder& pBuilder) {
  if (LinkerConfig::Object == config().codeGenType())
    return;

  ELFFileFormat* file_format = getOutputFormat();

  // initialize .rel.plt
  LDSection& relplt = file_format->getRelPlt();
  m_pRelPlt = new OutputRelocSection(pModule, relplt);

  // initialize .rel.dyn
  LDSection& reldyn = file_format->getRelDyn();
  m_pRelDyn = new OutputRelocSection(pModule, reldyn);

  // initialize .sdata
  m_psdata = pBuilder.CreateSection(
      ".sdata", LDFileFormat::Target, llvm::ELF::SHT_PROGBITS,
      llvm::ELF::SHF_ALLOC | llvm::ELF::SHF_WRITE | llvm::ELF::SHF_MIPS_GPREL,
      4);
}

void MipsGNULDBackend::initTargetSymbols(IRBuilder& pBuilder, Module& pModule) {
  // Define the symbol _GLOBAL_OFFSET_TABLE_ if there is a symbol with the
  // same name in input
  m_pGOTSymbol = pBuilder.AddSymbol<IRBuilder::AsReferred, IRBuilder::Resolve>(
      "_GLOBAL_OFFSET_TABLE_",
      ResolveInfo::Object,
      ResolveInfo::Define,
      ResolveInfo::Local,
      0x0,                  // size
      0x0,                  // value
      FragmentRef::Null(),  // FragRef
      ResolveInfo::Hidden);

  // Define the symbol _PROCEDURE_LINKAGE_TABLE_ if there is a symbol with the
  // same name in input
  m_pPLTSymbol = pBuilder.AddSymbol<IRBuilder::AsReferred, IRBuilder::Resolve>(
      "_PROCEDURE_LINKAGE_TABLE_",
      ResolveInfo::Object,
      ResolveInfo::Define,
      ResolveInfo::Local,
      0x0,                  // size
      0x0,                  // value
      FragmentRef::Null(),  // FragRef
      ResolveInfo::Hidden);

  m_pGpDispSymbol =
      pBuilder.AddSymbol<IRBuilder::AsReferred, IRBuilder::Resolve>(
          "_gp_disp",
          ResolveInfo::Section,
          ResolveInfo::Define,
          ResolveInfo::Absolute,
          0x0,                  // size
          0x0,                  // value
          FragmentRef::Null(),  // FragRef
          ResolveInfo::Default);

  pBuilder.AddSymbol<IRBuilder::AsReferred, IRBuilder::Unresolve>(
      "_gp",
      ResolveInfo::NoType,
      ResolveInfo::Define,
      ResolveInfo::Absolute,
      0x0,                  // size
      0x0,                  // value
      FragmentRef::Null(),  // FragRef
      ResolveInfo::Default);
}

const Relocator* MipsGNULDBackend::getRelocator() const {
  assert(m_pRelocator != NULL);
  return m_pRelocator;
}

Relocator* MipsGNULDBackend::getRelocator() {
  assert(m_pRelocator != NULL);
  return m_pRelocator;
}

void MipsGNULDBackend::doPreLayout(IRBuilder& pBuilder) {
  // initialize .dynamic data
  if (!config().isCodeStatic() && m_pDynamic == NULL)
    m_pDynamic = new MipsELFDynamic(*this, config());

  // set .got size
  // when building shared object, the .got section is must.
  if (LinkerConfig::Object != config().codeGenType()) {
    if (LinkerConfig::DynObj == config().codeGenType() || m_pGOT->hasGOT1() ||
        m_pGOTSymbol != NULL) {
      m_pGOT->finalizeScanning(*m_pRelDyn);
      m_pGOT->finalizeSectionSize();

      defineGOTSymbol(pBuilder);
    }

    if (m_pGOTPLT->hasGOT1()) {
      m_pGOTPLT->finalizeSectionSize();

      defineGOTPLTSymbol(pBuilder);
    }

    if (m_pPLT->hasPLT1())
      m_pPLT->finalizeSectionSize();

    ELFFileFormat* file_format = getOutputFormat();

    // set .rel.plt size
    if (!m_pRelPlt->empty()) {
      assert(
          !config().isCodeStatic() &&
          "static linkage should not result in a dynamic relocation section");
      file_format->getRelPlt().setSize(m_pRelPlt->numOfRelocs() *
                                       getRelEntrySize());
    }

    // set .rel.dyn size
    if (!m_pRelDyn->empty()) {
      assert(
          !config().isCodeStatic() &&
          "static linkage should not result in a dynamic relocation section");
      file_format->getRelDyn().setSize(m_pRelDyn->numOfRelocs() *
                                       getRelEntrySize());
    }
  }
}

void MipsGNULDBackend::doPostLayout(Module& pModule, IRBuilder& pBuilder) {
  const ELFFileFormat* format = getOutputFormat();

  if (format->hasGOTPLT()) {
    assert(m_pGOTPLT != NULL && "doPostLayout failed, m_pGOTPLT is NULL!");
    m_pGOTPLT->applyAllGOTPLT(m_pPLT->addr());
  }

  if (format->hasPLT()) {
    assert(m_pPLT != NULL && "doPostLayout failed, m_pPLT is NULL!");
    m_pPLT->applyAllPLT(*m_pGOTPLT);
  }

  m_pInfo.setABIVersion(m_pPLT && m_pPLT->hasPLT1() ? 1 : 0);
}

/// dynamic - the dynamic section of the target machine.
/// Use co-variant return type to return its own dynamic section.
MipsELFDynamic& MipsGNULDBackend::dynamic() {
  assert(m_pDynamic != NULL);
  return *m_pDynamic;
}

/// dynamic - the dynamic section of the target machine.
/// Use co-variant return type to return its own dynamic section.
const MipsELFDynamic& MipsGNULDBackend::dynamic() const {
  assert(m_pDynamic != NULL);
  return *m_pDynamic;
}

uint64_t MipsGNULDBackend::emitSectionData(const LDSection& pSection,
                                           MemoryRegion& pRegion) const {
  assert(pRegion.size() && "Size of MemoryRegion is zero!");

  const ELFFileFormat* file_format = getOutputFormat();

  if (file_format->hasGOT() && (&pSection == &(file_format->getGOT()))) {
    return m_pGOT->emit(pRegion);
  }

  if (file_format->hasPLT() && (&pSection == &(file_format->getPLT()))) {
    return m_pPLT->emit(pRegion);
  }

  if (file_format->hasGOTPLT() && (&pSection == &(file_format->getGOTPLT()))) {
    return m_pGOTPLT->emit(pRegion);
  }

  if (&pSection == m_psdata && m_psdata->hasSectionData()) {
    const SectionData* sect_data = pSection.getSectionData();
    SectionData::const_iterator frag_iter, frag_end = sect_data->end();
    uint8_t* out_offset = pRegion.begin();
    for (frag_iter = sect_data->begin(); frag_iter != frag_end; ++frag_iter) {
      size_t size = frag_iter->size();
      switch (frag_iter->getKind()) {
        case Fragment::Fillment: {
          const FillFragment& fill_frag = llvm::cast<FillFragment>(*frag_iter);
          if (fill_frag.getValueSize() == 0) {
            // virtual fillment, ignore it.
            break;
          }
          memset(out_offset, fill_frag.getValue(), fill_frag.size());
          break;
        }
        case Fragment::Region: {
          const RegionFragment& region_frag =
              llvm::cast<RegionFragment>(*frag_iter);
          const char* start = region_frag.getRegion().begin();
          memcpy(out_offset, start, size);
          break;
        }
        case Fragment::Alignment: {
          const AlignFragment& align_frag =
              llvm::cast<AlignFragment>(*frag_iter);
          uint64_t count = size / align_frag.getValueSize();
          switch (align_frag.getValueSize()) {
            case 1u:
              std::memset(out_offset, align_frag.getValue(), count);
              break;
            default:
              llvm::report_fatal_error(
                  "unsupported value size for align fragment emission yet.\n");
              break;
          }  // end switch
          break;
        }
        case Fragment::Null: {
          assert(0x0 == size);
          break;
        }
        default:
          llvm::report_fatal_error("unsupported fragment type.\n");
          break;
      }  // end switch
      out_offset += size;
    }
    return pRegion.size();
  }

  fatal(diag::unrecognized_output_sectoin) << pSection.name()
                                           << "mclinker@googlegroups.com";
  return 0;
}

bool MipsGNULDBackend::hasEntryInStrTab(const LDSymbol& pSym) const {
  return ResolveInfo::Section != pSym.type() || m_pGpDispSymbol == &pSym;
}

namespace {
struct DynsymGOTCompare {
  const MipsGOT& m_pGOT;

  explicit DynsymGOTCompare(const MipsGOT& pGOT) : m_pGOT(pGOT) {}

  bool operator()(const LDSymbol* X, const LDSymbol* Y) const {
    return m_pGOT.dynSymOrderCompare(X, Y);
  }
};
}  // anonymous namespace

void MipsGNULDBackend::orderSymbolTable(Module& pModule) {
  if (config().options().hasGNUHash()) {
    // The MIPS ABI and .gnu.hash require .dynsym to be sorted
    // in different ways. The MIPS ABI requires a mapping between
    // the GOT and the symbol table. At the same time .gnu.hash
    // needs symbols to be grouped by hash code.
    llvm::errs() << ".gnu.hash is incompatible with the MIPS ABI\n";
  }

  Module::SymbolTable& symbols = pModule.getSymbolTable();

  std::stable_sort(
      symbols.dynamicBegin(), symbols.dynamicEnd(), DynsymGOTCompare(*m_pGOT));
}

}  // namespace mcld

namespace llvm {
namespace ELF {
// SHT_MIPS_OPTIONS section's block descriptor.
struct Elf_Options {
  unsigned char kind;  // Determines interpretation of variable
                       // part of descriptor. See ODK_xxx enumeration.
  unsigned char size;  // Byte size of descriptor, including this header.
  Elf64_Half section;  // Section header index of section affected,
                       // or 0 for global options.
  Elf64_Word info;     // Kind-speciﬁc information.
};

// Content of ODK_REGINFO block in SHT_MIPS_OPTIONS section on 32 bit ABI.
struct Elf32_RegInfo {
  Elf32_Word ri_gprmask;     // Mask of general purpose registers used.
  Elf32_Word ri_cprmask[4];  // Mask of co-processor registers used.
  Elf32_Addr ri_gp_value;    // GP register value for this object file.
};

// Content of ODK_REGINFO block in SHT_MIPS_OPTIONS section on 64 bit ABI.
struct Elf64_RegInfo {
  Elf32_Word ri_gprmask;     // Mask of general purpose registers used.
  Elf32_Word ri_pad;         // Padding.
  Elf32_Word ri_cprmask[4];  // Mask of co-processor registers used.
  Elf64_Addr ri_gp_value;    // GP register value for this object file.
};

}  // namespace ELF
}  // namespace llvm

namespace mcld {

static const char* ArchName(uint64_t flagBits) {
  using namespace llvm::ELF;
  switch (flagBits) {
    case EF_MIPS_ARCH_1:
      return "mips1";
    case EF_MIPS_ARCH_2:
      return "mips2";
    case EF_MIPS_ARCH_3:
      return "mips3";
    case EF_MIPS_ARCH_4:
      return "mips4";
    case EF_MIPS_ARCH_5:
      return "mips5";
    case EF_MIPS_ARCH_32:
      return "mips32";
    case EF_MIPS_ARCH_64:
      return "mips64";
    case EF_MIPS_ARCH_32R2:
      return "mips32r2";
    case EF_MIPS_ARCH_64R2:
      return "mips64r2";
    case EF_MIPS_ARCH_32R6:
      return "mips32r6";
    case EF_MIPS_ARCH_64R6:
      return "mips64r6";
    default:
      return "Unknown Arch";
  }
}

void MipsGNULDBackend::mergeFlags(Input& pInput, const char* ELF_hdr) {
  if (ELF_hdr[llvm::ELF::EI_CLASS] == llvm::ELF::ELFCLASS64) {
    const llvm::ELF::Elf64_Ehdr* hdr =
        reinterpret_cast<const llvm::ELF::Elf64_Ehdr*>(ELF_hdr);
    mergeFlagsFromHeader(pInput, hdr->e_flags);
  } else if (ELF_hdr[llvm::ELF::EI_CLASS] == llvm::ELF::ELFCLASS32) {
    const llvm::ELF::Elf32_Ehdr* hdr =
        reinterpret_cast<const llvm::ELF::Elf32_Ehdr*>(ELF_hdr);
    mergeFlagsFromHeader(pInput, hdr->e_flags);
  }
  return;
}

// TDB complete this method to match functionality
// from binutils file elfxx-mips.c
// For now we just get the arch flags right.
//
void MipsGNULDBackend::mergeFlagsFromHeader(Input& pInput, uint64_t newFlags) {
  using namespace llvm::ELF;
  bool arch64Bit = config().targets().triple().isArch64Bit();
  uint64_t newArch = newFlags & EF_MIPS_ARCH;
  // check that newArch is supported
  switch (newArch) {
    case EF_MIPS_ARCH_32:
    case EF_MIPS_ARCH_32R2:
    case EF_MIPS_ARCH_32R6:
      if (arch64Bit) {
        fatal(diag::error_Mips_unsupported_flags_for_arch)
            << pInput.name() << ArchName(newArch) << "mips64";
        return;  // error
      }
      break;
    case EF_MIPS_ARCH_64:
    case EF_MIPS_ARCH_64R2:
    case EF_MIPS_ARCH_64R6:
      if (!arch64Bit) {
        fatal(diag::error_Mips_unsupported_flags_for_arch)
            << pInput.name() << ArchName(newArch) << "mips32";
        return;  // error
      }
      break;
    default:
      // unsupported
      error(diag::error_Mips_unsupported_arch) << pInput.name()
                                               << ArchName(newArch);
      return;
  }

  // PIC code is inherently CPIC and may not set CPIC flag explicitly.
  // Ensure that this flag will exist in the linked file.
  if (newFlags & EF_MIPS_PIC)
    newFlags |= EF_MIPS_CPIC;

  uint32_t newPic = newFlags & (EF_MIPS_PIC | EF_MIPS_CPIC);
  uint32_t oldPic = m_pInfo.getPICFlags() & (EF_MIPS_PIC | EF_MIPS_CPIC);

  uint64_t currentArch = m_pInfo.getArchFlags();
  if (!currentArch) {
    // If arch flags is not initialized, we are processing the first
    // object file so just save current flags as initial state.
    m_pInfo.setArchFlags(newArch);
    m_pInfo.setPICFlags(newPic);
    return;
  }

  // Check PIC / CPIC flags compatibility.
  if ((newPic != 0) != (oldPic != 0))
    warning(diag::warn_Mips_abicalls_linking) << pInput.name();

  if (!(newPic & EF_MIPS_PIC))
    oldPic &= ~EF_MIPS_PIC;
  if (newPic)
    oldPic |= EF_MIPS_CPIC;
  newPic = oldPic;

  // check that architecture is not changing
  if (currentArch != newArch) {
    if ((newArch == EF_MIPS_ARCH_32 && currentArch == EF_MIPS_ARCH_32R2) ||
        (newArch == EF_MIPS_ARCH_64 && currentArch == EF_MIPS_ARCH_64R2))
      return; // do not need to update flags
    if ((newArch == EF_MIPS_ARCH_32R2 && currentArch == EF_MIPS_ARCH_32) ||
        (newArch == EF_MIPS_ARCH_64R2 && currentArch == EF_MIPS_ARCH_64))
      ;       // need to update flags
    else {
      error(diag::error_Mips_inconsistent_arch)
          << pInput.name() << ArchName(newArch) << ArchName(currentArch);
      return; // error
    }
  }
  m_pInfo.setArchFlags(newArch);
  m_pInfo.setPICFlags(newPic);
}

bool MipsGNULDBackend::readSection(Input& pInput, SectionData& pSD) {
  if (pSD.getSection().flag() & llvm::ELF::SHF_MIPS_GPREL) {
    uint64_t offset = pInput.fileOffset() + pSD.getSection().offset();
    uint64_t size = pSD.getSection().size();

    Fragment* frag = IRBuilder::CreateRegion(pInput, offset, size);
    ObjectBuilder::AppendFragment(*frag, pSD);
    return true;
  }

  if (pSD.getSection().type() == llvm::ELF::SHT_MIPS_OPTIONS) {
    uint32_t offset = pInput.fileOffset() + pSD.getSection().offset();
    uint32_t size = pSD.getSection().size();

    llvm::StringRef region = pInput.memArea()->request(offset, size);
    if (region.size() > 0) {
      const llvm::ELF::Elf_Options* optb =
          reinterpret_cast<const llvm::ELF::Elf_Options*>(region.begin());
      const llvm::ELF::Elf_Options* opte =
          reinterpret_cast<const llvm::ELF::Elf_Options*>(region.begin() +
                                                          size);

      for (const llvm::ELF::Elf_Options* opt = optb; opt < opte;
           opt += opt->size) {
        switch (opt->kind) {
          default:
            // Nothing to do.
            break;
          case llvm::ELF::ODK_REGINFO:
            if (config().targets().triple().isArch32Bit()) {
              const llvm::ELF::Elf32_RegInfo* reg =
                  reinterpret_cast<const llvm::ELF::Elf32_RegInfo*>(opt + 1);
              m_GP0Map[&pInput] = reg->ri_gp_value;
            } else {
              const llvm::ELF::Elf64_RegInfo* reg =
                  reinterpret_cast<const llvm::ELF::Elf64_RegInfo*>(opt + 1);
              m_GP0Map[&pInput] = reg->ri_gp_value;
            }
            break;
        }
      }
    }

    return true;
  }

  return GNULDBackend::readSection(pInput, pSD);
}

MipsGOT& MipsGNULDBackend::getGOT() {
  assert(m_pGOT != NULL);
  return *m_pGOT;
}

const MipsGOT& MipsGNULDBackend::getGOT() const {
  assert(m_pGOT != NULL);
  return *m_pGOT;
}

MipsPLT& MipsGNULDBackend::getPLT() {
  assert(m_pPLT != NULL);
  return *m_pPLT;
}

const MipsPLT& MipsGNULDBackend::getPLT() const {
  assert(m_pPLT != NULL);
  return *m_pPLT;
}

MipsGOTPLT& MipsGNULDBackend::getGOTPLT() {
  assert(m_pGOTPLT != NULL);
  return *m_pGOTPLT;
}

const MipsGOTPLT& MipsGNULDBackend::getGOTPLT() const {
  assert(m_pGOTPLT != NULL);
  return *m_pGOTPLT;
}

OutputRelocSection& MipsGNULDBackend::getRelPLT() {
  assert(m_pRelPlt != NULL);
  return *m_pRelPlt;
}

const OutputRelocSection& MipsGNULDBackend::getRelPLT() const {
  assert(m_pRelPlt != NULL);
  return *m_pRelPlt;
}

OutputRelocSection& MipsGNULDBackend::getRelDyn() {
  assert(m_pRelDyn != NULL);
  return *m_pRelDyn;
}

const OutputRelocSection& MipsGNULDBackend::getRelDyn() const {
  assert(m_pRelDyn != NULL);
  return *m_pRelDyn;
}

unsigned int MipsGNULDBackend::getTargetSectionOrder(
    const LDSection& pSectHdr) const {
  const ELFFileFormat* file_format = getOutputFormat();

  if (file_format->hasGOT() && (&pSectHdr == &file_format->getGOT()))
    return SHO_DATA;

  if (file_format->hasGOTPLT() && (&pSectHdr == &file_format->getGOTPLT()))
    return SHO_DATA;

  if (file_format->hasPLT() && (&pSectHdr == &file_format->getPLT()))
    return SHO_PLT;

  if (&pSectHdr == m_psdata)
    return SHO_SMALL_DATA;

  return SHO_UNDEFINED;
}

/// finalizeSymbol - finalize the symbol value
bool MipsGNULDBackend::finalizeTargetSymbols() {
  if (m_pGpDispSymbol != NULL)
    m_pGpDispSymbol->setValue(m_pGOT->getGPDispAddress());

  return true;
}

/// allocateCommonSymbols - allocate common symbols in the corresponding
/// sections. This is called at pre-layout stage.
/// FIXME: Mips needs to allocate small common symbol
bool MipsGNULDBackend::allocateCommonSymbols(Module& pModule) {
  SymbolCategory& symbol_list = pModule.getSymbolTable();

  if (symbol_list.emptyCommons() && symbol_list.emptyFiles() &&
      symbol_list.emptyLocals() && symbol_list.emptyLocalDyns())
    return true;

  SymbolCategory::iterator com_sym, com_end;

  // FIXME: If the order of common symbols is defined, then sort common symbols
  // std::sort(com_sym, com_end, some kind of order);

  // get corresponding BSS LDSection
  ELFFileFormat* file_format = getOutputFormat();
  LDSection& bss_sect = file_format->getBSS();
  LDSection& tbss_sect = file_format->getTBSS();

  // get or create corresponding BSS SectionData
  SectionData* bss_sect_data = NULL;
  if (bss_sect.hasSectionData())
    bss_sect_data = bss_sect.getSectionData();
  else
    bss_sect_data = IRBuilder::CreateSectionData(bss_sect);

  SectionData* tbss_sect_data = NULL;
  if (tbss_sect.hasSectionData())
    tbss_sect_data = tbss_sect.getSectionData();
  else
    tbss_sect_data = IRBuilder::CreateSectionData(tbss_sect);

  // remember original BSS size
  uint64_t bss_offset = bss_sect.size();
  uint64_t tbss_offset = tbss_sect.size();

  // allocate all local common symbols
  com_end = symbol_list.localEnd();

  for (com_sym = symbol_list.localBegin(); com_sym != com_end; ++com_sym) {
    if (ResolveInfo::Common == (*com_sym)->desc()) {
      // We have to reset the description of the symbol here. When doing
      // incremental linking, the output relocatable object may have common
      // symbols. Therefore, we can not treat common symbols as normal symbols
      // when emitting the regular name pools. We must change the symbols'
      // description here.
      (*com_sym)->resolveInfo()->setDesc(ResolveInfo::Define);
      Fragment* frag = new FillFragment(0x0, 1, (*com_sym)->size());

      if (ResolveInfo::ThreadLocal == (*com_sym)->type()) {
        // allocate TLS common symbol in tbss section
        tbss_offset += ObjectBuilder::AppendFragment(
            *frag, *tbss_sect_data, (*com_sym)->value());
        ObjectBuilder::UpdateSectionAlign(tbss_sect, (*com_sym)->value());
        (*com_sym)->setFragmentRef(FragmentRef::Create(*frag, 0));
      } else {
        // FIXME: how to identify small and large common symbols?
        bss_offset += ObjectBuilder::AppendFragment(
            *frag, *bss_sect_data, (*com_sym)->value());
        ObjectBuilder::UpdateSectionAlign(bss_sect, (*com_sym)->value());
        (*com_sym)->setFragmentRef(FragmentRef::Create(*frag, 0));
      }
    }
  }

  // allocate all global common symbols
  com_end = symbol_list.commonEnd();
  for (com_sym = symbol_list.commonBegin(); com_sym != com_end; ++com_sym) {
    // We have to reset the description of the symbol here. When doing
    // incremental linking, the output relocatable object may have common
    // symbols. Therefore, we can not treat common symbols as normal symbols
    // when emitting the regular name pools. We must change the symbols'
    // description here.
    (*com_sym)->resolveInfo()->setDesc(ResolveInfo::Define);
    Fragment* frag = new FillFragment(0x0, 1, (*com_sym)->size());

    if (ResolveInfo::ThreadLocal == (*com_sym)->type()) {
      // allocate TLS common symbol in tbss section
      tbss_offset += ObjectBuilder::AppendFragment(
          *frag, *tbss_sect_data, (*com_sym)->value());
      ObjectBuilder::UpdateSectionAlign(tbss_sect, (*com_sym)->value());
      (*com_sym)->setFragmentRef(FragmentRef::Create(*frag, 0));
    } else {
      // FIXME: how to identify small and large common symbols?
      bss_offset += ObjectBuilder::AppendFragment(
          *frag, *bss_sect_data, (*com_sym)->value());
      ObjectBuilder::UpdateSectionAlign(bss_sect, (*com_sym)->value());
      (*com_sym)->setFragmentRef(FragmentRef::Create(*frag, 0));
    }
  }

  bss_sect.setSize(bss_offset);
  tbss_sect.setSize(tbss_offset);
  symbol_list.changeCommonsToGlobal();
  return true;
}

uint64_t MipsGNULDBackend::getGP0(const Input& pInput) const {
  return m_GP0Map.lookup(&pInput);
}

void MipsGNULDBackend::defineGOTSymbol(IRBuilder& pBuilder) {
  // If we do not reserve any GOT entries, we do not need to re-define GOT
  // symbol.
  if (!m_pGOT->hasGOT1())
    return;

  // define symbol _GLOBAL_OFFSET_TABLE_
  if (m_pGOTSymbol != NULL) {
    pBuilder.AddSymbol<IRBuilder::Force, IRBuilder::Unresolve>(
        "_GLOBAL_OFFSET_TABLE_",
        ResolveInfo::Object,
        ResolveInfo::Define,
        ResolveInfo::Local,
        0x0,  // size
        0x0,  // value
        FragmentRef::Create(*(m_pGOT->begin()), 0x0),
        ResolveInfo::Hidden);
  } else {
    m_pGOTSymbol = pBuilder.AddSymbol<IRBuilder::Force, IRBuilder::Resolve>(
        "_GLOBAL_OFFSET_TABLE_",
        ResolveInfo::Object,
        ResolveInfo::Define,
        ResolveInfo::Local,
        0x0,  // size
        0x0,  // value
        FragmentRef::Create(*(m_pGOT->begin()), 0x0),
        ResolveInfo::Hidden);
  }
}

void MipsGNULDBackend::defineGOTPLTSymbol(IRBuilder& pBuilder) {
  // define symbol _PROCEDURE_LINKAGE_TABLE_
  if (m_pPLTSymbol != NULL) {
    pBuilder.AddSymbol<IRBuilder::Force, IRBuilder::Unresolve>(
        "_PROCEDURE_LINKAGE_TABLE_",
        ResolveInfo::Object,
        ResolveInfo::Define,
        ResolveInfo::Local,
        0x0,  // size
        0x0,  // value
        FragmentRef::Create(*(m_pPLT->begin()), 0x0),
        ResolveInfo::Hidden);
  } else {
    m_pPLTSymbol = pBuilder.AddSymbol<IRBuilder::Force, IRBuilder::Resolve>(
        "_PROCEDURE_LINKAGE_TABLE_",
        ResolveInfo::Object,
        ResolveInfo::Define,
        ResolveInfo::Local,
        0x0,  // size
        0x0,  // value
        FragmentRef::Create(*(m_pPLT->begin()), 0x0),
        ResolveInfo::Hidden);
  }
}

/// doCreateProgramHdrs - backend can implement this function to create the
/// target-dependent segments
void MipsGNULDBackend::doCreateProgramHdrs(Module& pModule) {
  // TODO
}

bool MipsGNULDBackend::relaxRelocation(IRBuilder& pBuilder, Relocation& pRel) {
  uint64_t sym_value = 0x0;

  LDSymbol* symbol = pRel.symInfo()->outSymbol();
  if (symbol->hasFragRef()) {
    uint64_t value = symbol->fragRef()->getOutputOffset();
    uint64_t addr = symbol->fragRef()->frag()->getParent()->getSection().addr();
    sym_value = addr + value;
  }

  Stub* stub = getStubFactory()->create(
      pRel, sym_value, pBuilder, *getBRIslandFactory());

  if (stub == NULL)
    return false;

  assert(stub->symInfo() != NULL);
  // increase the size of .symtab and .strtab
  LDSection& symtab = getOutputFormat()->getSymTab();
  LDSection& strtab = getOutputFormat()->getStrTab();
  symtab.setSize(symtab.size() + sizeof(llvm::ELF::Elf32_Sym));
  strtab.setSize(strtab.size() + stub->symInfo()->nameSize() + 1);

  return true;
}

bool MipsGNULDBackend::doRelax(Module& pModule,
                               IRBuilder& pBuilder,
                               bool& pFinished) {
  assert(getStubFactory() != NULL && getBRIslandFactory() != NULL);

  bool isRelaxed = false;

  for (Module::obj_iterator input = pModule.obj_begin();
       input != pModule.obj_end();
       ++input) {
    LDContext* context = (*input)->context();

    for (LDContext::sect_iterator rs = context->relocSectBegin();
         rs != context->relocSectEnd();
         ++rs) {
      LDSection* sec = *rs;

      if (LDFileFormat::Ignore == sec->kind() || !sec->hasRelocData())
        continue;

      for (RelocData::iterator reloc = sec->getRelocData()->begin();
           reloc != sec->getRelocData()->end();
           ++reloc) {
        if (llvm::ELF::R_MIPS_26 != reloc->type())
          continue;

        if (relaxRelocation(pBuilder, *llvm::cast<Relocation>(reloc)))
          isRelaxed = true;
      }
    }
  }

  SectionData* textData = getOutputFormat()->getText().getSectionData();

  // find the first fragment w/ invalid offset due to stub insertion
  Fragment* invalid = NULL;
  pFinished = true;
  for (BranchIslandFactory::iterator ii = getBRIslandFactory()->begin(),
                                     ie = getBRIslandFactory()->end();
       ii != ie;
       ++ii) {
    BranchIsland& island = *ii;
    if (island.end() == textData->end())
      break;

    Fragment* exit = island.end();
    if ((island.offset() + island.size()) > exit->getOffset()) {
      invalid = exit;
      pFinished = false;
      break;
    }
  }

  // reset the offset of invalid fragments
  while (invalid != NULL) {
    invalid->setOffset(invalid->getPrevNode()->getOffset() +
                       invalid->getPrevNode()->size());
    invalid = invalid->getNextNode();
  }

  // reset the size of .text
  if (isRelaxed)
    getOutputFormat()->getText().setSize(textData->back().getOffset() +
                                         textData->back().size());

  return isRelaxed;
}

bool MipsGNULDBackend::initTargetStubs() {
  if (getStubFactory() == NULL)
    return false;

  getStubFactory()->addPrototype(new MipsLA25Stub(*this));
  return true;
}

bool MipsGNULDBackend::readRelocation(const llvm::ELF::Elf32_Rel& pRel,
                                      Relocation::Type& pType,
                                      uint32_t& pSymIdx,
                                      uint32_t& pOffset) const {
  return GNULDBackend::readRelocation(pRel, pType, pSymIdx, pOffset);
}

bool MipsGNULDBackend::readRelocation(const llvm::ELF::Elf32_Rela& pRel,
                                      Relocation::Type& pType,
                                      uint32_t& pSymIdx,
                                      uint32_t& pOffset,
                                      int32_t& pAddend) const {
  return GNULDBackend::readRelocation(pRel, pType, pSymIdx, pOffset, pAddend);
}

bool MipsGNULDBackend::readRelocation(const llvm::ELF::Elf64_Rel& pRel,
                                      Relocation::Type& pType,
                                      uint32_t& pSymIdx,
                                      uint64_t& pOffset) const {
  uint64_t r_info = 0x0;
  if (llvm::sys::IsLittleEndianHost) {
    pOffset = pRel.r_offset;
    r_info = pRel.r_info;
  } else {
    pOffset = mcld::bswap64(pRel.r_offset);
    r_info = mcld::bswap64(pRel.r_info);
  }

  // MIPS 64 little endian (we do not support big endian now)
  // has a "special" encoding of r_info relocation
  // field. Instead of one 64 bit little endian number, it is a little
  // endian 32 bit number followed by a 32 bit big endian number.
  pType = mcld::bswap32(r_info >> 32);
  pSymIdx = r_info & 0xffffffff;
  return true;
}

bool MipsGNULDBackend::readRelocation(const llvm::ELF::Elf64_Rela& pRel,
                                      Relocation::Type& pType,
                                      uint32_t& pSymIdx,
                                      uint64_t& pOffset,
                                      int64_t& pAddend) const {
  uint64_t r_info = 0x0;
  if (llvm::sys::IsLittleEndianHost) {
    pOffset = pRel.r_offset;
    r_info = pRel.r_info;
    pAddend = pRel.r_addend;
  } else {
    pOffset = mcld::bswap64(pRel.r_offset);
    r_info = mcld::bswap64(pRel.r_info);
    pAddend = mcld::bswap64(pRel.r_addend);
  }

  pType = mcld::bswap32(r_info >> 32);
  pSymIdx = r_info & 0xffffffff;
  return true;
}

void MipsGNULDBackend::emitRelocation(llvm::ELF::Elf32_Rel& pRel,
                                      Relocation::Type pType,
                                      uint32_t pSymIdx,
                                      uint32_t pOffset) const {
  GNULDBackend::emitRelocation(pRel, pType, pSymIdx, pOffset);
}

void MipsGNULDBackend::emitRelocation(llvm::ELF::Elf32_Rela& pRel,
                                      Relocation::Type pType,
                                      uint32_t pSymIdx,
                                      uint32_t pOffset,
                                      int32_t pAddend) const {
  GNULDBackend::emitRelocation(pRel, pType, pSymIdx, pOffset, pAddend);
}

void MipsGNULDBackend::emitRelocation(llvm::ELF::Elf64_Rel& pRel,
                                      Relocation::Type pType,
                                      uint32_t pSymIdx,
                                      uint64_t pOffset) const {
  uint64_t r_info = mcld::bswap32(pType);
  r_info <<= 32;
  r_info |= pSymIdx;

  pRel.r_info = r_info;
  pRel.r_offset = pOffset;
}

void MipsGNULDBackend::emitRelocation(llvm::ELF::Elf64_Rela& pRel,
                                      Relocation::Type pType,
                                      uint32_t pSymIdx,
                                      uint64_t pOffset,
                                      int64_t pAddend) const {
  uint64_t r_info = mcld::bswap32(pType);
  r_info <<= 32;
  r_info |= pSymIdx;

  pRel.r_info = r_info;
  pRel.r_offset = pOffset;
  pRel.r_addend = pAddend;
}

bool MipsGNULDBackend::mergeSection(Module& pModule, const Input& pInput,
                                    LDSection& pSection) {
  if (pSection.flag() & llvm::ELF::SHF_MIPS_GPREL) {
    SectionData* sd = NULL;
    if (!m_psdata->hasSectionData()) {
      sd = IRBuilder::CreateSectionData(*m_psdata);
      m_psdata->setSectionData(sd);
    }
    sd = m_psdata->getSectionData();
    moveSectionData(*pSection.getSectionData(), *sd);
  } else {
    ObjectBuilder builder(pModule);
    builder.MergeSection(pInput, pSection);
  }
  return true;

}

void MipsGNULDBackend::moveSectionData(SectionData& pFrom, SectionData& pTo) {
  assert(&pFrom != &pTo && "Cannot move section data to itself!");

  uint64_t offset = pTo.getSection().size();
  AlignFragment* align = NULL;
  if (pFrom.getSection().align() > 1) {
    // if the align constraint is larger than 1, append an alignment
    unsigned int alignment = pFrom.getSection().align();
    align = new AlignFragment(/*alignment*/ alignment,
                              /*the filled value*/ 0x0,
                              /*the size of filled value*/ 1u,
                              /*max bytes to emit*/ alignment - 1);
    align->setOffset(offset);
    align->setParent(&pTo);
    pTo.getFragmentList().push_back(align);
    offset += align->size();
  }

  // move fragments from pFrom to pTO
  SectionData::FragmentListType& from_list = pFrom.getFragmentList();
  SectionData::FragmentListType& to_list = pTo.getFragmentList();
  SectionData::FragmentListType::iterator frag, fragEnd = from_list.end();
  for (frag = from_list.begin(); frag != fragEnd; ++frag) {
    frag->setParent(&pTo);
    frag->setOffset(offset);
    offset += frag->size();
  }
  to_list.splice(to_list.end(), from_list);

  // set up pTo's header
  pTo.getSection().setSize(offset);
}

//===----------------------------------------------------------------------===//
// Mips32GNULDBackend
//===----------------------------------------------------------------------===//
Mips32GNULDBackend::Mips32GNULDBackend(const LinkerConfig& pConfig,
                                       MipsGNUInfo* pInfo)
    : MipsGNULDBackend(pConfig, pInfo) {
}

bool Mips32GNULDBackend::initRelocator() {
  if (m_pRelocator == NULL)
    m_pRelocator = new Mips32Relocator(*this, config());

  return true;
}

void Mips32GNULDBackend::initTargetSections(Module& pModule,
                                            ObjectBuilder& pBuilder) {
  MipsGNULDBackend::initTargetSections(pModule, pBuilder);

  if (LinkerConfig::Object == config().codeGenType())
    return;

  ELFFileFormat* fileFormat = getOutputFormat();

  // initialize .got
  LDSection& got = fileFormat->getGOT();
  m_pGOT = new Mips32GOT(got);

  // initialize .got.plt
  LDSection& gotplt = fileFormat->getGOTPLT();
  m_pGOTPLT = new MipsGOTPLT(gotplt);

  // initialize .plt
  LDSection& plt = fileFormat->getPLT();
  m_pPLT = new MipsPLT(plt);
}

size_t Mips32GNULDBackend::getRelEntrySize() {
  return 8;
}

size_t Mips32GNULDBackend::getRelaEntrySize() {
  return 12;
}

//===----------------------------------------------------------------------===//
// Mips64GNULDBackend
//===----------------------------------------------------------------------===//
Mips64GNULDBackend::Mips64GNULDBackend(const LinkerConfig& pConfig,
                                       MipsGNUInfo* pInfo)
    : MipsGNULDBackend(pConfig, pInfo) {
}

bool Mips64GNULDBackend::initRelocator() {
  if (m_pRelocator == NULL)
    m_pRelocator = new Mips64Relocator(*this, config());

  return true;
}

void Mips64GNULDBackend::initTargetSections(Module& pModule,
                                            ObjectBuilder& pBuilder) {
  MipsGNULDBackend::initTargetSections(pModule, pBuilder);

  if (LinkerConfig::Object == config().codeGenType())
    return;

  ELFFileFormat* fileFormat = getOutputFormat();

  // initialize .got
  LDSection& got = fileFormat->getGOT();
  m_pGOT = new Mips64GOT(got);

  // initialize .got.plt
  LDSection& gotplt = fileFormat->getGOTPLT();
  m_pGOTPLT = new MipsGOTPLT(gotplt);

  // initialize .plt
  LDSection& plt = fileFormat->getPLT();
  m_pPLT = new MipsPLT(plt);
}

size_t Mips64GNULDBackend::getRelEntrySize() {
  return 16;
}

size_t Mips64GNULDBackend::getRelaEntrySize() {
  return 24;
}

//===----------------------------------------------------------------------===//
/// createMipsLDBackend - the help funtion to create corresponding MipsLDBackend
///
static TargetLDBackend* createMipsLDBackend(const LinkerConfig& pConfig) {
  const llvm::Triple& triple = pConfig.targets().triple();

  if (triple.isOSDarwin()) {
    assert(0 && "MachO linker is not supported yet");
  }
  if (triple.isOSWindows()) {
    assert(0 && "COFF linker is not supported yet");
  }

  llvm::Triple::ArchType arch = triple.getArch();

  if (llvm::Triple::mips64el == arch)
    return new Mips64GNULDBackend(pConfig, new MipsGNUInfo(triple));

  assert(arch == llvm::Triple::mipsel);
  return new Mips32GNULDBackend(pConfig, new MipsGNUInfo(triple));
}

}  // namespace mcld

//===----------------------------------------------------------------------===//
// Force static initialization.
//===----------------------------------------------------------------------===//
extern "C" void MCLDInitializeMipsLDBackend() {
  mcld::TargetRegistry::RegisterTargetLDBackend(mcld::TheMipselTarget,
                                                mcld::createMipsLDBackend);
  mcld::TargetRegistry::RegisterTargetLDBackend(mcld::TheMips64elTarget,
                                                mcld::createMipsLDBackend);
}
