/*
 * This file is part of KMyMoney, A Personal Finance Manager for KDE
 * Copyright (C) 2014 Christian Dávid <christian-david@web.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PAYEEIDENTIFIERUNAVAILABLE_H
#define PAYEEIDENTIFIERUNAVAILABLE_H

#include "payeeidentifier/payeeidentifierdata.h"
#include <QDomElement>

class payeeIdentifierLoader;

namespace payeeIdentifiers {

/**
 * @brief A payeeIdentifier which is used to store the plain xml data
 *
 * To avoid data loss if a plugin could not be loaded this payeeIdentifier is used in the xml backend.
 * It stores the data in plain xml so it can be written back to the file.
 */
class payeeIdentifierUnavailable : public payeeIdentifierData
{
public:
  PAYEEIDENTIFIER_IID(payeeIdentifierUnavailable, "org.kmymoney.payeeIdentifier.payeeIdentifierUnavailable");

  payeeIdentifierUnavailable();
  virtual void writeXML(QDomDocument& document, QDomElement& parent) const;
  virtual payeeIdentifierUnavailable* createFromXml(const QDomElement& element) const;
  virtual bool isValid() const;
  virtual bool operator==(const payeeIdentifierData& other) const;
  bool operator==(const payeeIdentifierUnavailable& other) const;

  friend class payeeIdentifierLoader;
  /** @todo make private */
  payeeIdentifierUnavailable( QDomElement data );

protected:
  virtual payeeIdentifierUnavailable* clone() const;

private:
  QDomElement m_data;
};

} // namespace payeeidentifiers

#endif // PAYEEIDENTIFIERUNAVAILABLE_H
