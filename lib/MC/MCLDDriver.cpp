/*****************************************************************************
 *   The mcld Project, Copyright (C), 2011 -                                 *
 *   Embedded and Web Computing Lab, National Taiwan University              *
 *   MediaTek, Inc.                                                          *
 *                                                                           *
 *   Jush Lu <jush.msn@mediatek.com>                                         *
 *   Luba Tang <luba.tang@mediatek.com>                                      *
 ****************************************************************************/
#include <mcld/MC/MCLDDriver.h>
#include <mcld/Target/TargetLDBackend.h>

using namespace mcld;

MCLDDriver::MCLDDriver(TargetLDBackend& pLDBackend)
  : m_LDBackend(pLDBackend) {
}

MCLDDriver::~MCLDDriver()
{
}

