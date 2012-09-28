//===- THMToARMStub.h -----------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef MCLD_ARM_THMToARMStub_H
#define MCLD_ARM_THMToARMStub_H
#ifdef ENABLE_UNITTEST
#include <gtest.h>
#endif

#include <llvm/Support/DataTypes.h>
#include <mcld/Fragment/Stub.h>
#include <string>
#include <vector>

namespace mcld
{

class Relocation;
class ResolveInfo;

/** \class THMToARMStub
 *  \brief ARM stub for long call from ARM source to ARM target
 *
 */
class THMToARMStub : public Stub
{
public:
  THMToARMStub();

  ~THMToARMStub();

  // isMyDuty
  bool isMyDuty(const class Relocation& pReloc,
                uint64_t pSource,
                uint64_t pTargetSymValue) const;

  // observers
  const std::string& name() const;

  const uint8_t* getContent() const;

  size_t size() const;

  size_t alignment() const;

  // for T bit of this stub
  uint64_t initSymValue() const;

private:
  THMToARMStub(const THMToARMStub&);

  THMToARMStub& operator=(const THMToARMStub&);

  /// for doClone
  THMToARMStub(bool pIsOutputPIC);

  /// doClone
  Stub* doClone(bool pIsOutputPIC);

private:
  std::string m_Name;
  static const uint32_t PIC_TEMPLATE[];
  static const uint32_t TEMPLATE[];
  const uint32_t* m_pData;
  size_t m_Size;
};

} // namespace of mcld

#endif