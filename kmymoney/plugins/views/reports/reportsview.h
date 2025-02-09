/*
    SPDX-FileCopyrightText: 2018 Łukasz Wojniłowicz <lukasz.wojnilowicz@gmail.com>
    SPDX-FileCopyrightText: 2021 Dawid Wróbel <me@dawidwrobel.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef REPORTSVIEW_H
#define REPORTSVIEW_H

// ----------------------------------------------------------------------------
// KDE Includes

// ----------------------------------------------------------------------------
// QT Includes

// Project Includes

#include "kmymoneyplugin.h"

class KReportsView;

class ReportsView : public KMyMoneyPlugin::Plugin, public KMyMoneyPlugin::DataPlugin
{
    Q_OBJECT
    Q_INTERFACES(KMyMoneyPlugin::DataPlugin)

public:
#if KCOREADDONS_VERSION < QT_VERSION_CHECK(5, 77, 0)
    explicit ReportsView(QObject *parent, const QVariantList &args);
#else
    explicit ReportsView(QObject *parent, const KPluginMetaData &metaData, const QVariantList &args);
#endif
    ~ReportsView() final;

    void plug(KXMLGUIFactory* guiFactory) final override;
    void unplug() final override;

    QVariant requestData(const QString &arg, uint type) final override;

private:
    QWidget *netWorthForecast() const;
    /**
     * @brief netWorthForecast
     * @param arg consists of following arguments:
     * 1) detail level of a chart  (eMyMoney::Report::DetailLevel)
     * 2) forecast days of a chart
     * 3) width of a chart
     * 3) height a chart
     * correctly written arg variable looks as follows:
     * "2;5;800;600"
     * @return self-handling widget with a chart
     */
    QWidget *netWorthForecast(const QString &arg) const;
    QString budget() const;
    QString showColoredAmount(const QString &amount, bool isNegative) const;
    QString link(const QString& view, const QString& query, const QString& _title) const;
    QString linkend() const;
    KReportsView* m_view;
};

#endif
