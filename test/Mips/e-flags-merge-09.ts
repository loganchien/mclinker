# Check that the linker shows an error and does not link files
# with mips32r2 and mips64 instructions sets.

# RUN: yaml2obj -format=elf -docnum 1 %s > %t-32r2.o
# RUN: yaml2obj -format=elf -docnum 2 %s > %t-64.o

# RUN: not %MCLinker -mtriple=mipsel-linux-gnu -shared -o %t.so \
# RUN:               %t-32r2.o %t-64.o 2>&1 | FileCheck %s

# CHECK: target arch 'mips32r2' is inconsist with the 'mips64' in e-flags-merge-09

# 32r2.o
---
FileHeader:
  Class:    ELFCLASS32
  Data:     ELFDATA2LSB
  Type:     ET_REL
  Machine:  EM_MIPS
  Flags:    [EF_MIPS_ABI_O32, EF_MIPS_ARCH_32R2]

Sections:
  - Name:          .text
    Type:          SHT_PROGBITS
    Flags:         [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign:  4
    Size:          4

# 64.o
---
FileHeader:
  Class:    ELFCLASS32
  Data:     ELFDATA2LSB
  Type:     ET_REL
  Machine:  EM_MIPS
  Flags:    [EF_MIPS_ABI_O32, EF_MIPS_ARCH_64, EF_MIPS_32BITMODE]

Sections:
  - Name:          .text
    Type:          SHT_PROGBITS
    Flags:         [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign:  4
    Size:          4
...
