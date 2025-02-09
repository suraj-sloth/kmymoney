/*
    SPDX-FileCopyrightText: 2000-2003 Michael Edwardes <mte@users.sourceforge.net>
    SPDX-FileCopyrightText: 2001 Felix Rodriguez <frodriguez@users.sourceforge.net>
    SPDX-FileCopyrightText: 2017-2018 Łukasz Wojniłowicz <lukasz.wojnilowicz@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kmymoneydateinput.h"
#include "kmymoneysettings.h"

// ----------------------------------------------------------------------------
// QT Includes

#include <QPoint>
#include <QApplication>
#include <QDesktopWidget>
#include <QTimer>
#include <QLabel>
#include <QKeyEvent>
#include <QEvent>
#include <QDateEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QIcon>
#include <QVBoxLayout>

// ----------------------------------------------------------------------------
// KDE Includes

#include <KLocalizedString>
#include <KPassivePopup>
#include <KDatePicker>

// ----------------------------------------------------------------------------
// Project Includes

#include "icons.h"
#include "popuppositioner.h"

using namespace Icons;

namespace
{
const int DATE_POPUP_TIMEOUT = 1500;
const QDate INVALID_DATE = QDate(1800, 1, 1);
}

KMyMoney::OldDateEdit::OldDateEdit(const QDate& date, QWidget* parent)
    : QDateEdit(date, parent)
    , m_initialSection(QDateTimeEdit::DaySection)
    , m_initStage(Created)
{
}

void KMyMoney::OldDateEdit::keyPressEvent(QKeyEvent* k)
{
    if ((lineEdit()->text().isEmpty() || lineEdit()->selectedText() == lineEdit()->text()) && QChar(k->key()).isDigit()) {
        // the line edit is empty which means that the date was cleared
        // or the whole text is selected and a digit character was entered
        // (the same meaning as clearing the date) - in this case set the date
        // to the current date and let the editor do the actual work
        setDate(QDate::currentDate());
        setSelectedSection(m_initialSection); // start as when focused in if the date was cleared
    }
    QDateEdit::keyPressEvent(k);
}

void KMyMoney::OldDateEdit::focusInEvent(QFocusEvent * event)
{
    QDateEdit::focusInEvent(event);
    setSelectedSection(m_initialSection);
}

bool KMyMoney::OldDateEdit::event(QEvent* e)
{
    // make sure that we keep the current date setting of a KMyMoneyDateInput object
    // across the QDateEdit::event(FocusOutEvent)
    bool rc;

    KMyMoneyDateInput* p = dynamic_cast<KMyMoneyDateInput*>(parentWidget());
    if (e->type() == QEvent::FocusOut && p) {
        QDate d = p->date();
        rc = QDateEdit::event(e);
        if (d.isValid())
            d = p->date();
        p->loadDate(d);
    } else {
        rc = QDateEdit::event(e);
    }
    switch (m_initStage) {
    case Created:
        if (e->type() == QEvent::FocusIn) {
            m_initStage = GotFocus;
        }
        break;
    case GotFocus:
        if (e->type() == QEvent::MouseButtonPress) {
            // create a phony corresponding release event
            QMouseEvent* mev = static_cast<QMouseEvent*>(e);
            QMouseEvent release(QEvent::MouseButtonRelease,
                                mev->localPos(),
                                mev->windowPos(),
                                mev->screenPos(),
                                mev->button(),
                                mev->buttons(),
                                mev->modifiers(),
                                mev->source());
            QApplication::sendEvent(this, &release);
            m_initStage = FirstMousePress;
        }
        break;
    case FirstMousePress:
        break;
    }
    return rc;
}

bool KMyMoney::OldDateEdit::focusNextPrevChild(bool next)
{
    Q_UNUSED(next)
    return true;
}

struct KMyMoneyDateInput::Private {
    KMyMoney::OldDateEdit *m_dateEdit;
    KDatePicker *m_datePicker;
    QDate m_date;
    QDate m_prevDate;
    Qt::AlignmentFlag m_qtalignment;
    QWidget *m_dateFrame;
    QPushButton *m_dateButton;
    KPassivePopup *m_datePopup;
};

KMyMoneyDateInput::KMyMoneyDateInput(QWidget *parent, Qt::AlignmentFlag flags)
    : QWidget(parent), d(new Private)
{
    d->m_qtalignment = flags;
    d->m_date = QDate::currentDate();

    QHBoxLayout *dateInputLayout = new QHBoxLayout(this);
    dateInputLayout->setSpacing(0);
    dateInputLayout->setContentsMargins(0, 0, 0, 0);
    d->m_dateEdit = new KMyMoney::OldDateEdit(d->m_date, this);
    dateInputLayout->addWidget(d->m_dateEdit, 3);
    setFocusProxy(d->m_dateEdit);
    d->m_dateEdit->installEventFilter(this); // To get d->m_dateEdit's FocusIn/Out and some KeyPress events

    // we use INVALID_DATE as a special value for multi transaction editing
    d->m_dateEdit->setMinimumDate(INVALID_DATE);
    d->m_dateEdit->setSpecialValueText(QLatin1String(" "));

    d->m_datePopup = new KPassivePopup(d->m_dateEdit);
    d->m_datePopup->setObjectName("datePopup");
    d->m_datePopup->setTimeout(DATE_POPUP_TIMEOUT);
    d->m_datePopup->setView(new QLabel(QLocale().toString(d->m_date), d->m_datePopup));

    d->m_dateFrame = new QWidget(this);
    dateInputLayout->addWidget(d->m_dateFrame);
    QVBoxLayout *dateFrameVBoxLayout = new QVBoxLayout(d->m_dateFrame);
    dateFrameVBoxLayout->setMargin(0);
    dateFrameVBoxLayout->setContentsMargins(0, 0, 0, 0);
    d->m_dateFrame->setWindowFlags(Qt::Popup);
    d->m_dateFrame->hide();

    d->m_dateEdit->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
    switch(KMyMoneySettings::initialDateFieldCursorPosition()) {
    case KMyMoneySettings::Day:
        d->m_dateEdit->setInitialSection(QDateTimeEdit::DaySection);
        break;
    case KMyMoneySettings::Month:
        d->m_dateEdit->setInitialSection(QDateTimeEdit::MonthSection);
        break;
    case KMyMoneySettings::Year:
        d->m_dateEdit->setInitialSection(QDateTimeEdit::YearSection);
        break;
    }

    d->m_datePicker = new KDatePicker(d->m_date, d->m_dateFrame);
    dateFrameVBoxLayout->addWidget(d->m_datePicker);
    // Let the date picker have a close button (Added in 3.1)
    d->m_datePicker->setCloseButton(true);

    // the next line is a try to add an icon to the button
    d->m_dateButton = new QPushButton(Icons::get(Icon::CalendarDay), QString(), this);
    dateInputLayout->addWidget(d->m_dateButton);

    connect(d->m_dateButton, &QAbstractButton::clicked, this, &KMyMoneyDateInput::toggleDatePicker);
    connect(d->m_dateEdit, &QDateTimeEdit::dateChanged, this, &KMyMoneyDateInput::slotDateChosenRef);
    connect(d->m_datePicker, &KDatePicker::dateSelected, this, &KMyMoneyDateInput::slotDateChosen);
    connect(d->m_datePicker, &KDatePicker::dateEntered, this, &KMyMoneyDateInput::slotDateChosen);
    connect(d->m_datePicker, &KDatePicker::dateSelected, d->m_dateFrame, &QWidget::hide);
}

void KMyMoneyDateInput::markAsBadDate(bool bad, const QColor& color)
{
    // the next line knows a bit about the internals of QAbstractSpinBox
    QLineEdit* le = d->m_dateEdit->findChild<QLineEdit *>(); //krazy:exclude=qclasses

    if (le) {
        QPalette palette = this->palette();
        le->setPalette(palette);
        if (bad) {
            palette.setColor(foregroundRole(), color);
            le->setPalette(palette);
        }
    }
}

void KMyMoneyDateInput::showEvent(QShowEvent* event)
{
    // don't forget the standard behaviour  ;-)
    QWidget::showEvent(event);

    // If the widget is shown, the size must be fixed a little later
    // to be appropriate. I saw this in some other places and the only
    // way to solve this problem is to postpone the setup of the size
    // to the time when the widget is on the screen.
    QTimer::singleShot(50, this, SLOT(fixSize()));
}

void KMyMoneyDateInput::fixSize()
{
    // According to a hint in the documentation of KDatePicker::sizeHint()
    // 28 pixels should be added in each direction to obtain a better
    // display of the month button. I decided, (22,14) is good
    // enough and save some space on the screen (ipwizard)
    d->m_dateFrame->setFixedSize(d->m_datePicker->sizeHint() + QSize(22, 14));
}

KMyMoneyDateInput::~KMyMoneyDateInput()
{
    delete d->m_dateFrame;
    delete d->m_datePopup;
    delete d;
}

void KMyMoneyDateInput::toggleDatePicker()
{
    if (d->m_dateFrame->isVisible()) {
        d->m_dateFrame->hide();
    } else {
        PopupPositioner pos(d->m_dateButton, d->m_dateFrame, PopupPositioner::BottomRight);
        if (d->m_date.isValid() && d->m_date != INVALID_DATE) {
            d->m_datePicker->setDate(d->m_date);
        } else {
            d->m_datePicker->setDate(QDate::currentDate());
        }
        d->m_dateFrame->show();
    }
}


/** Overriding QWidget::keyPressEvent
  *
  * increments/decrements the date upon +/- or Up/Down key input
  * sets the date to current date when the 'T' key is pressed
  */
void KMyMoneyDateInput::keyPressEvent(QKeyEvent * k)
{
    QKeySequence today(i18nc("Enter todays date into date input widget", "T"));

    auto adjustDateSection = [&](int offset) {
        switch(d->m_dateEdit->currentSection()) {
        case QDateTimeEdit::DaySection:
            slotDateChosen(d->m_date.addDays(offset));
            break;
        case QDateTimeEdit::MonthSection:
            slotDateChosen(d->m_date.addMonths(offset));
            break;
        case QDateTimeEdit::YearSection:
            slotDateChosen(d->m_date.addYears(offset));
            break;
        default:
            break;
        }
    };

    switch (k->key()) {
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        adjustDateSection(1);
        k->accept();
        break;

    case Qt::Key_Minus:
        adjustDateSection(-1);
        k->accept();
        break;

    default:
        if (today == QKeySequence(k->key()) || k->key() == Qt::Key_T) {
            slotDateChosen(QDate::currentDate());
            k->accept();
        }
        break;
    }
    k->ignore(); // signal that the key event was not handled
}

/**
  * This function receives all events that are sent to focusWidget().
  * Some KeyPress events are intercepted and passed to keyPressEvent.
  * Otherwise they would be consumed by QDateEdit.
  */
bool KMyMoneyDateInput::eventFilter(QObject *, QEvent *e)
{
    if (e->type() == QEvent::FocusIn) {
#ifndef Q_OS_MAC
        d->m_datePopup->show(mapToGlobal(QPoint(0, height())));
#endif
        // select the date section, but we need to delay it a bit
    } else if (e->type() == QEvent::FocusOut) {
#ifndef Q_OS_MAC
        d->m_datePopup->hide();
#endif
    } else if (e->type() == QEvent::KeyPress) {
        if (QKeyEvent *k = dynamic_cast<QKeyEvent*>(e)) {
            keyPressEvent(k);
            if (k->isAccepted())
                return true; // signal that the key event was handled
        }
    }

    return false; // Don't filter the event
}

void KMyMoneyDateInput::slotDateChosenRef(const QDate& date)
{
    if (date.isValid()) {
        emit dateChanged(date);
        d->m_date = date;

#ifndef Q_OS_MAC
        QLabel *lbl = static_cast<QLabel*>(d->m_datePopup->view());
        lbl->setText(QLocale().toString(date));
        lbl->adjustSize();
        if (d->m_datePopup->isVisible() || hasFocus())
            d->m_datePopup->show(mapToGlobal(QPoint(0, height()))); // Repaint
#endif
    }
}

void KMyMoneyDateInput::slotDateChosen(QDate date)
{
    if (date.isValid()) {
        // the next line implies a call to slotDateChosenRef() above
        d->m_dateEdit->setDate(date);
    } else {
        d->m_dateEdit->setDate(INVALID_DATE);
    }
}

QDate KMyMoneyDateInput::date() const
{
    QDate rc = d->m_dateEdit->date();
    if (rc == INVALID_DATE)
        rc = QDate();
    return rc;
}

void KMyMoneyDateInput::setDate(QDate date)
{
    slotDateChosen(date);
}

void KMyMoneyDateInput::loadDate(const QDate& date)
{
    d->m_date = d->m_prevDate = date;

    blockSignals(true);
    slotDateChosen(date);
    blockSignals(false);
}

void KMyMoneyDateInput::resetDate()
{
    setDate(d->m_prevDate);
}

void KMyMoneyDateInput::setMaximumDate(const QDate& max)
{
    d->m_dateEdit->setMaximumDate(max);
}

QWidget* KMyMoneyDateInput::focusWidget() const
{
    QWidget* w = d->m_dateEdit;
    while (w->focusProxy())
        w = w->focusProxy();
    return w;
}
/*
void KMyMoneyDateInput::setRange(const QDate & min, const QDate & max)
{
  d->m_dateEdit->setDateRange(min, max);
}
*/
