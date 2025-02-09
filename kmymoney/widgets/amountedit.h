/*
    SPDX-FileCopyrightText: 2010-2020 Thomas Baumgart <tbaumgart@kde.org>
    SPDX-FileCopyrightText: 2017-2018 Łukasz Wojniłowicz <lukasz.wojnilowicz@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef AMOUNTEDIT_H
#define AMOUNTEDIT_H

#include "kmm_base_widgets_export.h"

// ----------------------------------------------------------------------------
// QT Includes

#include <QLineEdit>

// ----------------------------------------------------------------------------
// KDE Includes

// ----------------------------------------------------------------------------
// Project Includes

#include "mymoneymoney.h"

class MyMoneySecurity;

/**
  * This class represents a widget to enter monetary values.
  * It has an edit field and a button to select a popup
  * calculator. The result of the calculator (if used) is
  * stored in the edit field.
  *
  * @author Thomas Baumgart
  */
class AmountEditPrivate;
class KMM_BASE_WIDGETS_EXPORT AmountEdit : public QLineEdit
{
    Q_OBJECT
    Q_DISABLE_COPY(AmountEdit)

    Q_PROPERTY(bool calculatorButtonVisibility READ isCalculatorButtonVisible WRITE setCalculatorButtonVisible)
    Q_PROPERTY(bool allowEmpty READ isEmptyAllowed WRITE setAllowEmpty)
    Q_PROPERTY(bool readOnly READ isReadOnly WRITE setReadOnly)
    Q_PROPERTY(MyMoneyMoney value READ value WRITE setValue DESIGNABLE false STORED false USER true)
    Q_PROPERTY(bool valid READ isValid DESIGNABLE false STORED false)

protected Q_SLOTS:
    void theTextChanged(const QString & text);
    void slotCalculatorResult();
    void slotCalculatorOpen();
    void slotCalculatorClose();

public:
    explicit AmountEdit(QWidget* parent = nullptr, const int prec = -2);
    explicit AmountEdit(const MyMoneySecurity& eq, QWidget* parent = nullptr);
    virtual ~AmountEdit();


    MyMoneyMoney value() const;

    void setValue(const MyMoneyMoney& value);

    bool isValid() const;

    /**
      * This method returns the value of the edit field in "numerator/denominator" format.
      * If you want to get the text of the edit field, use lineedit()->text() instead.
      */
    QString numericalText() const;

    /**
      * Set the number of fractional digits that should be shown
      *
      * @param prec number of fractional digits.
      *
      * @note should be used prior to calling setText()
      * @sa precision
      */
    void setPrecision(const int prec);

    /**
      * return the number of fractional digits
      * @sa setPrecision
      */
    int precision() const;

    /**
      * This method allows to modify the behavior of the widget
      * such that it accepts an empty value (all blank) or not.
      * The default is to not accept an empty input and to
      * convert an empty field into 0.00 upon loss of focus.
      *
      * @param allowed if @a true, empty input is allowed, if @a false
      *                empty input will be converted to 0.00
      */
    void setAllowEmpty(bool allowed = true);

    bool isEmptyAllowed() const;

    bool isCalculatorButtonVisible() const;

    void showCurrencySymbol(const QString& symbol);

    /**
     * This allows to setup the standard precision (number of decimal places)
     * to be used when no other information is available. @a prec must be in
     * the range of 0..19. If never set, the default precision is 2.
     *
     * @sa standardPrecision
     */
    void setStandardPrecision(int prec);

    /**
     * This returns the global selected standard precision
     *
     * @sa setStandardPrecision
     */
    int standardPrecision();


public Q_SLOTS:
    void setText(const QString& txt);

    /**
      * This method allows to show/hide the calculator button of the widget.
      * The parameter @p show controls the behavior. Default is to show the
      * button.
      *
      * @param show if true, button is shown, if false it is hidden
      */
    void setCalculatorButtonVisible(const bool show);

    /**
     * overridden for internal reasons (keep state of calculator button)
     */
    void setReadOnly(bool ro);

Q_SIGNALS:
    /**
      * This signal is sent, when the focus leaves this widget and
      * the amount has been changed by user during this focus possession.
      */
    void valueChanged(const QString& text);

    /**
     * This signal is emitted when the contents of the widget
     * changed and was validated. Use this in favor of textChanged()
     * in your application.
     */
    void validatedTextChanged(const QString& text);

protected:
    /**
      * This method ensures that the text version contains a
      * fractional part.
      */
    void ensureFractionalPart();

    /**
     * return a pointer to the global object for the settings
     */
    AmountEdit* global();

    /**
     * Overridden to support calculator button.
     */
    virtual void resizeEvent(QResizeEvent* event) override;

    /**
     * Overridden to support ensureFractionalPart().
     */
    virtual void focusOutEvent(QFocusEvent* event) override;

    /**
     * Overridden to support calculator button.
     */
    virtual void keyPressEvent(QKeyEvent* event) override;

private:
    AmountEditPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(AmountEdit)
};

#endif
