/*
    SPDX-FileCopyrightText: 2015-2019 Thomas Baumgart <tbaumgart@kde.org>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ledgerview.h"

// ----------------------------------------------------------------------------
// QT Includes

#include <QAction>
#include <QDate>
#include <QDebug>
#include <QHeaderView>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QToolTip>
#include <QWidgetAction>

// ----------------------------------------------------------------------------
// KDE Includes

#include <KDualAction>
#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KMessageWidget>
#include <KStandardGuiItem>

// ----------------------------------------------------------------------------
// Project Includes

#include "accountsmodel.h"
#include "columnselector.h"
#include "delegateproxy.h"
#include "journaldelegate.h"
#include "journalmodel.h"
#include "kmymoneyaccountselector.h"
#include "ledgerviewsettings.h"
#include "menuenums.h"
#include "mymoneyenums.h"
#include "mymoneyfile.h"
#include "mymoneymoney.h"
#include "mymoneysecurity.h"
#include "mymoneyutils.h"
#include "onlinebalancedelegate.h"
#include "reconciliationdelegate.h"
#include "reconciliationmodel.h"
#include "schedulesjournalmodel.h"
#include "selectedobjects.h"
#include "specialdatedelegate.h"
#include "specialdatesmodel.h"
#include "transactioneditorbase.h"

Q_GLOBAL_STATIC(LedgerView*, s_globalEditView);

class LedgerView::Private
{
public:
    Private(LedgerView* qq)
        : q(qq)
        , delegateProxy(new DelegateProxy(q))
        , moveToAccountSelector(nullptr)
        , columnSelector(nullptr)
        , infoMessage(new KMessageWidget(q))
        , adjustableColumn(JournalModel::Column::Detail)
        , adjustingColumn(false)
        , showValuesInverted(false)
        , newTransactionPresent(false)
    {
        infoMessage->hide();

        const auto file = MyMoneyFile::instance();

        auto journalDelegate = new JournalDelegate(q);
        delegateProxy->addDelegate(file->journalModel(), journalDelegate);
        delegateProxy->addDelegate(file->journalModel()->newTransaction(), journalDelegate);
        delegateProxy->addDelegate(file->accountsModel(), new OnlineBalanceDelegate(q));
        delegateProxy->addDelegate(file->specialDatesModel(), new SpecialDateDelegate(q));
        delegateProxy->addDelegate(file->schedulesJournalModel(), journalDelegate);
        delegateProxy->addDelegate(file->reconciliationModel(), new ReconciliationDelegate(q));

        q->setItemDelegate(delegateProxy);
    }

    void setSingleLineDetailRole(eMyMoney::Model::Roles role)
    {
        for (auto delegate : delegates) {
            JournalDelegate* journalDelegate = qobject_cast<JournalDelegate*>(delegate);
            if(journalDelegate) {
                journalDelegate->setSingleLineRole(role);
            }
        }
    }

    void ensureEditorFullyVisible(const QModelIndex& idx)
    {
        const auto viewportHeight = q->viewport()->height();
        const auto verticalOffset = q->verticalHeader()->offset();
        const auto verticalPosition = q->verticalHeader()->sectionPosition(idx.row());
        const auto cellHeight = q->verticalHeader()->sectionSize(idx.row());

        // in case the idx is displayed passed the viewport
        // adjust the position of the scroll area
        if (verticalPosition - verticalOffset + cellHeight > viewportHeight) {
            q->verticalScrollBar()->setValue(q->verticalScrollBar()->maximum());
        }
    }

    bool haveGlobalEditor()
    {
        return *s_globalEditView() != nullptr;
    }

    void registerGlobalEditor()
    {
        if (!haveGlobalEditor()) {
            *s_globalEditView() = q;
        }
    }

    void unregisterGlobalEditor()
    {
        *s_globalEditView() = nullptr;
    }

    void createMoveToSubMenu()
    {
        if (!moveToAccountSelector) {
            auto menu = pMenus[eMenu::Menu::MoveTransaction];
            if (menu) {
                const auto actionList = menu->actions();
                // the account selector is only created the first time this is called. All following calls will
                // reuse the created selector. The assumption is, that the first entry is the submenu header and
                // the second is the account selector. Everything else is a programming mistake and causes a crash.
                switch (actionList.count()) {
                case 1: // the header is the only thing in the menu
                {
                    auto accountSelectorAction = new QWidgetAction(menu);
                    moveToAccountSelector = new KMyMoneyAccountSelector(menu, 0, false);
                    moveToAccountSelector->setObjectName("transaction_move_menu_selector");
                    accountSelectorAction->setDefaultWidget(moveToAccountSelector);
                    menu->addAction(accountSelectorAction);
                    q->connect(moveToAccountSelector, &KMyMoneySelector::itemSelected, q, &LedgerView::slotMoveToAccount);
                } break;

                case 2: // the header and the selector are newTransactionPresent
                {
                    auto accountSelectorAction = qobject_cast<QWidgetAction*>(actionList.at(1));
                    if (accountSelectorAction) {
                        moveToAccountSelector = qobject_cast<KMyMoneyAccountSelector*>(accountSelectorAction->defaultWidget());
                    }
                } break;

                default:
                    qFatal("Found misalignment with MoveTransaction menu. Please have fixed by developer.");
                    break;
                }
            }
        }
    }

    eMenu::Menu updateDynamicActions()
    {
        eMenu::Menu menuType = eMenu::Menu::Transaction;

        const auto indexes = q->selectionModel()->selectedIndexes();
        auto const gotoAccount = pActions[eMenu::Action::GoToAccount];
        auto const gotoPayee = pActions[eMenu::Action::GoToPayee];

        gotoAccount->setText(i18nc("@action:inmenu open account", "Go to account"));
        gotoAccount->setEnabled(false);
        gotoPayee->setText(i18nc("@action:inmenu open payee", "Go to payee"));
        gotoPayee->setEnabled(false);

        if (!indexes.isEmpty()) {
            const auto baseIdx = MyMoneyFile::instance()->journalModel()->mapToBaseSource(indexes.at(0));
            const auto journalEntry = MyMoneyFile::instance()->journalModel()->itemByIndex(baseIdx);

            // if this entry points to the schedules, we switch the menu type
            if (baseIdx.model() == MyMoneyFile::instance()->schedulesJournalModel()) {
                menuType = eMenu::Menu::Schedule;
            }

            MyMoneyAccount acc;
            if (!q->isColumnHidden(JournalModel::Column::Account)) {
                // in case the account column is shown, we jump to that account
                acc = MyMoneyFile::instance()->account(journalEntry.split().accountId());
            } else {
                // otherwise, we try to find a suitable asset/liability account
                for (const auto& split : journalEntry.transaction().splits()) {
                    acc = MyMoneyFile::instance()->account(split.accountId());
                    if (split.id() != journalEntry.split().id()) {
                        if (!acc.isIncomeExpense()) {
                            // for stock accounts we show the portfolio account
                            if (acc.isInvest()) {
                                acc = MyMoneyFile::instance()->account(acc.parentAccountId());
                            }
                            break;
                        }
                    }
                }
            }
            // we don't allow jumping to categories when there are more than one
            if (acc.isIncomeExpense() && journalEntry.transaction().splitCount() > 2) {
                acc.clearId();
            }

            // found an account, update the action
            if (!acc.id().isEmpty()) {
                auto name = acc.name();
                name.replace(QRegExp("&(?!&)"), "&&");
                gotoAccount->setEnabled(true);
                gotoAccount->setText(i18nc("@action:inmenu open account", "Go to '%1'", name));
                gotoAccount->setData(acc.id());
            }

            if (!journalEntry.split().payeeId().isEmpty()) {
                auto payeeId = indexes.at(0).data(eMyMoney::Model::SplitPayeeIdRole).toString();
                if (!payeeId.isEmpty()) {
                    auto name = indexes.at(0).data(eMyMoney::Model::SplitPayeeRole).toString();
                    name.replace(QRegExp("&(?!&)"), "&&");
                    gotoPayee->setEnabled(true);
                    gotoPayee->setText(i18nc("@action:inmenu open payee", "Go to '%1'", name));
                    gotoPayee->setData(payeeId);
                }
            }
        }

        // for a transaction context menu we need to update the
        // "move to account" destinations
        if (menuType == eMenu::Menu::Transaction) {
            const auto file = MyMoneyFile::instance();
            createMoveToSubMenu();

            // in case we were not able to create the selector, we
            // better get out of here. Anything else would cause
            // a crash later on (accountSet.load)
            if (moveToAccountSelector) {
                const auto selectedAccountId = selection.firstSelection(SelectedObjects::Account);
                const auto accountIdx = file->accountsModel()->indexById(selectedAccountId);
                AccountSet accountSet;
                if (accountIdx.isValid()) {
                    if (accountIdx.data(eMyMoney::Model::AccountTypeRole).value<eMyMoney::Account::Type>() == eMyMoney::Account::Type::Investment) {
                        accountSet.addAccountType(eMyMoney::Account::Type::Investment);
                    } else if (accountIdx.data(eMyMoney::Model::AccountIsAssetLiabilityRole).toBool()) {
                        accountSet.addAccountType(eMyMoney::Account::Type::Checkings);
                        accountSet.addAccountType(eMyMoney::Account::Type::Savings);
                        accountSet.addAccountType(eMyMoney::Account::Type::Cash);
                        accountSet.addAccountType(eMyMoney::Account::Type::AssetLoan);
                        accountSet.addAccountType(eMyMoney::Account::Type::CertificateDep);
                        accountSet.addAccountType(eMyMoney::Account::Type::MoneyMarket);
                        accountSet.addAccountType(eMyMoney::Account::Type::Asset);
                        accountSet.addAccountType(eMyMoney::Account::Type::Currency);
                        accountSet.addAccountType(eMyMoney::Account::Type::CreditCard);
                        accountSet.addAccountType(eMyMoney::Account::Type::Loan);
                        accountSet.addAccountType(eMyMoney::Account::Type::Liability);
                    } else if (accountIdx.data(eMyMoney::Model::AccountIsIncomeExpenseRole).toBool()) {
                        accountSet.addAccountType(eMyMoney::Account::Type::Income);
                        accountSet.addAccountType(eMyMoney::Account::Type::Expense);
                    }
                } else {
                    accountSet.addAccountType(eMyMoney::Account::Type::Checkings);
                    accountSet.addAccountType(eMyMoney::Account::Type::Savings);
                    accountSet.addAccountType(eMyMoney::Account::Type::Cash);
                    accountSet.addAccountType(eMyMoney::Account::Type::AssetLoan);
                    accountSet.addAccountType(eMyMoney::Account::Type::CertificateDep);
                    accountSet.addAccountType(eMyMoney::Account::Type::MoneyMarket);
                    accountSet.addAccountType(eMyMoney::Account::Type::Asset);
                    accountSet.addAccountType(eMyMoney::Account::Type::Currency);
                    accountSet.addAccountType(eMyMoney::Account::Type::CreditCard);
                    accountSet.addAccountType(eMyMoney::Account::Type::Loan);
                    accountSet.addAccountType(eMyMoney::Account::Type::Liability);
                }

                accountSet.load(moveToAccountSelector);

                // remove those accounts that we currently reference
                // with the selected items
                QSet<QString> currencyIds;
                for (const auto& journalId : selection.selection(SelectedObjects::JournalEntry)) {
                    const auto journalIdx = file->journalModel()->indexById(journalId);
                    moveToAccountSelector->removeItem(journalIdx.data(eMyMoney::Model::JournalSplitAccountIdRole).toString());
                    const auto accountId = journalIdx.data(eMyMoney::Model::JournalSplitAccountIdRole).toString();
                    const auto accIdx = file->accountsModel()->indexById(accountId);
                    currencyIds.insert(accIdx.data(eMyMoney::Model::AccountCurrencyIdRole).toString());
                }

                // remove those accounts from the list that are denominated
                // in a different currency
                const auto list = moveToAccountSelector->accountList();
                for (const auto& accountId : list) {
                    const auto idx = file->accountsModel()->indexById(accountId);
                    if (!currencyIds.contains(idx.data(eMyMoney::Model::AccountCurrencyIdRole).toString())) {
                        moveToAccountSelector->removeItem(accountId);
                    }
                }

                // in case we have transactions in multiple currencies selected,
                // the move is not supported.
                pMenus[eMenu::Menu::MoveTransaction]->setDisabled(currencyIds.count() > 1);
            }
        }

        return menuType;
    }

    QString createSplitTooltip(const QModelIndex& idx)
    {
        QString txt;

        int splitCount = idx.data(eMyMoney::Model::TransactionSplitCountRole).toInt();
        if ((q->currentIndex().row() != idx.row()) && (splitCount > 1)) {
            auto file = MyMoneyFile::instance();
            const auto journalEntryId = idx.data(eMyMoney::Model::IdRole).toString();
            const auto securityId = idx.data(eMyMoney::Model::TransactionCommodityRole).toString();
            const auto security = file->security(securityId);
            MyMoneyMoney factor(MyMoneyMoney::ONE);
            if (!idx.data(eMyMoney::Model::Roles::SplitSharesRole).value<MyMoneyMoney>().isNegative())
                factor = -factor;

            const auto indexes = file->journalModel()->indexesByTransactionId(idx.data(eMyMoney::Model::JournalTransactionIdRole).toString());
            if (!indexes.isEmpty()) {
                txt = QLatin1String("<table style='white-space:pre'>");
                for (const auto& tidx : indexes) {
                    if (tidx.data(eMyMoney::Model::IdRole).toString() == journalEntryId)
                        continue;
                    const auto acc = file->accountsModel()->itemById(tidx.data(eMyMoney::Model::SplitAccountIdRole).toString());
                    const auto category = file->accountToCategory(acc.id());
                    const auto amount =
                        MyMoneyUtils::formatMoney((tidx.data(eMyMoney::Model::SplitValueRole).value<MyMoneyMoney>() * factor), acc, security, true);

                    txt += QString("<tr><td>%1</td><td align=right>%2</td></tr>").arg(category, amount);
                }
                if (splitCount > 2) {
                    txt += QStringLiteral("<tr><td></td><td><hr/></td></tr>");

                    const auto acc = file->accountsModel()->itemById(idx.data(eMyMoney::Model::SplitAccountIdRole).toString());
                    const auto amount =
                        MyMoneyUtils::formatMoney((idx.data(eMyMoney::Model::SplitValueRole).value<MyMoneyMoney>() * (-factor)), acc, security, true);
                    txt += QString("<tr><td></td><td align=right>%2</td></tr>").arg(amount);
                }
                txt += QLatin1String("</table>");
            }
        }
        return txt;
    }

    QVector<eMyMoney::Model::Roles> statusRoles(const QModelIndex& idx) const
    {
        QVector<eMyMoney::Model::Roles> status;
        if (idx.data(eMyMoney::Model::TransactionErroneousRole).toBool()) {
            status.append(eMyMoney::Model::TransactionErroneousRole);
        } else if (idx.data(eMyMoney::Model::ScheduleIsOverdueRole).toBool()) {
            status.append(eMyMoney::Model::ScheduleIsOverdueRole);
        }

        // draw the import icon
        if (idx.data(eMyMoney::Model::TransactionIsImportedRole).toBool()) {
            status.append(eMyMoney::Model::TransactionIsImportedRole);
        }

        // draw the matched icon
        if (idx.data(eMyMoney::Model::JournalSplitIsMatchedRole).toBool()) {
            status.append(eMyMoney::Model::JournalSplitIsMatchedRole);
        }
        return status;
    }

    int iconClickIndex(const QModelIndex& idx, const QPoint& pos)
    {
        const auto font = idx.data(Qt::FontRole).value<QFont>();
        const auto metrics = QFontMetrics(font);
        const auto iconWidth = (metrics.lineSpacing() + 2) + (2 * q->style()->pixelMetric(QStyle::PM_FocusFrameHMargin));
        const auto cellRect = q->visualRect(idx);
        auto iconRect = QRect(cellRect.x() + cellRect.width() - iconWidth, cellRect.y(), iconWidth, iconWidth);
        auto iconIndex = -1;
        for (int i = 0; i < JournalDelegate::maxIcons(); ++i) {
            if (iconRect.contains(pos)) {
                iconIndex = i;
                break;
            }
            iconRect.moveLeft(iconRect.left() - iconWidth);
        }
        return iconIndex;
    }

    LedgerView* q;
    DelegateProxy* delegateProxy;
    KMyMoneyAccountSelector* moveToAccountSelector;
    ColumnSelector* columnSelector;
    KMessageWidget* infoMessage;
    QHash<const QAbstractItemModel*, QStyledItemDelegate*>   delegates;
    int adjustableColumn;
    bool adjustingColumn;
    bool showValuesInverted;
    bool newTransactionPresent;
    QString accountId;
    QString groupName;
    QPersistentModelIndex editIndex;
    SelectedObjects selection;
    QString firstSelectedId;
};



LedgerView::LedgerView(QWidget* parent)
    : QTableView(parent)
    , d(new Private(this))
{
    // keep rows as small as possible
    verticalHeader()->setMinimumSectionSize(1);
    verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    verticalHeader()->hide();

    horizontalHeader()->setMinimumSectionSize(20);

    // since we don't have a vertical header, it does not make sense
    // to use the first column to select all items in the view
    setCornerButtonEnabled(false);

    // This will allow the user to move the columns, but
    // the delegate cannot handle it yet and it requires to
    // reset the spans as well.
    // horizontalHeader()->setMovable(true);

    // make sure to get informed about resize operations on the columns
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [&]() {
        adjustDetailColumn(viewport()->width());
    } );

    // get notifications about setting changes
    connect(LedgerViewSettings::instance(), &LedgerViewSettings::settingsChanged, this, &LedgerView::slotSettingsChanged);

    // we don't need autoscroll as we do not support drag/drop
    setAutoScroll(false);

    setAlternatingRowColors(true);

    setSelectionBehavior(SelectRows);

    // setup context menu
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [&](QPoint pos) {
        const auto menuType = d->updateDynamicActions();
        emit requestCustomContextMenu(menuType, viewport()->mapToGlobal(pos));
    });
    setTabKeyNavigation(false);
}

LedgerView::~LedgerView()
{
    delete d;
}

void LedgerView::setColumnSelectorGroupName(const QString& groupName)
{
    if (!d->columnSelector) {
        d->groupName = groupName;
    } else {
        qWarning() << "LedgerView::setColumnSelectorGroupName must be called before model assignment";
    }
}

void LedgerView::setModel(QAbstractItemModel* model)
{
    if (!d->columnSelector) {
        d->columnSelector = new ColumnSelector(this, d->groupName);
    }
    QSignalBlocker blocker(this);
    QTableView::setModel(model);

    d->columnSelector->setModel(model);
    horizontalHeader()->setSectionResizeMode(JournalModel::Column::Reconciliation, QHeaderView::ResizeToContents);
}

void LedgerView::setAccountId(const QString& id)
{
    d->accountId = id;
    d->selection.setSelection(SelectedObjects::Account, id);
}

const QString& LedgerView::accountId() const
{
    return d->accountId;
}

bool LedgerView::showValuesInverted() const
{
    return d->showValuesInverted;
}

void LedgerView::setColumnsHidden(QVector<int> columns)
{
    d->columnSelector->setAlwaysHidden(columns);
}

void LedgerView::setColumnsShown(QVector<int> columns)
{
    d->columnSelector->setAlwaysVisible(columns);
}

bool LedgerView::edit(const QModelIndex& index, QAbstractItemView::EditTrigger trigger, QEvent* event)
{
    bool suppressDuplicateEditorStart = false;

    switch(trigger) {
    case QAbstractItemView::DoubleClicked:
    case QAbstractItemView::EditKeyPressed:
        suppressDuplicateEditorStart = true;
        break;
    default:
        break;
    }

    if(d->haveGlobalEditor() && suppressDuplicateEditorStart) {
        if (!d->infoMessage->isVisible() && !d->infoMessage->isShowAnimationRunning()) {
            d->infoMessage->resize(viewport()->width(), d->infoMessage->height());
            d->infoMessage->setText(i18n("You are already editing a transaction in another view. KMyMoney does not support editing two transactions in parallel."));
            d->infoMessage->setMessageType(KMessageWidget::Warning);
            d->infoMessage->animatedShow();
        }

    } else {
        bool rc = QTableView::edit(index, trigger, event);

        if(rc) {
            // editing started, but we need the editor to cover all columns
            // so we close it, set the span to have a single row and recreate
            // the editor in that single cell
            closeEditor(indexWidget(index), QAbstractItemDelegate::NoHint);

            d->registerGlobalEditor();
            d->infoMessage->animatedHide();

            emit aboutToStartEdit();
            setSpan(index.row(), 0, 1, horizontalHeader()->count());
            d->editIndex = model()->index(index.row(), 0);

            rc = QTableView::edit(d->editIndex, trigger, event);

            // make sure that the row gets resized according to the requirements of the editor
            // and is completely visible
            const auto editor = qobject_cast<TransactionEditorBase*>(indexWidget(d->editIndex));
            connect(editor, &TransactionEditorBase::editorLayoutChanged, this, &LedgerView::resizeEditorRow);

            resizeEditorRow();
        }
        return rc;
    }
    return false;
}

void LedgerView::resizeEditorRow()
{
    resizeRowToContents(d->editIndex.row());
    d->ensureEditorFullyVisible(d->editIndex);
    QMetaObject::invokeMethod(this, "ensureCurrentItemIsVisible", Qt::QueuedConnection);
}

void LedgerView::closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint)
{
    QTableView::closeEditor(editor, hint);
    clearSpans();

    d->unregisterGlobalEditor();

    // we need to resize the row that contained the editor.
    resizeRowsToContents();

    emit aboutToFinishEdit();

    d->editIndex = QModelIndex();
    QMetaObject::invokeMethod(this, "ensureCurrentItemIsVisible", Qt::QueuedConnection);
}

QModelIndex LedgerView::editIndex() const
{
    return d->editIndex;
}

bool LedgerView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto helpEvent = static_cast<QHelpEvent*>(event);

        // get the row, if it's the header, then we're done
        // otherwise, adjust the row to be 0 based.
        const auto col = columnAt(helpEvent->x());
        const auto row = rowAt(helpEvent->y());
        const auto idx = model()->index(row, col);

        if (col == JournalModel::Column::Detail) {
            bool preventLineBreak(false);
            int iconIndex = d->iconClickIndex(idx, helpEvent->pos());

            QVector<QString> tooltips(JournalDelegate::maxIcons());
            if (iconIndex != -1) {
                int iconCount(0);
                if (idx.data(eMyMoney::Model::TransactionErroneousRole).toBool()) {
                    if (idx.data(eMyMoney::Model::Roles::TransactionSplitCountRole).toInt() < 2) {
                        tooltips[iconCount] = i18nc("@info:tooltip icon description", "Transaction is missing a category assignment.");
                    } else {
                        const auto acc = MyMoneyFile::instance()->account(d->accountId);
                        const auto sec = MyMoneyFile::instance()->security(acc.currencyId());
                        // don't allow line break between amount and currency symbol
                        tooltips[iconCount] =
                            i18nc("@info:tooltip icon description",
                                  "The transaction has a missing assignment of <b>%1</b>.",
                                  MyMoneyUtils::formatMoney(idx.data(eMyMoney::Model::TransactionSplitSumRole).value<MyMoneyMoney>().abs(), acc, sec));
                    }
                    preventLineBreak = true;
                    ++iconCount;

                } else if (idx.data(eMyMoney::Model::ScheduleIsOverdueRole).toBool()) {
                    tooltips[iconCount] = i18nc("@info:tooltip icon description", "This schedule is overdue. Click on the icon to enter it.");
                    ++iconCount;
                }

                if (idx.data(eMyMoney::Model::TransactionIsImportedRole).toBool()) {
                    tooltips[iconCount] = i18nc("@info:tooltip icon description", "This transaction is imported. Click on the icon to accept it.");
                    ++iconCount;
                }

                if (idx.data(eMyMoney::Model::JournalSplitIsMatchedRole).toBool()) {
                    tooltips[iconCount] = i18nc("@info:tooltip icon description", "This transaction is matched. Click on the icon to accept or un-match it.");
                    ++iconCount;
                }

            } else {
                tooltips[0] = d->createSplitTooltip(idx);
                iconIndex = 0;
            }

            if ((iconIndex != -1) && !tooltips[iconIndex].isEmpty()) {
                auto text = tooltips[iconIndex];
                if (preventLineBreak) {
                    text = QString("<p style='white-space:pre'>%1</p>").arg(text);
                }
                QToolTip::showText(helpEvent->globalPos(), tooltips[iconIndex]);
                return true;
            }

        } else if ((col == JournalModel::Column::Payment) || (col == JournalModel::Column::Deposit)) {
            if (!idx.data(Qt::DisplayRole).toString().isEmpty()) {
                auto tip = d->createSplitTooltip(idx);
                if (!tip.isEmpty()) {
                    QToolTip::showText(helpEvent->globalPos(), tip);
                    return true;
                }
            }
        }

        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QTableView::viewportEvent(event);
}

void LedgerView::mousePressEvent(QMouseEvent* event)
{
    if ((state() != QAbstractItemView::EditingState) && (event->button() == Qt::LeftButton)) {
        QTableView::mousePressEvent(event);
        // a click on the reconciliation column triggers the Mark transaction action
        switch (columnAt(event->pos().x())) {
        case JournalModel::Column::Reconciliation:
            pActions[eMenu::Action::ToggleReconciliationFlag]->trigger();
            break;

        case JournalModel::Column::Detail: {
            const auto col = columnAt(event->x());
            const auto row = rowAt(event->y());
            const auto idx = model()->index(row, col);
            const auto iconIndex = d->iconClickIndex(idx, event->pos());
            const auto statusRoles = this->statusRoles(idx);

            KGuiItem buttonYes = KStandardGuiItem::yes();
            KGuiItem buttonNo = KStandardGuiItem::no();
            KGuiItem buttonCancel = KStandardGuiItem::cancel();
            KMessageBox::ButtonCode result;

            if (iconIndex != -1 && (iconIndex < statusRoles.count())) {
                switch (statusRoles[iconIndex]) {
                case eMyMoney::Model::ScheduleIsOverdueRole:
                    buttonNo.setToolTip(i18nc("@info:tooltip No button", "Do not enter the overdue scheduled transaction."));
                    buttonYes.setToolTip(i18nc("@info:tooltip Yes button", "Enter the overdue scheduled transaction."));

                    result = KMessageBox::questionYesNo(this,
                                                        i18nc("Question about the overdue action", "Do you want to enter the overdue schedule now?"),
                                                        i18nc("@title:window", "Enter overdue schedule"),
                                                        buttonYes,
                                                        buttonNo);
                    if (result == KMessageBox::ButtonCode::Yes) {
                        pActions[eMenu::Action::EnterSchedule]->setData(idx.data(eMyMoney::Model::JournalTransactionIdRole).toString());
                        pActions[eMenu::Action::EnterSchedule]->trigger();
                    }
                    break;
                case eMyMoney::Model::TransactionIsImportedRole:
                    buttonNo.setToolTip(QString());
                    buttonYes.setToolTip(i18nc("@info:tooltip Accept button", "Accept the transaction and mark it cleared."));

                    result = KMessageBox::questionYesNo(this,
                                                        i18nc("Question about the accept action", "Do you want to accept the imported transaction now?"),
                                                        i18nc("@title:window", "Accept transaction"),
                                                        buttonYes,
                                                        buttonNo);
                    if (result == KMessageBox::ButtonCode::Yes) {
                        pActions[eMenu::Action::AcceptTransaction]->trigger();
                    }
                    break;
                case eMyMoney::Model::JournalSplitIsMatchedRole: {
                    buttonYes.setText(pActions[eMenu::Action::AcceptTransaction]->text());
                    buttonYes.setIcon(pActions[eMenu::Action::AcceptTransaction]->icon());
                    const auto unmatchAction = qobject_cast<KDualAction*>(pActions[eMenu::Action::MatchTransaction]);
                    if (unmatchAction) {
                        unmatchAction->setActive(false);
                        buttonNo.setText(pActions[eMenu::Action::MatchTransaction]->text());
                        buttonNo.setIcon(pActions[eMenu::Action::MatchTransaction]->icon());
                        buttonNo.setToolTip(i18nc("@info:tooltip Unmatch button",
                                                  "Detach the hidden (matched) transaction from the one shown and enter it into the ledger again."));
                        buttonYes.setToolTip(
                            i18nc("@info:tooltip Accept button", "Accept the match as shown and remove the data of the hidden (matched) transaction."));

                        result = KMessageBox::questionYesNoCancel(
                            this,
                            i18nc("Question about the accept or unmatch action", "Do you want to accept or unmatch the matched transaction now?"),
                            i18nc("@title:window", "Accept or unmatch transaction"),
                            buttonYes,
                            buttonNo,
                            buttonCancel);
                        switch (result) {
                        case KMessageBox::ButtonCode::Yes:
                            pActions[eMenu::Action::AcceptTransaction]->trigger();
                            break;
                        case KMessageBox::ButtonCode::No:
                            pActions[eMenu::Action::MatchTransaction]->trigger();
                            break;
                        default:
                            break;
                        }
                    }
                    break;
                }
                default:
                    break;
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

void LedgerView::mouseMoveEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    // qDebug() << "mouseMoveEvent";
    // QTableView::mouseMoveEvent(event);
}

void LedgerView::mouseDoubleClickEvent(QMouseEvent* event)
{
    // qDebug() << "mouseDoubleClickEvent";
    QTableView::mouseDoubleClickEvent(event);
}

void LedgerView::wheelEvent(QWheelEvent* e)
{
    // qDebug() << "wheelEvent";
    QTableView::wheelEvent(e);
}

void LedgerView::keyPressEvent(QKeyEvent* kev)
{
    if ((d->infoMessage->isVisible()) && kev->matches(QKeySequence::Cancel)) {
        kev->accept();
        d->infoMessage->animatedHide();
    } else {
        QTableView::keyPressEvent(kev);
    }
}

void LedgerView::currentChanged(const QModelIndex& current, const QModelIndex& previous)
{
    QTableView::currentChanged(current, previous);

    if(current.isValid()) {
        QModelIndex idx = current.model()->index(current.row(), 0);
        QString id = idx.data(eMyMoney::Model::IdRole).toString();
        // For a new transaction the id is completely empty, for a split view the transaction
        // part is filled but the split id is empty and the string ends with a dash
        if(id.isEmpty() || id.endsWith('-')) {
            // the next two lines prevent an endless recursive call of this method
            if (idx == previous) {
                return;
            }
            // check for an empty account being opened. we can detect
            // that by an invalid previous index and don't start
            // editing right away.
            if (!previous.isValid()) {
                selectRow(idx.row());
                return;
            }
            selectionModel()->clearSelection();
            setCurrentIndex(idx);
            selectRow(idx.row());
            scrollTo(idx, QAbstractItemView::PositionAtBottom);
            edit(idx);
        } else {
            scrollTo(idx, EnsureVisible);
            emit transactionSelected(idx);
        }
        QMetaObject::invokeMethod(this, "doItemsLayout", Qt::QueuedConnection);
    }
}

void LedgerView::moveEvent(QMoveEvent* event)
{
    // qDebug() << "moveEvent";
    QWidget::moveEvent(event);
}

void LedgerView::paintEvent(QPaintEvent* event)
{
    QTableView::paintEvent(event);

    // the base class implementation paints the regular grid in case there
    // is room below the last line and the bottom of the viewport. We check
    // here if that is the case and fill that part with the base color to
    // remove the false painted grid.

    const QHeaderView *verticalHeader = this->verticalHeader();
    if(verticalHeader->count() == 0)
        return;

    int lastVisualRow = verticalHeader->visualIndexAt(verticalHeader->viewport()->height());
    if (lastVisualRow == -1)
        lastVisualRow = model()->rowCount(QModelIndex()) - 1;

    while(lastVisualRow >= model()->rowCount(QModelIndex()))
        --lastVisualRow;

    while ((lastVisualRow > -1) && verticalHeader->isSectionHidden(verticalHeader->logicalIndex(lastVisualRow)))
        --lastVisualRow;

    int top = 0;
    if(lastVisualRow != -1)
        top = verticalHeader->sectionViewportPosition(lastVisualRow) + verticalHeader->sectionSize(lastVisualRow);

    if(top < viewport()->height()) {
        QPainter painter(viewport());
        QRect rect(0, top, viewport()->width(), viewport()->height()-top);
        painter.fillRect(rect, QBrush(palette().base()));
    }
}

QVector<eMyMoney::Model::Roles> LedgerView::statusRoles(const QModelIndex& idx) const
{
    return d->statusRoles(idx);
}

void LedgerView::setSingleLineDetailRole(eMyMoney::Model::Roles role)
{
    d->setSingleLineDetailRole(role);
}

int LedgerView::sizeHintForColumn(int col) const
{
    if (col == JournalModel::Column::Reconciliation) {
        QStyleOptionViewItem opt;
        const QModelIndex index = model()->index(0, col);
        const auto delegate = d->delegateProxy->delegate(index);
        if (delegate) {
            int hint = delegate->sizeHint(opt, index).width();
            if(showGrid())
                hint += 1;
            return hint;
        }
    }
    return QTableView::sizeHintForColumn(col);
}

int LedgerView::sizeHintForRow(int row) const
{
    // we can optimize the sizeHintForRow() operation by asking the
    // delegate about the height. There's no need to use the std
    // method which scans over all items in a column and takes a long
    // time in large ledgers. In case the editor is open in the row, we
    // use the regular method.
    // We always ask for the detail column as this varies in height
    ensurePolished();

    const QModelIndex index = model()->index(row, JournalModel::Column::Detail);
    const auto delegate = d->delegateProxy->delegate(index);
    const auto journalDelegate = qobject_cast<const JournalDelegate*>(delegate);

    if(journalDelegate && (journalDelegate->editorRow() != row)) {
        QStyleOptionViewItem opt;
        opt.state |= (row == currentIndex().row()) ? QStyle::State_Selected : QStyle::State_None;
        int hint = delegate->sizeHint(opt, index).height();
        if(showGrid())
            hint += 1;
        return hint;
    }

    return QTableView::sizeHintForRow(row);
}

void LedgerView::resizeEvent(QResizeEvent* event)
{
    // qDebug() << "resizeEvent, old:" << event->oldSize() << "new:" << event->size() << "viewport:" << viewport()->width();
    QTableView::resizeEvent(event);
    adjustDetailColumn(event->size().width());
    d->infoMessage->resize(viewport()->width(), d->infoMessage->height());
    d->infoMessage->setWordWrap(false);
    d->infoMessage->setWordWrap(true);
    d->infoMessage->setText(d->infoMessage->text());
}

void LedgerView::adjustDetailColumn(int newViewportWidth)
{
    // make sure we don't get here recursively
    if(d->adjustingColumn)
        return;

    d->adjustingColumn = true;

    QHeaderView* header = horizontalHeader();

    int totalColumnWidth = 0;
    for(int i=0; i < header->count(); ++i) {
        if(header->isSectionHidden(i)) {
            continue;
        }
        totalColumnWidth += header->sectionSize(i);
    }
    const int delta = newViewportWidth - totalColumnWidth;
    const int newWidth = header->sectionSize(d->adjustableColumn) + delta;
    if(newWidth > 10) {
        header->resizeSection(d->adjustableColumn, newWidth);
    }

    // remember that we're done this time
    d->adjustingColumn = false;
}

void LedgerView::ensureCurrentItemIsVisible()
{
    scrollTo(currentIndex(), EnsureVisible);
}

void LedgerView::slotSettingsChanged()
{
    updateGeometries();
#if 0

    // KMyMoneySettings::showGrid()
    // KMyMoneySettings::sortNormalView()
    // KMyMoneySettings::ledgerLens()
    // KMyMoneySettings::showRegisterDetailed()
    d->m_proxyModel->setHideClosedAccounts(!KMyMoneySettings::showAllAccounts());
    d->m_proxyModel->setHideEquityAccounts(!KMyMoneySettings::expertMode());
    d->m_proxyModel->setHideFavoriteAccounts(true);
#endif
}

void LedgerView::selectMostRecentTransaction()
{
    if (model()->rowCount() > 0) {

        // we need to check that the last row may contain a scheduled transaction or
        // the row that is shown for new transacations or a special entry (e.g.
        // online balance or date mark).
        // in that case, we need to go back to find the actual last transaction
        int row = model()->rowCount()-1;
        const auto journalModel = MyMoneyFile::instance()->journalModel();
        while(row >= 0) {
            const auto idx = model()->index(row, 0);
            if (MyMoneyFile::baseModel()->baseModel(idx) == journalModel) {
                setCurrentIndex(idx);
                selectRow(idx.row());
                scrollTo(idx, QAbstractItemView::PositionAtBottom);
                break;
            }
            row--;
        }
    }
}

void LedgerView::editNewTransaction()
{
    const auto row = model()->rowCount() - 1;
    const auto idx = model()->index(row, 0);
    if (idx.data(eMyMoney::Model::IdRole).toString().isEmpty()) {
        scrollTo(idx, QAbstractItemView::EnsureVisible);
        selectRow(idx.row());
        setCurrentIndex(idx);
    }
}

void LedgerView::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    // call base class implementation
    QTableView::selectionChanged(selected, deselected);

    QSet<int> allSelectedRows;
    QSet<int> selectedRows;

    // we need to remember the first item selected as this
    // should always be reported as the first item in the
    // list of selected journalEntries. We have to devide
    // the number of selected indexes by the column count
    // to get the number of selected rows.
    switch (selectionModel()->selectedIndexes().count() / model()->columnCount()) {
    case 0:
        d->firstSelectedId.clear();
        break;
    case 1:
        d->firstSelectedId = selectionModel()->selectedIndexes().first().data(eMyMoney::Model::IdRole).toString();
        break;
    default:
        break;
    }

    if (!selected.isEmpty()) {
        int lastRow = -1;
        for (const auto& idx : selectionModel()->selectedIndexes()) {
            if (idx.row() != lastRow) {
                lastRow = idx.row();
                allSelectedRows += lastRow;
            }
        }
        lastRow = -1;
        for (const auto& idx : selected.indexes()) {
            if (idx.row() != lastRow) {
                lastRow = idx.row();
                selectedRows += lastRow;
            }
        }

        allSelectedRows -= selectedRows;
        // determine the current type of selection by looking at
        // the first item in allSelectedRows. In case allSelectedRows
        // is empty, a single item was selected and we are good to go
        if (!allSelectedRows.isEmpty()) {
            const auto baseIdx = model()->index(*allSelectedRows.constBegin(), 0);
            const auto isSchedule = baseIdx.data(eMyMoney::Model::TransactionScheduleRole).toBool();

            // now scan all in selected to check if they are of the same type
            // and add them to toDeselect if not.
            QItemSelection toDeselect;
            for (const auto& idx : selected.indexes()) {
                if (idx.data(eMyMoney::Model::TransactionScheduleRole).toBool() != isSchedule) {
                    toDeselect.select(idx, idx);
                }
            }
            if (!toDeselect.isEmpty()) {
                selectionModel()->select(toDeselect, QItemSelectionModel::Deselect);
                /// @TODO: may be, we should inform the user why we deselect here
            }
        }
    }

    // build the list of selected journalEntryIds
    // and make sure the first selected is the first listed
    QStringList selectedJournalEntries;

    int lastRow = -1;
    bool firstSelectedStillPresent(false);

    for (const auto& idx : selectionModel()->selectedIndexes()) {
        if (idx.row() != lastRow) {
            lastRow = idx.row();
            if (d->firstSelectedId != idx.data(eMyMoney::Model::IdRole).toString()) {
                selectedJournalEntries += idx.data(eMyMoney::Model::IdRole).toString();
            } else {
                firstSelectedStillPresent = true;
            }
        }
    }

    // in case we still have the first selected id, we prepend
    // it to the list. Otherwise, if the list is not empty, we
    // use the now first entry to have one in place.
    if (firstSelectedStillPresent && !d->firstSelectedId.isEmpty()) {
        selectedJournalEntries.prepend(d->firstSelectedId);
    } else if (!selectedJournalEntries.isEmpty()) {
        d->firstSelectedId = selectedJournalEntries.first();
    }

    d->selection.setSelection(SelectedObjects::JournalEntry, selectedJournalEntries);
    emit transactionSelectionChanged(d->selection);
}

QStringList LedgerView::selectedJournalEntries() const
{
    QStringList selection;

    int lastRow = -1;
    QString id;
    for (const auto& idx : selectionModel()->selectedIndexes()) {
        // we don't need to process all columns but only the first one
        if (idx.row() != lastRow) {
            lastRow = idx.row();
            id = idx.data(eMyMoney::Model::IdRole).toString();
            if (!selection.contains(id)) {
                selection.append(id);
            }
        }
    }
    return selection;
}

void LedgerView::reselectJournalEntry(const QString& journalEntryId)
{
    const auto journalModel = MyMoneyFile::instance()->journalModel();
    const auto baseIdx = journalModel->indexById(journalEntryId);
    auto row = journalModel->mapFromBaseSource(model(), baseIdx).row();
    if (row != -1) {
        setSelectedJournalEntries(QStringList(journalEntryId));
    } else {
        // it could be, that the journal id changed due to a date
        // change. In this case, the transaction id is still the same
        // so we check if we find it.
        const auto newJournalEntryId = journalModel->updateJournalId(journalEntryId);
        if (!newJournalEntryId.isEmpty()) {
            setSelectedJournalEntries(QStringList(newJournalEntryId));
        }
    }
}

void LedgerView::setSelectedJournalEntries(const QStringList& journalEntryIds)
{
    QItemSelection selection;
    const auto journalModel = MyMoneyFile::instance()->journalModel();
    const auto lastColumn = model()->columnCount()-1;
    int startRow = -1;
    int lastRow = -1;
    QModelIndex currentIdx;

    auto createSelectionRange = [&]() {
        if (startRow != -1) {
            selection.select(model()->index(startRow, 0), model()->index(lastRow, lastColumn));
            startRow = -1;
        }
    };

    for (const auto& id : journalEntryIds) {
        if (id.isEmpty())
            continue;
        int row = -1;
        const auto baseIdx = journalModel->indexById(id);
        row = journalModel->mapFromBaseSource(model(), baseIdx).row();

        // the baseIdx may point to a split in a different account which
        // we don't see here. In this case, we scan the journal entries
        // of the transaction
        if ((row == -1) && baseIdx.isValid()) {
            const auto indexes = journalModel->indexesByTransactionId(baseIdx.data(eMyMoney::Model::JournalTransactionIdRole).toString());
            for (const auto idx : indexes) {
                if (idx.data(eMyMoney::Model::JournalSplitAccountIdRole).toString() == d->accountId) {
                    row = journalModel->mapFromBaseSource(model(), idx).row();
                    if (row != -1) {
                        break;
                    }
                }
            }
        }

        if (row == -1) {
            qDebug() << "transaction" << id << "not found anymore for selection. skipped";
            continue;
        }

        if (startRow == -1) {
            startRow = row;
            lastRow = row;
            // use the first as the current index
            if (!currentIdx.isValid()) {
                currentIdx = model()->index(startRow, 0);
            }
        } else {
            if (row == lastRow+1) {
                lastRow = row;
            } else {
                // a new range start, so we take care of it
                createSelectionRange();
            }
        }
    }

    // if no selection has been setup but we have
    // transactions in the ledger, we select the
    // last. The very last entry is the empty line,
    // so we have to skip that.
    if ((lastRow == -1) && (model()->rowCount() > 1)) {
        // find the last 'real' transaction
        startRow = model()->rowCount()-1;
        do {
            --startRow;
            currentIdx = model()->index(startRow, 0);
        } while (startRow > 0 && journalModel->baseModel(currentIdx) != journalModel);
        lastRow = startRow;
    }

    // add a possibly dangling range
    createSelectionRange();

    selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    setCurrentIndex(currentIdx);
}

void LedgerView::selectAllTransactions()
{
    QItemSelection selection;
    const auto journalModel = MyMoneyFile::instance()->journalModel();
    int startRow = -1;
    int lastRow = -1;
    const auto lastColumn = model()->columnCount() - 1;
    const auto rows = model()->rowCount();

    auto createSelectionRange = [&]() {
        if (startRow != -1) {
            selection.select(model()->index(startRow, 0), model()->index(lastRow, lastColumn));
            startRow = -1;
        }
    };

    for (auto row = 0; row < rows; ++row) {
        const auto idx = model()->index(row, 0);
        if (!idx.data(eMyMoney::Model::JournalTransactionIdRole).toString().isEmpty()) {
            auto baseIdx = journalModel->mapToBaseSource(idx);
            if (baseIdx.model() == journalModel) {
                if (startRow == -1) {
                    startRow = row;
                    lastRow = row;
                } else {
                    if (row == (lastRow + 1)) {
                        lastRow = row;
                    } else {
                        // a new range start, so we take care of it
                        createSelectionRange();
                    }
                }
            } else {
                // a range ends, so we take care of it
                createSelectionRange();
            }
        } else {
            // a range ends, so we take care of it
            createSelectionRange();
        }
    }
    // add a possibly dangling range
    createSelectionRange();

    selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
}

void LedgerView::slotMoveToAccount(const QString& accountId)
{
    // close the menu, if it is still open
    if (pMenus[eMenu::Menu::Transaction]->isVisible()) {
        pMenus[eMenu::Menu::Transaction]->close();
    }

    pActions[eMenu::Action::MoveTransactionTo]->setData(accountId);
    pActions[eMenu::Action::MoveTransactionTo]->activate(QAction::Trigger);
}
