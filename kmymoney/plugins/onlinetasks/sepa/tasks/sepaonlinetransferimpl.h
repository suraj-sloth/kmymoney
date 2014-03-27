/*
  This file is part of KMyMoney, A Personal Finance Manager for KDE
  Copyright (C) 2013 Christian Dávid <christian-david@web.de>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SEPAONLINETRANSFERIMPL_H
#define SEPAONLINETRANSFERIMPL_H

#include <algorithm>

#include <klocalizedstring.h>

#include "sepaonlinetransfer.h"

#include "swiftaccountidentifier.h"

/**
 * @brief SEPA Credit Transfer
 */
class sepaOnlineTransferImpl : public sepaOnlineTransfer
{
  Q_INTERFACES(sepaOnlineTransfer)

public:
  ONLINETASK_META(sepaOnlineTransfer, "org.kmymoney.creditTransfer.sepa");
  sepaOnlineTransferImpl();
  sepaOnlineTransferImpl(const sepaOnlineTransferImpl &other );

  QString responsibleAccount() const { return _originAccount; }
  void setOriginAccount( const QString& accountId );
  
  MyMoneyMoney value() const { return _value; }
  virtual void setValue(MyMoneyMoney value) { _value = value; }

  void setRecipient ( const swiftAccountIdentifier& accountIdentifier ) { _remoteAccount = accountIdentifier; }
  const sepaAccountIdentifier& getRecipient() const { return _remoteAccount; }

  virtual void setPurpose( const QString purpose ) { _purpose = purpose; }
  QString purpose() const { return _purpose; }

  virtual void setEndToEndReference( const QString& reference ) { _endToEndReference = reference; }
  QString endToEndReference() const { return _endToEndReference; }

  sepaAccountIdentifier* originAccountIdentifier() const;

  MyMoneySecurity currency() const;

  bool isValid() const;

  QString jobTypeName() const { return i18n("SEPA Credit Transfer"); }

  unsigned short int textKey() const { return _textKey; }
  unsigned short int subTextKey() const { return _subTextKey; }
  
  virtual bool hasReferenceTo(const QString& id) const;

  QSharedPointer<const sepaOnlineTransfer::settings> getSettings() const;
  
protected:
  sepaOnlineTransfer* clone() const;
  
  virtual sepaOnlineTransfer* createFromXml(const QDomElement &element) const;
  virtual void writeXML(QDomDocument& document, QDomElement& parent) const;

private:
  mutable QSharedPointer<const settings> _settings;
  
  QString _originAccount;
  MyMoneyMoney _value;
  QString _purpose;
  QString _endToEndReference;

  sepaAccountIdentifier _remoteAccount;

  unsigned short int _textKey;
  unsigned short int _subTextKey;
};

#endif // SEPAONLINETRANSFERIMPL_H
