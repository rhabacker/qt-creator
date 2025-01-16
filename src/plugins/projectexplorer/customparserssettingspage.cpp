// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "customparserssettingspage.h"

#include "customparser.h"
#include "customparserconfigdialog.h"
#include "projectexplorer.h"
#include "projectexplorerconstants.h"
#include "projectexplorertr.h"

#include <utils/algorithm.h>
#include <utils/qtcassert.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <qheaderview.h>

namespace ProjectExplorer {
namespace Internal {

class CustomParsersSettingsWidget final : public Core::IOptionsPageWidget
{
public:
    CustomParsersSettingsWidget()
    {
        m_customParsers = ProjectExplorerPlugin::customParsers();
        resetListView();

        const auto mainLayout = new QVBoxLayout(this);
        const auto widgetLayout = new QHBoxLayout;
        mainLayout->addLayout(widgetLayout);
        const auto hintLabel = new QLabel(Tr::tr(
            "Custom output parsers defined here can be enabled individually "
            "in the project's build or run settings."));
        mainLayout->addWidget(hintLabel);
        widgetLayout->addWidget(&m_parserListView);
        const auto buttonLayout = new QVBoxLayout;
        widgetLayout->addLayout(buttonLayout);
        const auto addButton = new QPushButton(Tr::tr("Add..."));
        const auto removeButton = new QPushButton(Tr::tr("Remove"));
        const auto editButton = new QPushButton("Edit...");
        buttonLayout->addWidget(addButton);
        buttonLayout->addWidget(removeButton);
        buttonLayout->addWidget(editButton);
        buttonLayout->addStretch(1);

        connect(addButton, &QPushButton::clicked, this, [this] {
            CustomParserConfigDialog dlg(this);
            dlg.setSettings(CustomParserSettings());
            if (dlg.exec() != QDialog::Accepted)
                return;
            CustomParserSettings newParser = dlg.settings();
            newParser.id = Utils::Id::generate();
            newParser.displayName = Tr::tr("New Parser");
            m_customParsers << newParser;
            resetListView();
        });
        connect(removeButton, &QPushButton::clicked, this, [this] {
            const QList<QTableWidgetItem *> sel = m_parserListView.selectedItems();
            QTC_ASSERT(sel.size() == 1, return);
            m_customParsers.removeAt(m_parserListView.row(sel.first()));
            delete sel.first();
        });
        connect(editButton, &QPushButton::clicked, this, [this] {
            const QList<QTableWidgetItem *> sel = m_parserListView.selectedItems();
            QTC_ASSERT(sel.size() == 1, return);
            CustomParserSettings &s = m_customParsers[m_parserListView.row(sel.first())];
            CustomParserConfigDialog dlg(this);
            dlg.setSettings(s);
            if (dlg.exec() != QDialog::Accepted)
                return;
            s.error = dlg.settings().error;
            s.warning = dlg.settings().warning;
        });

        connect(&m_parserListView, &QTableWidget::cellClicked, this, [this](int row, int column) {
            if (column == 0)
                return;
            QTableWidgetItem *item = m_parserListView.item(row, column);
            if (item->checkState() == Qt::Checked) {
                item->setCheckState(Qt::Unchecked);
                m_customParsers[row].projectLevel = false;
            } else {
                item->setCheckState(Qt::Checked);
                m_customParsers[row].projectLevel = true;
            }
        });

        connect(&m_parserListView, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
            if (item->column() == 0)
                m_customParsers[m_parserListView.row(item)].displayName = item->text();
            else if (item->column() == 1)
                m_customParsers[m_parserListView.row(item)].projectLevel = item->checkState() == Qt::Checked;
        });

        const auto updateButtons = [this, removeButton, editButton] {
            const bool enable = !m_parserListView.selectedItems().isEmpty();
            removeButton->setEnabled(enable);
            editButton->setEnabled(enable);
        };
        updateButtons();
        connect(m_parserListView.selectionModel(), &QItemSelectionModel::selectionChanged,
                updateButtons);
    }

private:
    void apply() override { ProjectExplorerPlugin::setCustomParsers(m_customParsers); }

    void resetListView()
    {
        m_parserListView.clearContents();
        m_parserListView.setColumnCount(2);
        m_parserListView.setRowCount(m_customParsers.size());
        m_parserListView.setHorizontalHeaderLabels(QStringList() << Tr::tr("Name") << Tr::tr("Project level"));
        m_parserListView.verticalHeader()->hide();

        Utils::sort(m_customParsers,
                    [](const CustomParserSettings &s1, const CustomParserSettings &s2) {
            return s1.displayName < s2.displayName;
        });
        int row = 0;
        for (const CustomParserSettings &s : std::as_const(m_customParsers)) {
            const auto nameItem = new QTableWidgetItem(s.displayName);
            nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            m_parserListView.setItem(row, 0, nameItem);
            const auto projectLevelItem = new QTableWidgetItem();
            projectLevelItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
            projectLevelItem->setCheckState(s.projectLevel ? Qt::Checked : Qt::Unchecked);
            m_parserListView.setItem(row++, 1, projectLevelItem);
        }
    }

    QTableWidget m_parserListView;
    QList<CustomParserSettings> m_customParsers;
};

CustomParsersSettingsPage::CustomParsersSettingsPage()
{
    setId(Constants::CUSTOM_PARSERS_SETTINGS_PAGE_ID);
    setDisplayName(Tr::tr("Custom Output Parsers"));
    setCategory(Constants::BUILD_AND_RUN_SETTINGS_CATEGORY);
    setWidgetCreator([] { return new CustomParsersSettingsWidget; });
}

} // namespace Internal
} // namespace ProjectExplorer
