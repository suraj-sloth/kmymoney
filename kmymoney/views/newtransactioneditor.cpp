/*
    SPDX-FileCopyrightText: 2015-2020 Thomas Baumgart <tbaumgart@kde.org>
    SPDX-License-Identifier: GPL-2.0-or-later
*/


#include "newtransactioneditor.h"

// ----------------------------------------------------------------------------
// QT Includes

#include <QCompleter>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QDebug>
#include <QGlobalStatic>
#include <QStandardItemModel>
#include <QAbstractItemView>

// ----------------------------------------------------------------------------
// KDE Includes

#include <KLocalizedString>
#include <KConcatenateRowsProxyModel>

// ----------------------------------------------------------------------------
// Project Includes

#include "accountsmodel.h"
#include "amounteditcurrencyhelper.h"
#include "costcentermodel.h"
#include "creditdebithelper.h"
#include "icons/icons.h"
#include "idfilter.h"
#include "journalmodel.h"
#include "kcurrencycalculator.h"
#include "kmymoneyaccountcombo.h"
#include "kmymoneysettings.h"
#include "kmymoneyutils.h"
#include "modelenums.h"
#include "mymoneyaccount.h"
#include "mymoneyenums.h"
#include "mymoneyexception.h"
#include "mymoneyfile.h"
#include "mymoneypayee.h"
#include "mymoneyprice.h"
#include "mymoneyschedule.h"
#include "mymoneysecurity.h"
#include "mymoneysplit.h"
#include "mymoneytransaction.h"
#include "payeesmodel.h"
#include "splitdialog.h"
#include "splitmodel.h"
#include "statusmodel.h"
#include "tagsmodel.h"
#include "ui_newtransactioneditor.h"
#include "widgethintframe.h"

using namespace Icons;

class NewTransactionEditor::Private
{
public:
    Private(NewTransactionEditor* parent)
        : q(parent)
        , ui(new Ui_NewTransactionEditor)
        , accountsModel(new AccountNamesFilterProxyModel(parent))
        , categoriesModel(new AccountNamesFilterProxyModel(parent))
        , costCenterModel(new QSortFilterProxyModel(parent))
        , payeesModel(new QSortFilterProxyModel(parent))
        , accepted(false)
        , costCenterRequired(false)
        , bypassPriceEditor(false)
        , splitModel(parent, &undoStack)
        , price(MyMoneyMoney::ONE)
        , amountHelper(nullptr)
    {
        accountsModel->setObjectName(QLatin1String("NewTransactionEditor::accountsModel"));
        categoriesModel->setObjectName(QLatin1String("NewTransactionEditor::categoriesModel"));
        costCenterModel->setObjectName(QLatin1String("SortedCostCenterModel"));
        payeesModel->setObjectName(QLatin1String("SortedPayeesModel"));
        splitModel.setObjectName(QLatin1String("SplitModel"));

        costCenterModel->setSortLocaleAware(true);
        costCenterModel->setSortCaseSensitivity(Qt::CaseInsensitive);

        payeesModel->setSortLocaleAware(true);
        payeesModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    ~Private()
    {
        delete ui;
    }

    void createStatusEntry(eMyMoney::Split::State status);
    void updateWidgetState();
    bool checkForValidTransaction(bool doUserInteraction = true);
    bool isDatePostOpeningDate(const QDate& date, const QString& accountId);
    bool postdateChanged(const QDate& date);
    bool costCenterChanged(int costCenterIndex);
    bool payeeChanged(int payeeIndex);
    void accountChanged(const QString& id);
    bool categoryChanged(const QString& id);
    bool numberChanged(const QString& newNumber);
    bool valueChanged(CreditDebitHelper* valueHelper);
    bool isIncomeExpense(const QModelIndex& idx) const;
    bool isIncomeExpense(const QString& categoryId) const;
    bool tagsChanged(const QStringList& ids);
    int editSplits();
    void updateWidgetAccess();

    MyMoneyMoney getPrice();
    MyMoneyMoney splitsSum() const;

    NewTransactionEditor*         q;
    Ui_NewTransactionEditor*      ui;
    AccountNamesFilterProxyModel* accountsModel;
    AccountNamesFilterProxyModel* categoriesModel;
    QSortFilterProxyModel*        costCenterModel;
    QSortFilterProxyModel*        payeesModel;
    bool                          accepted;
    bool                          costCenterRequired;
    bool                          bypassPriceEditor;
    QUndoStack                    undoStack;
    SplitModel                    splitModel;
    MyMoneyAccount                m_account;
    MyMoneyTransaction            transaction;
    MyMoneySplit                  split;
    MyMoneyMoney                  price;
    CreditDebitHelper*            amountHelper;
};

void NewTransactionEditor::Private::updateWidgetAccess()
{
    const auto enable = !m_account.id().isEmpty();
    ui->dateEdit->setEnabled(enable);
    ui->amountEditCredit->setEnabled(enable);
    ui->amountEditDebit->setEnabled(enable);
    ui->payeeEdit->setEnabled(enable);
    ui->numberEdit->setEnabled(enable);
    ui->categoryCombo->setEnabled(enable);
    ui->costCenterCombo->setEnabled(enable);
    ui->tagContainer->setEnabled(enable);
    ui->statusCombo->setEnabled(enable);
    ui->memoEdit->setEnabled(enable);
    ui->enterButton->setEnabled(enable);
}

void NewTransactionEditor::Private::updateWidgetState()
{
    // update the category combo box
    auto index = splitModel.index(0, 0);

    // update the costcenter combo box
    if (ui->costCenterCombo->isEnabled()) {
        // extract the cost center
        index = MyMoneyFile::instance()->costCenterModel()->indexById(index.data(eMyMoney::Model::SplitCostCenterIdRole).toString());
        if (index.isValid())
            ui->costCenterCombo->setCurrentIndex(costCenterModel->mapFromSource(index).row());
    }
}

bool NewTransactionEditor::Private::checkForValidTransaction(bool doUserInteraction)
{
    QStringList infos;
    bool rc = true;
    if (!postdateChanged(ui->dateEdit->date())) {
        infos << ui->dateEdit->toolTip();
        rc = false;
    }

    if (!costCenterChanged(ui->costCenterCombo->currentIndex())) {
        infos << ui->costCenterCombo->toolTip();
        rc = false;
    }

    if (doUserInteraction) {
        /// @todo add dialog here that shows the @a infos about the problem
    }
    return rc;
}

bool NewTransactionEditor::Private::isDatePostOpeningDate(const QDate& date, const QString& accountId)
{
    bool rc = true;

    try {
        MyMoneyAccount account = MyMoneyFile::instance()->account(accountId);
        const bool isIncomeExpense = account.isIncomeExpense();

        // we don't check for categories
        if (!isIncomeExpense) {
            if (date < account.openingDate())
                rc = false;
        }
    } catch (MyMoneyException& e) {
        qDebug() << "Ooops: invalid account id" << accountId << "in" << Q_FUNC_INFO;
    }
    return rc;
}

bool NewTransactionEditor::Private::postdateChanged(const QDate& date)
{
    bool rc = true;
    WidgetHintFrame::hide(ui->dateEdit, i18n("The posting date of the transaction."));

    // collect all account ids
    QStringList accountIds;
    accountIds << m_account.id();
    const auto rows = splitModel.rowCount();
    for (int row = 0; row < rows; ++row) {
        const auto index = splitModel.index(row, 0);
        accountIds << index.data(eMyMoney::Model::SplitAccountIdRole).toString();
    }

    for (const auto& accountId : accountIds) {
        if (!isDatePostOpeningDate(date, accountId)) {
            MyMoneyAccount account = MyMoneyFile::instance()->account(accountId);
            WidgetHintFrame::show(ui->dateEdit, i18n("The posting date is prior to the opening date of account <b>%1</b>.", account.name()));
            rc = false;
            break;
        }
    }
    return rc;
}


bool NewTransactionEditor::Private::costCenterChanged(int costCenterIndex)
{
    bool rc = true;
    WidgetHintFrame::hide(ui->costCenterCombo, i18n("The cost center this transaction should be assigned to."));
    if (costCenterIndex != -1) {
        if (costCenterRequired && ui->costCenterCombo->currentText().isEmpty()) {
            WidgetHintFrame::show(ui->costCenterCombo, i18n("A cost center assignment is required for a transaction in the selected category."));
            rc = false;
        }
        if (rc == true && splitModel.rowCount() == 1) {
            auto index = costCenterModel->index(costCenterIndex, 0);
            const auto costCenterId = index.data(eMyMoney::Model::IdRole).toString();
            index = splitModel.index(0, 0);

            splitModel.setData(index, costCenterId, eMyMoney::Model::SplitCostCenterIdRole);
        }
    }

    return rc;
}

bool NewTransactionEditor::Private::isIncomeExpense(const QString& categoryId) const
{
    if (!categoryId.isEmpty()) {
        MyMoneyAccount category = MyMoneyFile::instance()->account(categoryId);
        return category.isIncomeExpense();
    }
    return false;
}

bool NewTransactionEditor::Private::isIncomeExpense(const QModelIndex& idx) const
{
    return isIncomeExpense(idx.data(eMyMoney::Model::SplitAccountIdRole).toString());
}

void NewTransactionEditor::Private::accountChanged(const QString& id)
{
    m_account = MyMoneyFile::instance()->accountsModel()->itemById(id);

    transaction.setCommodity(m_account.currencyId());

    // in case we have a single split, we set the accountCombo again
    // so that a possible foreign currency is also taken care of.
    if (splitModel.rowCount() == 1) {
        ui->categoryCombo->setSelected(splitModel.index(0, 0).data(eMyMoney::Model::SplitAccountIdRole).toString());
    }

    updateWidgetAccess();
}

bool NewTransactionEditor::Private::categoryChanged(const QString& accountId)
{
    bool rc = true;
    if (splitModel.rowCount() <= 1) {
        if (!accountId.isEmpty()) {
            try {
                MyMoneyAccount category = MyMoneyFile::instance()->account(accountId);
                const bool isIncomeExpense = category.isIncomeExpense();
                ui->costCenterCombo->setEnabled(isIncomeExpense);
                ui->costCenterLabel->setEnabled(isIncomeExpense);
                costCenterRequired = category.isCostCenterRequired();

                bool needValueSet = false;
                // make sure we have a split in the model
                if (splitModel.rowCount() == 0) {
                    // add an empty split
                    MyMoneySplit s;
                    splitModel.addItem(s);
                    needValueSet = true;
                }

                const QModelIndex index = splitModel.index(0, 0);
                if (!needValueSet) {
                    // update the values only if the category changes. This prevents
                    // the call of the currency calculator if not needed.
                    needValueSet = (index.data(eMyMoney::Model::SplitAccountIdRole).toString().compare(accountId) != 0);
                }
                splitModel.setData(index, accountId, eMyMoney::Model::SplitAccountIdRole);

                rc &= costCenterChanged(ui->costCenterCombo->currentIndex());
                rc &= postdateChanged(ui->dateEdit->date());
                payeeChanged(ui->payeeEdit->currentIndex());

                if (amountHelper->haveValue() && needValueSet) {
                    if (!amountHelper->value().isZero()) {
                        if (isIncomeExpense) {
                            splitModel.setData(index, QVariant::fromValue<MyMoneyMoney>(-amountHelper->value() * getPrice()), eMyMoney::Model::SplitValueRole);
                            splitModel.setData(index, QVariant::fromValue<MyMoneyMoney>(-amountHelper->value()), eMyMoney::Model::SplitSharesRole);
                        } else {
                            splitModel.setData(index, QVariant::fromValue<MyMoneyMoney>(-amountHelper->value()), eMyMoney::Model::SplitValueRole);
                            splitModel.setData(index, QVariant::fromValue<MyMoneyMoney>(-amountHelper->value() * getPrice()), eMyMoney::Model::SplitSharesRole);
                        }
                    } else {
                        splitModel.setData(index, QVariant::fromValue<MyMoneyMoney>(MyMoneyMoney()), eMyMoney::Model::SplitValueRole);
                        splitModel.setData(index, QVariant::fromValue<MyMoneyMoney>(MyMoneyMoney()), eMyMoney::Model::SplitSharesRole);
                    }
                }

            } catch (MyMoneyException& e) {
                qDebug() << "Ooops: invalid account id" << accountId << "in" << Q_FUNC_INFO;
            }
        } else {
            splitModel.unload();
        }
    }
    return rc;
}

bool NewTransactionEditor::Private::numberChanged(const QString& newNumber)
{
    bool rc = true; // number did change
    WidgetHintFrame::hide(ui->numberEdit, i18n("The check number used for this transaction."));
    if (!newNumber.isEmpty()) {
        auto model = MyMoneyFile::instance()->journalModel();
        const QModelIndexList list = model->match(model->index(0, 0), eMyMoney::Model::SplitNumberRole,
                                     QVariant(newNumber),
                                     -1,                         // all splits
                                     Qt::MatchFlags(Qt::MatchExactly | Qt::MatchCaseSensitive | Qt::MatchRecursive));
        for (const auto& idx : list) {
            if (idx.data(eMyMoney::Model::SplitAccountIdRole).toString() == m_account.id()
                    && idx.data(eMyMoney::Model::JournalTransactionIdRole).toString().compare(transaction.id())) {
                WidgetHintFrame::show(ui->numberEdit, i18n("The check number <b>%1</b> has already been used in this account.", newNumber));
                rc = false;
                break;
            }
        }
    }
    return rc;
}

MyMoneyMoney NewTransactionEditor::Private::getPrice()
{
    MyMoneyMoney result(price);
    const auto file = MyMoneyFile::instance();
    if (!bypassPriceEditor && splitModel.rowCount() > 0) {
        const QModelIndex idx = splitModel.index(0, 0);
        const auto categoryId = idx.data(eMyMoney::Model::SplitAccountIdRole).toString();
        const auto category = file->accountsModel()->itemById(categoryId);
        if (!category.id().isEmpty()) {
            const auto security = file->security(category.currencyId());
            if (security.id() != transaction.commodity()) {
                const auto commodity = file->security(transaction.commodity());
                QPointer<KCurrencyCalculator> calc;
                if (category.isIncomeExpense()) {
                    if (result == MyMoneyMoney::ONE) {
                        result = file->price(security.id(), commodity.id(), QDate()).rate(commodity.id());
                    }
                    calc = new KCurrencyCalculator(security,
                                                   commodity,
                                                   amountHelper->value(),
                                                   amountHelper->value() * result,
                                                   ui->dateEdit->date(),
                                                   security.smallestAccountFraction(),
                                                   q);
                } else {
                    if (result == MyMoneyMoney::ONE) {
                        result = file->price(commodity.id(), security.id(), QDate()).rate(security.id());
                    }
                    calc = new KCurrencyCalculator(commodity,
                                                   security,
                                                   amountHelper->value(),
                                                   amountHelper->value() * result,
                                                   ui->dateEdit->date(),
                                                   security.smallestAccountFraction(),
                                                   q);
                }
                if (calc->exec() == QDialog::Accepted && calc) {
                    result = calc->price();
                }
                delete calc;

            } else {
                result = MyMoneyMoney::ONE;
            }
            // keep for next round
            price = result;
        }
    }
    return result;
}


bool NewTransactionEditor::Private::valueChanged(CreditDebitHelper* valueHelper)
{
    bool rc = true;
    if (valueHelper->haveValue() && (splitModel.rowCount() <= 1)) {
        // if (valueHelper->haveValue() && (splitModel.rowCount() <= 1) && (amountHelper->value() != split.value())) {
        rc = false;
        try {
            MyMoneyMoney shares;
            if (splitModel.rowCount() == 1) {
                const QModelIndex index = splitModel.index(0, 0);
                if (isIncomeExpense(index)) {
                    splitModel.setData(index, QVariant::fromValue<MyMoneyMoney>(-amountHelper->value()), eMyMoney::Model::SplitSharesRole);
                    splitModel.setData(index, QVariant::fromValue<MyMoneyMoney>(-amountHelper->value() * getPrice()), eMyMoney::Model::SplitValueRole);
                } else {
                    splitModel.setData(index, QVariant::fromValue<MyMoneyMoney>(-amountHelper->value()), eMyMoney::Model::SplitValueRole);
                    splitModel.setData(index, QVariant::fromValue<MyMoneyMoney>(-amountHelper->value() * getPrice()), eMyMoney::Model::SplitSharesRole);
                }
            }
            rc = true;

        } catch (MyMoneyException& e) {
            qDebug() << "Ooops: something went wrong in" << Q_FUNC_INFO;
        }
    } else {
        /// @todo ask what to do: if the rest of the splits is the same amount we could simply reverse the sign
        /// of all splits, otherwise we could ask if the user wants to start the split editor or anything else.
    }
    return rc;
}

bool NewTransactionEditor::Private::payeeChanged(int payeeIndex)
{
    // copy payee information to second split if there are only two splits
    if (splitModel.rowCount() == 1) {
        const auto idx = splitModel.index(0, 0);
        const auto payeeId = payeesModel->index(payeeIndex, 0).data(eMyMoney::Model::IdRole).toString();
        splitModel.setData(idx, payeeId, eMyMoney::Model::SplitPayeeIdRole);
    }
    return true;
}

bool NewTransactionEditor::Private::tagsChanged(const QStringList& ids)
{
    if (splitModel.rowCount() == 1) {
        const auto idx = splitModel.index(0, 0);
        splitModel.setData(idx, ids, eMyMoney::Model::SplitTagIdRole);
    }
    return true;
}

MyMoneyMoney NewTransactionEditor::Private::splitsSum() const
{
    const auto rows = splitModel.rowCount();
    MyMoneyMoney value;
    for(int row = 0; row < rows; ++row) {
        const auto idx = splitModel.index(row, 0);
        value += idx.data(eMyMoney::Model::SplitValueRole).value<MyMoneyMoney>();
    }
    return value;
}

int NewTransactionEditor::Private::editSplits()
{
    const auto transactionFactor(amountHelper->value().isNegative() ? MyMoneyMoney::ONE : MyMoneyMoney::MINUS_ONE);

    SplitModel dlgSplitModel(q, nullptr, splitModel);

    // create an empty split at the end
    // used to create new splits
    dlgSplitModel.appendEmptySplit();

    auto commodityId = transaction.commodity();
    if (commodityId.isEmpty())
        commodityId = m_account.currencyId();
    const auto commodity = MyMoneyFile::instance()->security(commodityId);

    QPointer<SplitDialog> splitDialog = new SplitDialog(m_account, commodity, -(q->transactionAmount()), transactionFactor, q);
    splitDialog->setModel(&dlgSplitModel);

    int rc = splitDialog->exec();

    if (splitDialog && (rc == QDialog::Accepted)) {
        // remove that empty split again before we update the splits
        dlgSplitModel.removeEmptySplit();

        // copy the splits model contents
        splitModel = dlgSplitModel;

        // update the transaction amount
        amountHelper->setValue(-splitDialog->transactionAmount());

        // the price might have been changed, so we have to update our copy
        // but only if there is one counter split
        if (splitModel.rowCount() == 1) {
            const auto splitIdx = splitModel.index(0, 0);
            const auto shares = splitIdx.data(eMyMoney::Model::SplitSharesRole).value<MyMoneyMoney>();
            const auto value = splitIdx.data(eMyMoney::Model::SplitValueRole).value<MyMoneyMoney>();
            if (!shares.isZero()) {
                price = value / shares;
            }
        }

        // bypass the currency calculator here, we have all info already
        bypassPriceEditor = true;
        updateWidgetState();
        bypassPriceEditor = false;

        QWidget* next = ui->tagContainer->tagCombo();
        if (ui->costCenterCombo->isEnabled()) {
            next = ui->costCenterCombo;
        }
        next->setFocus();
    }

    if (splitDialog) {
        splitDialog->deleteLater();
    }

    return rc;
}

NewTransactionEditor::NewTransactionEditor(QWidget* parent, const QString& accountId)
    : TransactionEditorBase(parent, accountId)
    , d(new Private(this))
{
    auto const file = MyMoneyFile::instance();
    auto const model = file->accountsModel();
    // extract account information from model
    const auto index = model->indexById(accountId);
    d->m_account = model->itemByIndex(index);

    d->ui->setupUi(this);

    // default is to hide the account selection combobox
    setShowAccountCombo(false);

    // insert the tag combo into the tab order
    QWidget::setTabOrder(d->ui->accountCombo, d->ui->dateEdit);
    QWidget::setTabOrder(d->ui->dateEdit, d->ui->amountEditCredit);
    QWidget::setTabOrder(d->ui->amountEditCredit, d->ui->amountEditDebit);
    QWidget::setTabOrder(d->ui->amountEditDebit, d->ui->payeeEdit);
    QWidget::setTabOrder(d->ui->payeeEdit, d->ui->numberEdit);
    QWidget::setTabOrder(d->ui->numberEdit, d->ui->categoryCombo);
    QWidget::setTabOrder(d->ui->categoryCombo, d->ui->costCenterCombo);
    QWidget::setTabOrder(d->ui->costCenterCombo, d->ui->tagContainer->tagCombo());
    QWidget::setTabOrder(d->ui->tagContainer->tagCombo(), d->ui->statusCombo);
    QWidget::setTabOrder(d->ui->statusCombo, d->ui->memoEdit);
    QWidget::setTabOrder(d->ui->memoEdit, d->ui->enterButton);
    QWidget::setTabOrder(d->ui->enterButton, d->ui->cancelButton);

    const auto* splitHelper = new KMyMoneyAccountComboSplitHelper(d->ui->categoryCombo, &d->splitModel);
    connect(splitHelper, &KMyMoneyAccountComboSplitHelper::accountComboEnabled, d->ui->costCenterCombo, &QComboBox::setEnabled);
    connect(splitHelper, &KMyMoneyAccountComboSplitHelper::accountComboEnabled, d->ui->costCenterLabel, &QComboBox::setEnabled);

    d->accountsModel->addAccountGroup(QVector<eMyMoney::Account::Type>{
        eMyMoney::Account::Type::Asset,
        eMyMoney::Account::Type::Liability,
        eMyMoney::Account::Type::Equity,
    });
    d->accountsModel->setHideEquityAccounts(false);
    d->accountsModel->setSourceModel(model);
    d->accountsModel->sort(AccountsModel::Column::AccountName);
    d->ui->accountCombo->setModel(d->accountsModel);

    d->categoriesModel->addAccountGroup(QVector<eMyMoney::Account::Type>{
        eMyMoney::Account::Type::Asset,
        eMyMoney::Account::Type::Liability,
        eMyMoney::Account::Type::Income,
        eMyMoney::Account::Type::Expense,
        eMyMoney::Account::Type::Equity,
    });
    d->categoriesModel->setHideEquityAccounts(false);
    d->categoriesModel->setSourceModel(model);
    d->categoriesModel->sort(AccountsModel::Column::AccountName);
    d->ui->categoryCombo->setModel(d->categoriesModel);

    d->ui->tagContainer->setModel(file->tagsModel()->modelWithEmptyItem());

    d->costCenterModel->setSortRole(Qt::DisplayRole);
    d->costCenterModel->setSourceModel(file->costCenterModel()->modelWithEmptyItem());
    d->costCenterModel->setSortLocaleAware(true);
    d->costCenterModel->sort(0);

    d->ui->costCenterCombo->setEditable(true);
    d->ui->costCenterCombo->setModel(d->costCenterModel);
    d->ui->costCenterCombo->setModelColumn(0);
    d->ui->costCenterCombo->completer()->setFilterMode(Qt::MatchContains);

    d->payeesModel->setSortRole(Qt::DisplayRole);
    d->payeesModel->setSourceModel(file->payeesModel()->modelWithEmptyItem());
    d->payeesModel->setSortLocaleAware(true);
    d->payeesModel->sort(0);

    d->ui->payeeEdit->setEditable(true);
    d->ui->payeeEdit->lineEdit()->setClearButtonEnabled(true);

    d->ui->payeeEdit->setModel(d->payeesModel);
    d->ui->payeeEdit->setModelColumn(0);
    d->ui->payeeEdit->completer()->setCompletionMode(QCompleter::PopupCompletion);
    d->ui->payeeEdit->completer()->setFilterMode(Qt::MatchContains);

    // make sure that there is no selection left in the background
    // in case there is no text in the edit field
    connect(d->ui->payeeEdit->lineEdit(), &QLineEdit::textEdited,
            [&](const QString& txt)
    {
        if (txt.isEmpty()) {
            d->ui->payeeEdit->setCurrentIndex(0);
        }
    }
           );
    connect(d->ui->categoryCombo->lineEdit(), &QLineEdit::textEdited, [&](const QString& txt) {
        if (txt.isEmpty()) {
            d->ui->categoryCombo->setSelected(QString());
        }
    });
    d->ui->enterButton->setIcon(Icons::get(Icon::DialogOK));
    d->ui->cancelButton->setIcon(Icons::get(Icon::DialogCancel));

    d->ui->statusCombo->setModel(MyMoneyFile::instance()->statusModel());

    d->ui->dateEdit->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));

    d->ui->amountEditCredit->setAllowEmpty(true);
    d->ui->amountEditDebit->setAllowEmpty(true);
    d->amountHelper = new CreditDebitHelper(this, d->ui->amountEditCredit, d->ui->amountEditDebit);

    WidgetHintFrameCollection* frameCollection = new WidgetHintFrameCollection(this);
    frameCollection->addFrame(new WidgetHintFrame(d->ui->dateEdit));
    frameCollection->addFrame(new WidgetHintFrame(d->ui->costCenterCombo));
    frameCollection->addFrame(new WidgetHintFrame(d->ui->numberEdit, WidgetHintFrame::Warning));
    frameCollection->addWidget(d->ui->enterButton);

    connect(d->ui->numberEdit, &QLineEdit::textChanged, this, [&](const QString& newNumber) {
        d->numberChanged(newNumber);
    });

    connect(d->ui->costCenterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [&](int costCenterIndex) {
        d->costCenterChanged(costCenterIndex);
    });

    connect(d->ui->accountCombo, &KMyMoneyAccountCombo::accountSelected, this, [&](const QString& id) {
        d->accountChanged(id);
    });
    connect(d->ui->categoryCombo, &KMyMoneyAccountCombo::accountSelected, this, [&](const QString& id) {
        d->categoryChanged(id);
    });

    connect(d->ui->categoryCombo, &KMyMoneyAccountCombo::splitDialogRequest, this, [&]() {
        d->editSplits();
    });

    connect(d->ui->dateEdit, &KMyMoneyDateEdit::dateChanged, this, [&](const QDate& date) {
        d->postdateChanged(date);
        emit postDateChanged(date);
    });

    connect(d->amountHelper, &CreditDebitHelper::valueChanged, this, [&]() {
        d->valueChanged(d->amountHelper);
    });

    connect(d->ui->payeeEdit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [&](int payeeIndex) {
        d->payeeChanged(payeeIndex);
    });

    connect(d->ui->tagContainer, &KTagContainer::tagsChanged, this, [&](const QStringList& tagIds) {
        d->tagsChanged(tagIds);
    });

    connect(d->ui->cancelButton, &QToolButton::clicked, this, &NewTransactionEditor::reject);
    connect(d->ui->enterButton, &QToolButton::clicked, this, &NewTransactionEditor::acceptEdit);

    // handle some events in certain conditions different from default
    d->ui->payeeEdit->installEventFilter(this);
    d->ui->costCenterCombo->installEventFilter(this);
    d->ui->tagContainer->tagCombo()->installEventFilter(this);
    d->ui->statusCombo->installEventFilter(this);

    setCancelButton(d->ui->cancelButton);
    setEnterButton(d->ui->enterButton);

    // setup tooltip

    // setWindowFlags(Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
}

NewTransactionEditor::~NewTransactionEditor()
{
}

void NewTransactionEditor::setAmountPlaceHolderText(const QAbstractItemModel* model)
{
    d->ui->amountEditCredit->setPlaceholderText(model->headerData(JournalModel::Column::Payment, Qt::Horizontal).toString());
    d->ui->amountEditDebit->setPlaceholderText(model->headerData(JournalModel::Column::Deposit, Qt::Horizontal).toString());
}

bool NewTransactionEditor::accepted() const
{
    return d->accepted;
}

void NewTransactionEditor::acceptEdit()
{
    if (d->checkForValidTransaction()) {
        d->accepted = true;
        emit done();
    }
}

void NewTransactionEditor::loadSchedule(const MyMoneySchedule& schedule)
{
    if (schedule.transaction().splitCount() == 0) {
        // new schedule
        d->transaction = MyMoneyTransaction();
        d->transaction.setCommodity(MyMoneyFile::instance()->baseCurrency().id());
        d->split = MyMoneySplit();
        d->split.setAccountId(QString());
        const auto lastUsedPostDate = KMyMoneySettings::lastUsedPostDate();
        if (lastUsedPostDate.isValid()) {
            d->ui->dateEdit->setDate(lastUsedPostDate.date());
        } else {
            d->ui->dateEdit->setDate(QDate::currentDate());
        }
        QSignalBlocker accountBlocker(d->ui->categoryCombo->lineEdit());
        d->ui->accountCombo->clearEditText();
        QSignalBlocker categoryBlocker(d->ui->categoryCombo->lineEdit());
        d->ui->categoryCombo->clearEditText();
        d->updateWidgetAccess();
    } else {
        // existing schedule
        d->transaction = schedule.transaction();
        d->split = d->transaction.splits().first();

        // make sure the commodity is the one of the current account
        // in case we have exactly two splits. This is a precondition
        // used by the transaction editor to work properly.
        auto transactionValue = d->split.value();
        if (d->transaction.splitCount() == 2) {
            transactionValue = d->split.shares();
            d->split.setValue(transactionValue);
        }

        // preset the value to be used for the amount widget
        auto amount = d->split.shares();

        for (const auto& split : d->transaction.splits()) {
            if (split.id() == d->split.id()) {
                d->ui->dateEdit->setDate(d->transaction.postDate());

                const auto payeeId = split.payeeId();
                const QModelIndex payeeIdx = MyMoneyFile::instance()->payeesModel()->indexById(payeeId);
                if (payeeIdx.isValid()) {
                    d->ui->payeeEdit->setCurrentIndex(MyMoneyFile::baseModel()->mapFromBaseSource(d->payeesModel, payeeIdx).row());
                } else {
                    d->ui->payeeEdit->setCurrentIndex(0);
                }

                d->ui->memoEdit->clear();
                d->ui->memoEdit->insertPlainText(split.memo());
                d->ui->memoEdit->moveCursor(QTextCursor::Start);
                d->ui->memoEdit->ensureCursorVisible();

                d->ui->numberEdit->setText(split.number());
                d->ui->statusCombo->setCurrentIndex(static_cast<int>(split.reconcileFlag()));
                d->ui->tagContainer->loadTags(split.tagIdList());
            } else {
                d->splitModel.appendSplit(split);
                if (d->transaction.splitCount() == 2) {
                    const auto shares = split.shares();
                    // the following is only relevant for transactions with two splits
                    // in different currencies
                    const auto value = -transactionValue;
                    const auto idx = d->splitModel.index(0, 0);
                    d->splitModel.setData(idx, QVariant::fromValue<MyMoneyMoney>(value), eMyMoney::Model::SplitValueRole);
                    if (!value.isZero()) {
                        d->price = shares / value;
                    }
                    // make sure to use a possible foreign currency value
                    // in case of income and expense categories.
                    // in this case, we also need to use the reciprocal of the price
                    if (d->isIncomeExpense(split.accountId())) {
                        amount = -shares;
                        if (!shares.isZero()) {
                            d->price = value / shares;
                        }
                    }
                }
            }
        }
        d->transaction.setCommodity(d->m_account.currencyId());

        // in case we have a single split, we set the accountCombo again
        // so that a possible foreign currency is also taken care of.
        if (d->splitModel.rowCount() == 1) {
            d->ui->categoryCombo->setSelected(d->splitModel.index(0, 0).data(eMyMoney::Model::SplitAccountIdRole).toString());
        }
        // then setup the amount widget and update the state
        // of all other widgets without calling the price editor
        d->bypassPriceEditor = true;
        d->amountHelper->setValue(amount);
        d->updateWidgetState();
        d->bypassPriceEditor = false;
    }
    new AmountEditCurrencyHelper(d->ui->categoryCombo, d->amountHelper, d->transaction.commodity());
}

void NewTransactionEditor::loadTransaction(const QModelIndex& index)
{
    // we may also get here during saving the transaction as
    // a callback from the model, but we can safely ignore it
    if (d->accepted || !index.isValid())
        return;

    auto idx = MyMoneyFile::baseModel()->mapToBaseSource(index);
    if (idx.data(eMyMoney::Model::IdRole).toString().isEmpty()) {
        d->transaction = MyMoneyTransaction();
        d->transaction.setCommodity(d->m_account.currencyId());
        d->split = MyMoneySplit();
        d->split.setAccountId(d->m_account.id());
        const auto lastUsedPostDate = KMyMoneySettings::lastUsedPostDate();
        if (lastUsedPostDate.isValid()) {
            d->ui->dateEdit->setDate(lastUsedPostDate.date());
        } else {
            d->ui->dateEdit->setDate(QDate::currentDate());
        }
        QSignalBlocker accountBlocker(d->ui->categoryCombo->lineEdit());
        d->ui->accountCombo->clearEditText();
        QSignalBlocker categoryBlocker(d->ui->categoryCombo->lineEdit());
        d->ui->categoryCombo->clearEditText();

    } else {
        // find which item has this id and set is as the current item
        const auto selectedSplitRow = idx.row();

        // keep a copy of the transaction and split
        d->transaction = MyMoneyFile::instance()->journalModel()->itemByIndex(idx).transaction();
        d->split = MyMoneyFile::instance()->journalModel()->itemByIndex(idx).split();
        const auto list = idx.model()->match(idx.model()->index(0, 0), eMyMoney::Model::JournalTransactionIdRole,
                                             idx.data(eMyMoney::Model::JournalTransactionIdRole),
                                             -1,                         // all splits
                                             Qt::MatchFlags(Qt::MatchExactly | Qt::MatchCaseSensitive | Qt::MatchRecursive));

        // make sure the commodity is the one of the current account
        // in case we have exactly two splits. This is a precondition
        // used by the transaction editor to work properly.
        auto transactionValue = d->split.value();
        if (d->transaction.splitCount() == 2) {
            transactionValue = d->split.shares();
            d->split.setValue(transactionValue);
        }

        // preset the value to be used for the amount widget
        auto amount = d->split.shares();

        for (const auto& splitIdx : list) {
            if (selectedSplitRow == splitIdx.row()) {
                d->ui->dateEdit->setDate(splitIdx.data(eMyMoney::Model::TransactionPostDateRole).toDate());

                const auto payeeId = splitIdx.data(eMyMoney::Model::SplitPayeeIdRole).toString();
                const QModelIndex payeeIdx = MyMoneyFile::instance()->payeesModel()->indexById(payeeId);
                if (payeeIdx.isValid()) {
                    d->ui->payeeEdit->setCurrentIndex(MyMoneyFile::baseModel()->mapFromBaseSource(d->payeesModel, payeeIdx).row());
                } else {
                    d->ui->payeeEdit->setCurrentIndex(0);
                }

                d->ui->memoEdit->clear();
                d->ui->memoEdit->insertPlainText(splitIdx.data(eMyMoney::Model::SplitMemoRole).toString());
                d->ui->memoEdit->moveCursor(QTextCursor::Start);
                d->ui->memoEdit->ensureCursorVisible();

                d->ui->numberEdit->setText(splitIdx.data(eMyMoney::Model::SplitNumberRole).toString());
                d->ui->statusCombo->setCurrentIndex(splitIdx.data(eMyMoney::Model::SplitReconcileFlagRole).toInt());
                d->ui->tagContainer->loadTags(splitIdx.data(eMyMoney::Model::SplitTagIdRole).toStringList());
            } else {
                d->splitModel.appendSplit(MyMoneyFile::instance()->journalModel()->itemByIndex(splitIdx).split());
                if (splitIdx.data(eMyMoney::Model::TransactionSplitCountRole) == 2) {
                    const auto shares = splitIdx.data(eMyMoney::Model::SplitSharesRole).value<MyMoneyMoney>();
                    // the following is only relevant for transactions with two splits
                    // in different currencies
                    const auto value = -transactionValue;
                    idx = d->splitModel.index(0, 0);
                    d->splitModel.setData(idx, QVariant::fromValue<MyMoneyMoney>(value), eMyMoney::Model::SplitValueRole);
                    if (!value.isZero()) {
                        d->price = shares / value;
                    }
                    // make sure to use a possible foreign currency value
                    // in case of income and expense categories.
                    // in this case, we also need to use the reciprocal of the price
                    if (d->isIncomeExpense(splitIdx)) {
                        amount = -shares;
                        if (!shares.isZero()) {
                            d->price = value / shares;
                        }
                    }
                }
            }
        }
        d->transaction.setCommodity(d->m_account.currencyId());

        // in case we have a single split, we set the accountCombo again
        // so that a possible foreign currency is also taken care of.
        if (d->splitModel.rowCount() == 1) {
            d->ui->categoryCombo->setSelected(d->splitModel.index(0, 0).data(eMyMoney::Model::SplitAccountIdRole).toString());
        }
        // then setup the amount widget and update the state
        // of all other widgets without calling the price editor
        d->bypassPriceEditor = true;
        d->amountHelper->setValue(amount);
        d->updateWidgetState();
        d->bypassPriceEditor = false;
    }
    // set focus to payee edit once we return to event loop
    QMetaObject::invokeMethod(d->ui->payeeEdit, "setFocus", Qt::QueuedConnection);

    new AmountEditCurrencyHelper(d->ui->categoryCombo, d->amountHelper, d->transaction.commodity());
}


void NewTransactionEditor::editSplits()
{
    d->editSplits() == QDialog::Accepted ? acceptEdit() : reject();
}

MyMoneyMoney NewTransactionEditor::transactionAmount() const
{
    auto amount = -d->splitsSum();
    if (amount.isZero()) {
        amount = d->amountHelper->value();
    }
    return amount;
}

MyMoneyTransaction NewTransactionEditor::transaction() const
{
    MyMoneyTransaction t;

    if (!d->transaction.id().isEmpty()) {
        t = d->transaction;
    } else {
        // we keep the date when adding a new transaction
        // for the next new one
        KMyMoneySettings::setLastUsedPostDate(QDateTime(d->ui->dateEdit->date()));
    }

    // first remove the splits that are gone
    for (const auto& split : t.splits()) {
        if (split.id() == d->split.id()) {
            continue;
        }
        const auto rows = d->splitModel.rowCount();
        int row;
        for (row = 0; row < rows; ++row) {
            const QModelIndex index = d->splitModel.index(row, 0);
            if (index.data(eMyMoney::Model::IdRole).toString() == split.id()) {
                break;
            }
        }

        // if the split is not in the model, we get rid of it
        if (d->splitModel.rowCount() == row) {
            t.removeSplit(split);
        }
    }

    // now we update the split we are opened for
    MyMoneySplit sp(d->split);

    // in case the transaction does not have a split
    // at this point, we need to make sure that we
    // add the first one and don't try to modify it
    // we do so by clearing its id
    if (t.splitCount() == 0) {
        sp.clearId();
    }

    sp.setNumber(d->ui->numberEdit->text());
    sp.setMemo(d->ui->memoEdit->toPlainText());
    // setting up the shares and value members. In case there is
    // no or more than two splits, we can take the amount shown
    // in the widgets directly. In case of 2 splits, we take
    // the negative value of the second split (the one in the
    // splitModel) and use it as value and shares since the
    // displayed value in the widget may be shown in a different
    // currency
    if (d->splitModel.rowCount() == 1) {
        const QModelIndex idx = d->splitModel.index(0, 0);
        const auto val = idx.data(eMyMoney::Model::SplitValueRole).value<MyMoneyMoney>();
        sp.setShares(-val);
        sp.setValue(-val);
    } else {
        sp.setShares(d->amountHelper->value());
        sp.setValue(d->amountHelper->value());
    }

    if (sp.reconcileFlag() != eMyMoney::Split::State::Reconciled && !sp.reconcileDate().isValid()
        && d->ui->statusCombo->currentIndex() == (int)eMyMoney::Split::State::Reconciled) {
        sp.setReconcileDate(QDate::currentDate());
    }

    sp.setReconcileFlag(static_cast<eMyMoney::Split::State>(d->ui->statusCombo->currentIndex()));

    const auto payeeRow = d->ui->payeeEdit->currentIndex();
    const auto payeeIdx = d->payeesModel->index(payeeRow, 0);
    sp.setPayeeId(payeeIdx.data(eMyMoney::Model::IdRole).toString());
    sp.setTagIdList(d->ui->tagContainer->selectedTags());

    if (sp.id().isEmpty()) {
        t.addSplit(sp);
    } else {
        t.modifySplit(sp);
    }
    t.setPostDate(d->ui->dateEdit->date());

    // now update and add what we have in the model
    addSplitsFromModel(t, &d->splitModel);

    return t;
}

void NewTransactionEditor::saveTransaction()
{
    auto t = transaction();

    MyMoneyFileTransaction ft;
    try {
        if (t.id().isEmpty()) {
            MyMoneyFile::instance()->addTransaction(t);
        } else {
            MyMoneyFile::instance()->modifyTransaction(t);
        }
        ft.commit();

    } catch (const MyMoneyException& e) {
        qDebug() << Q_FUNC_INFO << "something went wrong" << e.what();
    }
}

bool NewTransactionEditor::eventFilter(QObject* o, QEvent* e)
{
    auto cb = qobject_cast<QComboBox*>(o);
    if (o) {
        // filter out wheel events for combo boxes if the popup view is not visible
        if ((e->type() == QEvent::Wheel) && !cb->view()->isVisible()) {
            return true;
        }
    }
    return QFrame::eventFilter(o, e);
}

QDate NewTransactionEditor::postDate() const
{
    return d->ui->dateEdit->date();
}

void NewTransactionEditor::setShowAccountCombo(bool show) const
{
    d->ui->accountLabel->setVisible(show);
    d->ui->accountCombo->setVisible(show);
    d->ui->accountCombo->setSplitActionVisible(false);
}

void NewTransactionEditor::setShowButtons(bool show) const
{
    d->ui->enterButton->setVisible(show);
    d->ui->cancelButton->setVisible(show);
}

void NewTransactionEditor::setShowNumberWidget(bool show) const
{
    d->ui->numberLabel->setVisible(show);
    d->ui->numberEdit->setVisible(show);
}

void NewTransactionEditor::setAccountId(const QString& accountId)
{
    d->ui->accountCombo->setSelected(accountId);
}
