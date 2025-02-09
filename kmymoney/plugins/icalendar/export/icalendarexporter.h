/*
    SPDX-FileCopyrightText: 2009 Cristian Onet onet.cristian @gmail.com
    SPDX-FileCopyrightText: 2021 Dawid Wróbel <me@dawidwrobel.com>
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#ifndef ICALENDAREXPORTER_H
#define ICALENDAREXPORTER_H

#include <memory>

#include "kmymoneyplugin.h"
#include "mymoneyaccount.h"
#include "mymoneykeyvaluecontainer.h"

class QStringList;
class KPluginInfo;

class iCalendarExporter: public KMyMoneyPlugin::Plugin
{
    Q_OBJECT

public:
#if KCOREADDONS_VERSION < QT_VERSION_CHECK(5, 77, 0)
    explicit iCalendarExporter(QObject *parent, const QVariantList &args);
#else
    explicit iCalendarExporter(QObject *parent, const KPluginMetaData &metaData, const QVariantList &args);
#endif
    ~iCalendarExporter() override;

protected Q_SLOTS:
    // this is the export function called when the user selects the interface menu
    void slotFirstExport();

    // this is the export method called automatically
    void slotExport();

    // the plugin loader plugs in a plugin
    void plug(KXMLGUIFactory* guiFactory) override;

    // the plugin loader unplugs a plugin
    void unplug() override;

    // the plugin's configurations has changed
    void updateConfiguration() override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

#endif // ICALENDAREXPORT_H

