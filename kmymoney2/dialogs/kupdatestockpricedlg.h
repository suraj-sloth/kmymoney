/***************************************************************************
                          kupdatestockpricedlg.h  -  description
                             -------------------
    begin                : Thu Feb 7 2002
    copyright            : (C) 2000-2002 by Michael Edwardes
    email                : mte@users.sourceforge.net
                           Javier Campos Morales <javi_c@users.sourceforge.net>
                           Felix Rodriguez <frodriguez@users.sourceforge.net>
                           John C <thetacoturtle@users.sourceforge.net>
                           Thomas Baumgart <ipwizard@users.sourceforge.net>
                           Kevin Tambascio <ktambascio@users.sourceforge.net>
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


#ifndef KUPDATESTOCKPRICEDLG_H
#define KUPDATESTOCKPRICEDLG_H

#include "config-kmymoney.h"

// ----------------------------------------------------------------------------
// QT Includes

#include <qdatetime.h>

// ----------------------------------------------------------------------------
// KDE Includes

#include <klocale.h>
#include <kpushbutton.h>

// ----------------------------------------------------------------------------
// Project Includes

#include "ui_kupdatestockpricedlgdecl.h"

#include <kmymoney/mymoneyprice.h>
#include <kmymoney/kmymoneydateinput.h>
#include <kmymoney/kmymoneyedit.h>

/**
  * @author Kevin Tambascio
  */
class kUpdateStockPriceDecl : public QDialog, public Ui::kUpdateStockPriceDecl
{
public:
  kUpdateStockPriceDecl( QWidget *parent ) : QDialog( parent ) {
    setupUi( this );
  }
};

class KUpdateStockPriceDlg : public kUpdateStockPriceDecl
{
  Q_OBJECT

public:
  KUpdateStockPriceDlg(QWidget* parent = NULL);
  ~KUpdateStockPriceDlg();

  const QDate date() const { return m_date->date(); };
  const MyMoneyMoney price(void) const;

public slots:
  int exec(void);

protected slots:
  void slotCheckData(void);

private:
  void init();

};

#endif
