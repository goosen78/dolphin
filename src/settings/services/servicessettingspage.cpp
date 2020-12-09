/*
 * SPDX-FileCopyrightText: 2009-2010 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "servicessettingspage.h"

#include "dolphin_generalsettings.h"
#include "dolphin_versioncontrolsettings.h"
#include "settings/serviceitemdelegate.h"
#include "settings/servicemodel.h"

#include <KDesktopFile>
#include <KLocalizedString>
#include <KMessageBox>
#include <KNS3/Button>
#include <KPluginMetaData>
#include <KService>
#include <KServiceTypeTrader>
#include <KDesktopFileActions>

#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QScroller>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QLineEdit>

namespace
{
    const bool ShowDeleteDefault = false;
    const char VersionControlServicePrefix[] = "_version_control_";
    const char DeleteService[] = "_delete";
    const char CopyToMoveToService[] ="_copy_to_move_to";
}

ServicesSettingsPage::ServicesSettingsPage(QWidget* parent) :
    SettingsPageBase(parent),
    m_initialized(false),
    m_serviceModel(nullptr),
    m_sortModel(nullptr),
    m_listView(nullptr),
    m_enabledVcsPlugins()
{
    QVBoxLayout* topLayout = new QVBoxLayout(this);

    QLabel* label = new QLabel(i18nc("@label:textbox",
                                     "Select which services should "
                                     "be shown in the context menu:"), this);
    label->setWordWrap(true);
    m_searchLineEdit = new QLineEdit(this);
    m_searchLineEdit->setPlaceholderText(i18nc("@label:textbox", "Search..."));
    connect(m_searchLineEdit, &QLineEdit::textChanged, this, [this](const QString &filter){
        m_sortModel->setFilterFixedString(filter);
    });

    m_listView = new QListView(this);
    QScroller::grabGesture(m_listView->viewport(), QScroller::TouchGesture);

    auto *delegate = new ServiceItemDelegate(m_listView, m_listView);
    m_serviceModel = new ServiceModel(this);
    m_sortModel = new QSortFilterProxyModel(this);
    m_sortModel->setSourceModel(m_serviceModel);
    m_sortModel->setSortRole(Qt::DisplayRole);
    m_sortModel->setSortLocaleAware(true);
    m_sortModel->setFilterRole(Qt::DisplayRole);
    m_sortModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_listView->setModel(m_sortModel);
    m_listView->setItemDelegate(delegate);
    m_listView->setVerticalScrollMode(QListView::ScrollPerPixel);
    connect(m_listView, &QListView::clicked, this, &ServicesSettingsPage::changed);

#ifndef Q_OS_WIN
    auto *downloadButton = new KNS3::Button(i18nc("@action:button", "Download New Services..."),
                                                  QStringLiteral("servicemenu.knsrc"),
                                                  this);
    connect(downloadButton, &KNS3::Button::dialogFinished, this, [this](const KNS3::Entry::List &changedEntries) {
           if (!changedEntries.isEmpty()) {
               m_serviceModel->clear();
               loadServices();
           }
    });

#endif

    topLayout->addWidget(label);
    topLayout->addWidget(m_searchLineEdit);
    topLayout->addWidget(m_listView);
#ifndef Q_OS_WIN
    topLayout->addWidget(downloadButton);
#endif

    m_enabledVcsPlugins = VersionControlSettings::enabledPlugins();
    std::sort(m_enabledVcsPlugins.begin(), m_enabledVcsPlugins.end());
}

ServicesSettingsPage::~ServicesSettingsPage() = default;

void ServicesSettingsPage::applySettings()
{
    if (!m_initialized) {
        return;
    }

    KConfig config(QStringLiteral("kservicemenurc"), KConfig::NoGlobals);
    KConfigGroup showGroup = config.group("Show");

    QStringList enabledPlugins;

    const QAbstractItemModel *model = m_listView->model();
    for (int i = 0; i < model->rowCount(); ++i) {
        const QModelIndex index = model->index(i, 0);
        const QString service = model->data(index, ServiceModel::DesktopEntryNameRole).toString();
        const bool checked = model->data(index, Qt::CheckStateRole).toBool();

        if (service.startsWith(VersionControlServicePrefix)) {
            if (checked) {
                enabledPlugins.append(model->data(index, Qt::DisplayRole).toString());
            }
        } else if (service == QLatin1String(DeleteService)) {
            KSharedConfig::Ptr globalConfig = KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::NoGlobals);
            KConfigGroup configGroup(globalConfig, "KDE");
            configGroup.writeEntry("ShowDeleteCommand", checked);
            configGroup.sync();
        } else if (service == QLatin1String(CopyToMoveToService)) {
            GeneralSettings::setShowCopyMoveMenu(checked);
            GeneralSettings::self()->save();
        } else {
            showGroup.writeEntry(service, checked);
        }
    }

    showGroup.sync();

    if (m_enabledVcsPlugins != enabledPlugins) {
        VersionControlSettings::setEnabledPlugins(enabledPlugins);
        VersionControlSettings::self()->save();

        KMessageBox::information(window(),
                                 i18nc("@info", "Dolphin must be restarted to apply the "
                                                "updated version control systems settings."),
                                 QString(), // default title
                                 QStringLiteral("ShowVcsRestartInformation"));
    }
}

void ServicesSettingsPage::restoreDefaults()
{
    QAbstractItemModel* model = m_listView->model();
    for (int i = 0; i < model->rowCount(); ++i) {
        const QModelIndex index = model->index(i, 0);
        const QString service = model->data(index, ServiceModel::DesktopEntryNameRole).toString();

        const bool checked = !service.startsWith(VersionControlServicePrefix)
                             && service != QLatin1String(DeleteService)
                             && service != QLatin1String(CopyToMoveToService);
        model->setData(index, checked, Qt::CheckStateRole);
    }
}

void ServicesSettingsPage::showEvent(QShowEvent* event)
{
    if (!event->spontaneous() && !m_initialized) {
        loadServices();

        loadVersionControlSystems();

        // Add "Show 'Delete' command" as service
        KSharedConfig::Ptr globalConfig = KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::IncludeGlobals);
        KConfigGroup configGroup(globalConfig, "KDE");
        addRow(QStringLiteral("edit-delete"),
               i18nc("@option:check", "Delete"),
               DeleteService,
               configGroup.readEntry("ShowDeleteCommand", ShowDeleteDefault));

        // Add "Show 'Copy To' and 'Move To' commands" as service
        addRow(QStringLiteral("edit-copy"),
               i18nc("@option:check", "'Copy To' and 'Move To' commands"),
               CopyToMoveToService,
               GeneralSettings::showCopyMoveMenu());

        m_sortModel->sort(Qt::DisplayRole);

        m_initialized = true;
    }
    SettingsPageBase::showEvent(event);
}

void ServicesSettingsPage::loadServices()
{
    const KConfig config(QStringLiteral("kservicemenurc"), KConfig::NoGlobals);
    const KConfigGroup showGroup = config.group("Show");

    // Load generic services
    const KService::List entries = KServiceTypeTrader::self()->query(QStringLiteral("KonqPopupMenu/Plugin"));
    for (const KService::Ptr &service : entries) {
        const QString file = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "kservices5/" % service->entryPath());
        const QList<KServiceAction> serviceActions = KDesktopFileActions::userDefinedServices(file, true);

        const KDesktopFile desktopFile(file);
        const QString subMenuName = desktopFile.desktopGroup().readEntry("X-KDE-Submenu");

        for (const KServiceAction &action : serviceActions) {
            const QString serviceName = action.name();
            const bool addService = !action.noDisplay() && !action.isSeparator() && !isInServicesList(serviceName);

            if (addService) {
                const QString itemName = subMenuName.isEmpty()
                                         ? action.text()
                                         : i18nc("@item:inmenu", "%1: %2", subMenuName, action.text());
                const bool checked = showGroup.readEntry(serviceName, true);
                addRow(action.icon(), itemName, serviceName, checked);
            }
        }
    }

    // Load service plugins that implement the KFileItemActionPlugin interface
    const KService::List pluginServices = KServiceTypeTrader::self()->query(QStringLiteral("KFileItemAction/Plugin"));
    for (const KService::Ptr &service : pluginServices) {
        const QString desktopEntryName = service->desktopEntryName();
        if (!isInServicesList(desktopEntryName)) {
            const bool checked = showGroup.readEntry(desktopEntryName, true);
            addRow(service->icon(), service->name(), desktopEntryName, checked);
        }
    }

    // Load JSON-based plugins that implement the KFileItemActionPlugin interface
    const auto jsonPlugins = KPluginLoader::findPlugins(QStringLiteral("kf5/kfileitemaction"), [](const KPluginMetaData& metaData) {
        return metaData.serviceTypes().contains(QLatin1String("KFileItemAction/Plugin"));
    });

    for (const auto &jsonMetadata : jsonPlugins) {
        const QString desktopEntryName = jsonMetadata.pluginId();
        if (!isInServicesList(desktopEntryName)) {
            const bool checked = showGroup.readEntry(desktopEntryName, true);
            addRow(jsonMetadata.iconName(), jsonMetadata.name(), desktopEntryName, checked);
        }
    }

    m_sortModel->sort(Qt::DisplayRole);
    m_searchLineEdit->setFocus(Qt::OtherFocusReason);
}

void ServicesSettingsPage::loadVersionControlSystems()
{
    const QStringList enabledPlugins = VersionControlSettings::enabledPlugins();

    // Create a checkbox for each available version control plugin
    QStringList loadedPlugins;

    const QVector<KPluginMetaData> plugins = KPluginLoader::findPlugins(QStringLiteral("dolphin/vcs"));
    for(const auto &plugin: plugins) {
        const QString pluginName = plugin.name();
        addRow(QStringLiteral("code-class"),
               pluginName,
               VersionControlServicePrefix + pluginName,
               enabledPlugins.contains(pluginName));
        loadedPlugins += pluginName;
    }

    const KService::List pluginServices = KServiceTypeTrader::self()->query(QStringLiteral("FileViewVersionControlPlugin"));
    for (const auto &plugin : pluginServices) {
        const QString pluginName = plugin->name();
        if(loadedPlugins.contains(pluginName)) {
            continue;
        }
        addRow(QStringLiteral("code-class"),
               pluginName,
               VersionControlServicePrefix + pluginName,
               enabledPlugins.contains(pluginName));
    }

    m_sortModel->sort(Qt::DisplayRole);
}

bool ServicesSettingsPage::isInServicesList(const QString &service) const
{
    for (int i = 0; i < m_serviceModel->rowCount(); ++i) {
        const QModelIndex index = m_serviceModel->index(i, 0);
        if (m_serviceModel->data(index, ServiceModel::DesktopEntryNameRole).toString() == service) {
            return true;
        }
    }
    return false;
}

void ServicesSettingsPage::addRow(const QString &icon,
                                  const QString &text,
                                  const QString &value,
                                  bool checked)
{
    m_serviceModel->insertRow(0);

    const QModelIndex index = m_serviceModel->index(0, 0);
    m_serviceModel->setData(index, icon, Qt::DecorationRole);
    m_serviceModel->setData(index, text, Qt::DisplayRole);
    m_serviceModel->setData(index, value, ServiceModel::DesktopEntryNameRole);
    m_serviceModel->setData(index, checked, Qt::CheckStateRole);
}

