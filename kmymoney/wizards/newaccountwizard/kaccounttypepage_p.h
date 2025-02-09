/*
    SPDX-FileCopyrightText: 2007 Thomas Baumgart <Thomas Baumgart <ipwizard@users.sourceforge.net>>
    SPDX-FileCopyrightText: 2017 Łukasz Wojniłowicz <lukasz.wojnilowicz@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KACCOUNTTYPEPAGE_P_H
#define KACCOUNTTYPEPAGE_P_H

// ----------------------------------------------------------------------------
// QT Includes

// ----------------------------------------------------------------------------
// KDE Includes

// ----------------------------------------------------------------------------
// Project Includes

#include "ui_kaccounttypepage.h"

#include "wizardpage_p.h"

namespace NewAccountWizard
{
class Wizard;

class AccountTypePagePrivate : public WizardPagePrivate<Wizard>
{
    Q_DISABLE_COPY(AccountTypePagePrivate)

public:
    explicit AccountTypePagePrivate(QObject* parent) :
        WizardPagePrivate<Wizard>(parent),
        ui(new Ui::KAccountTypePage),
        m_showPriceWarning(false)
    {
    }

    ~AccountTypePagePrivate()
    {
        delete ui;
    }

    Ui::KAccountTypePage *ui;
    bool                  m_showPriceWarning;
    QString               m_parentAccountId;
};
}

#endif
