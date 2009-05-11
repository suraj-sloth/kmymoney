/***************************************************************************
                          ksettingsfonts.h
                             -------------------
    copyright            : (C) 2005 by Thomas Baumgart
    email                : ipwizard@users.sourceforge.net
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef KSETTINGSFONTS_H
#define KSETTINGSFONTS_H

// ----------------------------------------------------------------------------
// QT Includes

// ----------------------------------------------------------------------------
// KDE Includes

// ----------------------------------------------------------------------------
// Project Includes

#include "kmymoney2/dialogs/settings/ksettingsfontsdecl.h"

class KSettingsFonts : public KSettingsFontsDecl
{
  Q_OBJECT

public:
  KSettingsFonts(QWidget* parent = 0, const char* name = 0);
  ~KSettingsFonts();
};
#endif

