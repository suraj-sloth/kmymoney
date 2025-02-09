/*
    SPDX-FileCopyrightText: 2002-2004 Kevin Tambascio <ktambascio@users.sourceforge.net>
    SPDX-FileCopyrightText: 2003-2019 Thomas Baumgart <tbaumgart@kde.org>
    SPDX-FileCopyrightText: 2004-2005 Ace Jones <acejones@users.sourceforge.net>
    SPDX-FileCopyrightText: 2009-2010 Alvaro Soliverez <asoliverez@kde.org>
    SPDX-FileCopyrightText: 2017 Łukasz Wojniłowicz <lukasz.wojnilowicz@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/


#ifndef KINVESTMENTVIEW_P_H
#define KINVESTMENTVIEW_P_H

#include "kinvestmentview.h"

// ----------------------------------------------------------------------------
// QT Includes

#include <QIcon>

// ----------------------------------------------------------------------------
// KDE Includes

#include <KSharedConfig>
#include <KExtraColumnsProxyModel>

// ----------------------------------------------------------------------------
// Project Includes

#include "ui_kinvestmentview.h"
#include "kmymoneyviewbase_p.h"

#include "mymoneyexception.h"
#include "mymoneyfile.h"
#include "mymoneysecurity.h"
#include "mymoneyaccount.h"
#include "kmymoneysettings.h"
#include "kmymoneyaccountcombo.h"
#include "accountsmodel.h"
#include "equitiesmodel.h"
#include "securitiesmodel.h"
#include "icons.h"
#include "mymoneyenums.h"
#include "columnselector.h"

using namespace Icons;

namespace eView {
namespace Investment {
enum Tab { Equities = 0, Securities };
}
}

class KInvestmentViewPrivate : public KMyMoneyViewBasePrivate
{
    Q_DECLARE_PUBLIC(KInvestmentView)

public:
    explicit KInvestmentViewPrivate(KInvestmentView *qq)
        : KMyMoneyViewBasePrivate(qq)
        , ui(new Ui::KInvestmentView)
        , m_idInvAcc(QString())
        , m_needLoad(true)
        , m_accountsProxyModel(nullptr)
        , m_equitiesProxyModel(nullptr)
        , m_securitiesProxyModel(nullptr)
        , m_securityColumnSelector(nullptr)
        , m_equityColumnSelector(nullptr)
    {
    }

    ~KInvestmentViewPrivate()
    {
        /// @todo port to new model code
#if 0
        if (!m_needLoad) {
            // save the header state of the equities list
            auto cfgGroup = KSharedConfig::openConfig()->group("KInvestmentView_Equities");
            auto cfgHeader = ui->m_equitiesTree->header()->saveState();
            auto visEColumns = m_equitiesProxyModel->getVisibleColumns();

            QList<int> cfgColumns;
            foreach (const auto visColumn, visEColumns)
                cfgColumns.append(static_cast<int>(visColumn));

            cfgGroup.writeEntry("HeaderState", cfgHeader);
            cfgGroup.writeEntry("ColumnsSelection", cfgColumns);

            // save the header state of the securities list
            cfgGroup = KSharedConfig::openConfig()->group("KInvestmentView_Securities");
            cfgHeader = ui->m_securitiesTree->header()->saveState();
            auto visSColumns = m_securitiesProxyModel->getVisibleColumns();
            cfgColumns.clear();
            foreach (const auto visColumn, visSColumns)
                cfgColumns.append(static_cast<int>(visColumn));

            cfgGroup.writeEntry("HeaderState", cfgHeader);
            cfgGroup.writeEntry("ColumnsSelection", cfgColumns);
        }
#endif
        delete ui;
    }

    void init()
    {
        Q_Q(KInvestmentView);
        m_needLoad = false;
        ui->setupUi(q);

        // Equities tab
        m_accountsProxyModel = new AccountNamesFilterProxyModel(q);
        m_accountsProxyModel->setObjectName("m_accountsProxyModel");
        m_accountsProxyModel->addAccountType(eMyMoney::Account::Type::Investment);
        m_accountsProxyModel->setHideEquityAccounts(false);
        m_accountsProxyModel->setSourceModel(MyMoneyFile::instance()->accountsModel());
        m_accountsProxyModel->sort(AccountsModel::Column::AccountName);
        ui->m_accountComboBox->setModel(m_accountsProxyModel);
        ui->m_accountComboBox->expandAll();

        auto extraColumnModel = new EquitiesModel(q);
        extraColumnModel->setObjectName("extraColumnModel");
        extraColumnModel->setSourceModel(MyMoneyFile::instance()->accountsModel());

        m_equitiesProxyModel = new AccountsProxyModel(q);
        m_equitiesProxyModel->setObjectName("m_equitiesProxyModel");
        m_equitiesProxyModel->clear();
        m_equitiesProxyModel->addAccountType(eMyMoney::Account::Type::Stock);
        m_equitiesProxyModel->setHideEquityAccounts(false);
        m_equitiesProxyModel->setHideAllEntries(true);
        m_equitiesProxyModel->setSourceModel(extraColumnModel);
        m_equitiesProxyModel->sort(AccountsModel::Column::AccountName);

        ui->m_equitiesTree->setModel(m_equitiesProxyModel);

        QVector<int> equityColumns( {
            extraColumnModel->proxyColumnForExtraColumn(EquitiesModel::Column::Symbol),
            extraColumnModel->proxyColumnForExtraColumn(EquitiesModel::Column::Value),
            extraColumnModel->proxyColumnForExtraColumn(EquitiesModel::Column::Quantity),
            extraColumnModel->proxyColumnForExtraColumn(EquitiesModel::Column::Price),
        });

        m_equityColumnSelector = new ColumnSelector(ui->m_equitiesTree,
                QStringLiteral("KInvestmentView_Equities"),
                extraColumnModel->proxyColumnForExtraColumn(EquitiesModel::Column::Symbol)-1,
                equityColumns);
        m_equityColumnSelector->setModel(m_equitiesProxyModel);

        m_equityColumnSelector->setAlwaysVisible(QVector<int>({ AccountsModel::Column::AccountName }));

        QVector<int> columns;
        columns = m_equityColumnSelector->columns();

        int colIdx;
        for (auto col : equityColumns) {
            colIdx = columns.indexOf(col);
            if (colIdx != -1)
                columns.remove(colIdx);
        }
        colIdx = columns.indexOf(AccountsModel::Column::AccountName);
        if (colIdx != -1)
            columns.remove(colIdx);

        m_equityColumnSelector->setAlwaysHidden(columns);
        m_equityColumnSelector->setSelectable(equityColumns);


        q->connect(ui->m_equitiesTree, &QWidget::customContextMenuRequested, q, &KInvestmentView::slotInvestmentMenuRequested);
        q->connect(ui->m_equitiesTree->selectionModel(), &QItemSelectionModel::currentRowChanged, q, &KInvestmentView::slotEquitySelected);
        q->connect(ui->m_equitiesTree, &QTreeView::doubleClicked, q, &KInvestmentView::slotEditInvestment);

        // Securities tab
        m_securitiesProxyModel = new QSortFilterProxyModel(q);
        ui->m_securitiesTree->setModel(m_securitiesProxyModel);
        m_securitiesProxyModel->setSourceModel(MyMoneyFile::instance()->securitiesModel());
        m_securitiesProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

        m_securityColumnSelector = new ColumnSelector(ui->m_securitiesTree, QStringLiteral("KInvestmentView_Securities"));
        m_securityColumnSelector->setModel(MyMoneyFile::instance()->securitiesModel());
        m_securityColumnSelector->setAlwaysVisible(QVector<int>({0}));
        m_securityColumnSelector->setSelectable(m_securityColumnSelector->columns());

        ui->m_deleteSecurityButton->setIcon(Icons::get(Icon::EditRemove));
        ui->m_deleteSecurityButton->setEnabled(false);
        ui->m_editSecurityButton->setIcon(Icons::get(Icon::DocumentEdit));
        ui->m_editSecurityButton->setEnabled(false);

        q->connect(ui->m_searchSecurities, &QLineEdit::textChanged, m_securitiesProxyModel, &QSortFilterProxyModel::setFilterFixedString);
        q->connect(ui->m_securitiesTree->selectionModel(), &QItemSelectionModel::currentRowChanged, q, &KInvestmentView::slotSecuritySelected);
        q->connect(ui->m_editSecurityButton, &QAbstractButton::clicked, q, &KInvestmentView::slotEditSecurity);
        q->connect(ui->m_deleteSecurityButton, &QAbstractButton::clicked, q, &KInvestmentView::slotDeleteSecurity);

        // Investment Page
        m_needReload[eView::Investment::Tab::Equities] = m_needReload[eView::Investment::Tab::Securities] = true;
        q->connect(MyMoneyFile::instance(), &MyMoneyFile::dataChanged, q, &KInvestmentView::refresh);

        q->connect(ui->m_accountComboBox, &KMyMoneyAccountCombo::accountSelected, q, &KInvestmentView::slotLoadAccount);

    }

    /**
      * This slot is used to programatically preselect default account in investment view
      */
    void selectDefaultInvestmentAccount()
    {
        Q_Q(KInvestmentView);
        if (m_accountsProxyModel->rowCount() > 0) {
            /// @todo port to new model code
#if 0
            auto firsitem = m_accountsProxyModel->index(0, 0, QModelIndex());
            if (m_accountsProxyModel->hasChildren(firsitem)) {
                auto seconditem = m_accountsProxyModel->index(0, 0, firsitem);
                q->slotSelectAccount(seconditem.data(EquitiesModel::EquityID).toString());
            }
#endif
        }
    }

    /**
      * This slots returns security currently selected in tree view
      */
    MyMoneySecurity currentSecurity()
    {
        /// @todo port to new model code
        MyMoneySecurity sec;
        auto treeItem = ui->m_securitiesTree->currentIndex();
        if (treeItem.isValid()) {
            auto mdlItem = m_securitiesProxyModel->index(treeItem.row(), SecuritiesModel::Security, treeItem.parent());
            sec = MyMoneyFile::instance()->security(mdlItem.data(eMyMoney::Model::IdRole).toString());
        }
        return sec;
    }

    /**
      * This slots returns equity currently selected in tree view
      */
    MyMoneyAccount currentEquity()
    {
        QModelIndex idx = MyMoneyFile::baseModel()->mapToBaseSource(ui->m_equitiesTree->currentIndex());
        return MyMoneyFile::instance()->accountsModel()->itemByIndex(idx);
    }

    Ui::KInvestmentView *ui;
    QString             m_idInvAcc;

    /**
      * This member holds the load state of page
      */
    bool m_needLoad;

    bool m_needReload[2];
    AccountNamesFilterProxyModel* m_accountsProxyModel;
    AccountsProxyModel*           m_equitiesProxyModel;
    // EquitiesFilterProxyModel*     m_equitiesProxyModel;
    QSortFilterProxyModel*        m_securitiesProxyModel;
    ColumnSelector*               m_securityColumnSelector;
    ColumnSelector*               m_equityColumnSelector;
    MyMoneyAccount                m_currentEquity;
};

#endif
