# Check that the linker shows an error when object file has missed
# or unsupported ASE flags.

# RUN: yaml2obj -format=elf -docnum 2 %s > %t-mips16.o
# RUN: not %MCLinker -mtriple=mipsel-linux-gnu -e T -o %t.exe %t-mips16.o 2>&1 \
# RUN:   | FileCheck -check-prefix=MIPS16 %s

# MIPS16: MIPS16 extension is unsupported: e-flags-merge-01.ts.tmp-mips16

# no-abi.o
---
FileHeader:
  Class:           ELFCLASS32
  Data:            ELFDATA2LSB
  Type:            ET_REL
  Machine:         EM_MIPS
  Flags:           []

Sections:
  - Name:          .text
    Type:          SHT_PROGBITS
    Flags:         [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign:  0x04
    Size:          0x04

Symbols:
  Global:
    - Name:        T
      Section:     .text

# mips16.o
---
FileHeader:
  Class:           ELFCLASS32
  Data:            ELFDATA2LSB
  Type:            ET_REL
  Machine:         EM_MIPS
  Flags:           [EF_MIPS_ABI_O32, EF_MIPS_ARCH_32, EF_MIPS_ARCH_ASE_M16]

Sections:
  - Name:          .text
    Type:          SHT_PROGBITS
    Flags:         [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign:  0x04
    Size:          0x04

Symbols:
  Global:
    - Name:        T
      Section:     .text
...
