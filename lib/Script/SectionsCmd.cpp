//===- SectionsCmd.cpp ----------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <mcld/Script/SectionsCmd.h>
#include <mcld/Support/raw_ostream.h>
#include <cassert>

using namespace mcld;

//===----------------------------------------------------------------------===//
// SectionsCmd
//===----------------------------------------------------------------------===//
SectionsCmd::SectionsCmd()
  : ScriptCommand(ScriptCommand::Sections)
{
}

SectionsCmd::~SectionsCmd()
{
  for (iterator it = begin(), ie = end(); it != ie; ++it) {
    if (*it != NULL)
      delete *it;
  }
}

void SectionsCmd::dump() const
{
  mcld::outs() << "SECTIONS\n{\n";

  for (const_iterator it = begin(), ie = end(); it != ie; ++it) {
    switch ((*it)->getKind()) {
    case ScriptCommand::Entry:
    case ScriptCommand::Assignment:
    case ScriptCommand::OutputSectDesc:
      mcld::outs() << "\t";
      (*it)->dump();
      break;
    default:
      assert(0);
      break;
    }
  }

  mcld::outs() << "}\n";
}

void SectionsCmd::push_back(ScriptCommand* pCommand)
{
  switch (pCommand->getKind()) {
  case ScriptCommand::Entry:
  case ScriptCommand::Assignment:
  case ScriptCommand::OutputSectDesc:
    m_SectionCommands.push_back(pCommand);
    break;
  default:
    assert(0);
    break;
  }
}

void SectionsCmd::activate()
{
  for (const_iterator it = begin(), ie = end(); it != ie; ++it) {
    switch ((*it)->getKind()) {
    case ScriptCommand::Entry:
    case ScriptCommand::Assignment:
    case ScriptCommand::OutputSectDesc:
      (*it)->activate();
      break;
    default:
      assert(0);
      break;
    }
  }
}
