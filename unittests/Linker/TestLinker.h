//===- TestLinker.h -------------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef MCLD_TESTLINKER_TEST_H
#define MCLD_TESTLINKER_TEST_H

#include <gtest.h>

#include <string>

#include <mcld/MC/MCLDDriver.h>
#include <mcld/MC/MCLinker.h>
#include <mcld/LD/DiagnosticPrinter.h>
#include <mcld/LD/DiagnosticLineInfo.h>
#include <mcld/Support/TargetRegistry.h>

namespace mcld {

class MCLDInfo;
class TargetLDBackend;
class RegionFactory;

namespace test
{

class TestLinker
{
public:
  TestLinker();

  ~TestLinker();

  bool initialize(const std::string &pTriple);

  MCLDInfo* config() {
    assert(NULL != m_pInfo);
    return m_pInfo;
  }

  const MCLDInfo* config() const {
    assert(NULL != m_pInfo);
    return m_pInfo;
  }

  void addObject(const std::string &pPath);

  void addObject(void* pMemBuffer, size_t pSize);

  void addObject(int pFileHandler);

  bool setOutput(const std::string &pPath);

  bool setOutput(int pFileHandler);

  /// getDriver
  MCLDDriver* getDriver() {
    assert(NULL != m_pDriver);
    return m_pDriver;
  }

  /// getDriver
  const MCLDDriver* getDriver() const {
    assert(NULL != m_pDriver);
    return m_pDriver;
  }

  /// getLinker
  MCLinker* getLinker() {
    assert(NULL != m_pDriver);
    return m_pDriver->getLinker();
  }

  /// getLinker
  const MCLinker* getLinker() const {
    assert(NULL != m_pDriver);
    return m_pDriver->getLinker();
  }

private:
  void advanceRoot();

private:
  const mcld::Target* m_pTarget;
  mcld::MCLDDriver *m_pDriver;
  mcld::MCLDInfo* m_pInfo;
  mcld::DiagnosticLineInfo* m_pDiagLineInfo;
  mcld::DiagnosticPrinter* m_pDiagPrinter;
  mcld::TargetLDBackend* m_pBackend;
  mcld::InputTree::iterator m_Root;
  mcld::RegionFactory* m_pRegionFactory;

};

} // namespace of test
} // namespace of mcld
#endif
