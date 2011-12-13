//===- DynObjReader.h -----------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef MCLD_DYNAMIC_SHARED_OBJECT_READER_H
#define MCLD_DYNAMIC_SHARED_OBJECT_READER_H
#ifdef ENABLE_UNITTEST
#include <gtest.h>
#endif
#include "mcld/LD/LDReader.h"
#include "llvm/Support/system_error.h"

namespace mcld
{

class TargetLDBackend;
class Input;

/** \class DynObjReader
 *  \brief DynObjReader provides an common interface for different object
 *  formats.
 */
class DynObjReader : public LDReader
{

protected:
  DynObjReader(TargetLDBackend& pLDBackend)
  : m_TargetLDBackend(pLDBackend)
  { }

public:
  virtual ~DynObjReader() { }

  virtual llvm::error_code readDSO(Input& pFile) = 0;

  TargetLDBackend& target()
  { return m_TargetLDBackend; }

  const TargetLDBackend& target() const
  { return m_TargetLDBackend; }
  
private:
  TargetLDBackend& m_TargetLDBackend;

};

} // namespace of mcld

#endif

