/*
    SPDX-FileCopyrightText: 2017-2018 Łukasz Wojniłowicz <lukasz.wojnilowicz@gmail.com>
    SPDX-FileCopyrightText: 2019 Thomas Baumgart <tbaumgart@kde.org>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "securitiesfilterproxymodel.h"

// ----------------------------------------------------------------------------
// QT Includes

#include <QMenu>

// ----------------------------------------------------------------------------
// KDE Includes

#include <KLocalizedString>

// ----------------------------------------------------------------------------
// Project Includes

#include "mymoneyfile.h"
#include "mymoneysecurity.h"
#include "mymoneyenums.h"

/// @todo cleanup
#if 0
class SecuritiesModel::Private
{
public:
    Private() :
        m_file(MyMoneyFile::instance()),
        m_ndCurrencies(nullptr),
        m_ndSecurities(nullptr)
    {
        QVector<Column> columns {
            Column::Security, Column::Symbol, Column::Type,
            Column::Market, Column::Currency, Column::Fraction
        };
        foreach (auto const column, columns)
            m_columns.append(column);
    }

    ~Private() {}

    void loadSecurity(QStandardItem *node, const MyMoneySecurity &sec)
    {
        auto itSec = new QStandardItem(sec.name());
        node->appendRow(itSec);
        itSec->setEditable(false);
        setSecurityData(node, itSec->row(), sec, m_columns);
    }

    void setSecurityData(QStandardItem *node, const int row, const MyMoneySecurity &security, const QList<Column> &columns)
    {
        QStandardItem *cell;

        auto getCell = [&, row](const auto column) {
            cell = node->child(row, column);      // try to get QStandardItem
            if (!cell) {                          // it may be uninitialized
                cell = new QStandardItem;           // so create one
                node->setChild(row, column, cell);  // and add it under the node
                cell->setEditable(false);           // and don't forget that it's non-editable
            }
        };

        auto colNum = m_columns.indexOf(Column::Security);
        if (colNum == -1)
            return;

        // Security
        getCell(colNum);
        if (columns.contains(Column::Security)) {
            cell->setData(security.name(), Qt::DisplayRole);
            cell->setData(security.id(), Qt::UserRole);
        }

        // Symbol
        if (columns.contains(Column::Symbol)) {
            colNum = m_columns.indexOf(Column::Symbol);
            if (colNum != -1) {
                getCell(colNum);
                cell->setData(security.tradingSymbol(), Qt::DisplayRole);
            }
        }

        // Type
        if (columns.contains(Column::Type)) {
            colNum = m_columns.indexOf(Column::Type);
            if (colNum != -1) {
                getCell(colNum);
                cell->setData(security.securityTypeToString(security.securityType()), Qt::DisplayRole);
            }
        }

        // Market
        if (columns.contains(Column::Market)) {
            colNum = m_columns.indexOf(Column::Market);
            if (colNum != -1) {
                getCell(colNum);
                QString market;
                if (security.isCurrency())
                    market = QLatin1String("ISO 4217");
                else
                    market = security.tradingMarket();
                cell->setData(market, Qt::DisplayRole);
            }
        }

        // Currency
        if (columns.contains(Column::Currency)) {
            colNum = m_columns.indexOf(Column::Currency);
            if (colNum != -1) {
                getCell(colNum);
                MyMoneySecurity tradingCurrency;
                if (!security.isCurrency())
                    tradingCurrency = m_file->security(security.tradingCurrency());
                cell->setData(tradingCurrency.tradingSymbol(), Qt::DisplayRole);
            }
        }

        // Fraction
        if (columns.contains(Column::Fraction)) {
            colNum = m_columns.indexOf(Column::Fraction);
            if (colNum != -1) {
                getCell(colNum);
                cell->setData(QString::number(security.smallestAccountFraction()), Qt::DisplayRole);
            }
        }
    }

    QStandardItem *itemFromSecurityId(QStandardItemModel *model, const QString &securityId)
    {
        const auto itemList = model->match(model->index(0, 0), Qt::UserRole, QVariant(securityId), 1, Qt::MatchFlags(Qt::MatchExactly | Qt::MatchCaseSensitive | Qt::MatchRecursive));
        if (!itemList.isEmpty())
            return model->itemFromIndex(itemList.first());
        return nullptr;
    }

    MyMoneyFile *m_file;
    QList<SecuritiesModel::Column> m_columns;
    QStandardItem *m_ndCurrencies;
    QStandardItem *m_ndSecurities;
};

SecuritiesModel::SecuritiesModel(QObject *parent)
    : QStandardItemModel(parent), d(new Private)
{
    init();
}

SecuritiesModel::~SecuritiesModel()
{
    delete d;
}

void SecuritiesModel::init()
{
    QStringList headerLabels;
    foreach (const auto column, d->m_columns)
        headerLabels.append(getHeaderName(column));
    setHorizontalHeaderLabels(headerLabels);
}

void SecuritiesModel::load()
{
    this->blockSignals(true);

    auto rootItem = invisibleRootItem();
    auto secList = d->m_file->securityList();   // get all available securities
    d->m_ndSecurities = new QStandardItem(QStringLiteral("Securities"));
    d->m_ndSecurities->setEditable(false);
    rootItem->appendRow(d->m_ndSecurities);

    foreach (const auto sec, secList)
        d->loadSecurity(d->m_ndSecurities, sec);

    secList = d->m_file->currencyList();   // get all available currencies
    d->m_ndCurrencies = new QStandardItem(QStringLiteral("Currencies"));
    d->m_ndCurrencies->setEditable(false);
    rootItem->appendRow(d->m_ndCurrencies);
    foreach (const auto sec, secList)
        d->loadSecurity(d->m_ndCurrencies, sec);

    this->blockSignals(false);
}

/**
  * Notify the model that an object has been added. An action is performed only if the object is a security.
  *
  */
void SecuritiesModel::slotObjectAdded(eMyMoney::File::Object objType, const QString& id)
{
    // check whether change is about security
    if (objType != eMyMoney::File::Object::Security)
        return;

    // check that we're about to add security
    auto sec = MyMoneyFile::instance()->security(id);

    auto itSec = d->itemFromSecurityId(this, id);

    QStandardItem *node;
    if (sec.isCurrency())
        node = d->m_ndCurrencies;
    else
        node = d->m_ndSecurities;

    // if security doesn't exist in model then add it
    if (!itSec) {
        itSec = new QStandardItem(sec.name());
        node->appendRow(itSec);
        itSec->setEditable(false);
    }

    d->setSecurityData(node, itSec->row(), sec, d->m_columns);
}

/**
  * Notify the model that an object has been modified. An action is performed only if the object is a security.
  *
  */
void SecuritiesModel::slotObjectModified(eMyMoney::File::Object objType, const QString& id)
{
    if (objType != eMyMoney::File::Object::Security)
        return;

    // check that we're about to modify security
    auto sec = MyMoneyFile::instance()->security(id);

    auto itSec = d->itemFromSecurityId(this, id);

    QStandardItem *node;
    if (sec.isCurrency())
        node = d->m_ndCurrencies;
    else
        node = d->m_ndSecurities;
    d->setSecurityData(node, itSec->row(), sec, d->m_columns);
}

/**
  * Notify the model that an object has been removed. An action is performed only if the object is an account.
  *
  */
void SecuritiesModel::slotObjectRemoved(eMyMoney::File::Object objType, const QString& id)
{
    if (objType != eMyMoney::File::Object::Security)
        return;

    const auto indexList = match(index(0, 0), Qt::UserRole, id, -1, Qt::MatchFlags(Qt::MatchExactly | Qt::MatchRecursive));
    foreach (const auto index, indexList)
        removeRow(index.row(), index.parent());
}

auto SecuritiesModel::getColumns()
{
    return &d->m_columns;
}

QString SecuritiesModel::getHeaderName(const Column column)
{
    switch(column) {
    case Security:
        return i18n("Security");
    case Symbol:
        return i18nc("@title stock symbol column", "Symbol");
    case Type:
        return i18n("Type");
    case Market:
        return i18n("Market");
    case Currency:
        return i18n("Currency");
    case Fraction:
        return i18n("Fraction");
    default:
        return QString();
    }
}
#endif

class SecuritiesFilterProxyModel::Private
{
public:
    Private()
    {
    }

    ~Private() {}

};

#if QT_VERSION < QT_VERSION_CHECK(5,10,0)
#define QSortFilterProxyModel KRecursiveFilterProxyModel
#endif

SecuritiesFilterProxyModel::SecuritiesFilterProxyModel(QObject *parent, SecuritiesModel *model)
    : QSortFilterProxyModel(parent), d(new Private)
{
    setRecursiveFilteringEnabled(true);
    setDynamicSortFilter(true);
    setFilterKeyColumn(-1);
    setSortLocaleAware(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSourceModel(model);
}

#undef QSortFilterProxyModel

SecuritiesFilterProxyModel::~SecuritiesFilterProxyModel()
{
    delete d;
}

bool SecuritiesFilterProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
    const QModelIndex idx = sourceModel()->index(source_row, 0, source_parent);
    return !sourceModel()->data(idx, eMyMoney::Model::Roles::IdRole).toString().isEmpty();
}
