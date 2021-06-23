/*
 * Copyright (c) Contributors to the Open 3D Engine Project
 * 
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <FormFolderBrowseEditWidget.h>
#include <AzQtComponents/Components/StyledLineEdit.h>

#include <QFileDialog>
#include <QLineEdit>
#include <QStandardPaths>

namespace O3DE::ProjectManager
{
    FormFolderBrowseEditWidget::FormFolderBrowseEditWidget(const QString& labelText, const QString& valueText, QWidget* parent)
        : FormBrowseEditWidget(labelText, valueText, parent)
    {
    }

    void FormFolderBrowseEditWidget::HandleBrowseButton()
    {
        QString defaultPath = m_lineEdit->text();
        if (defaultPath.isEmpty())
        {
            defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        }

        QString directory = QDir::toNativeSeparators(QFileDialog::getExistingDirectory(this, tr("Browse"), defaultPath));
        if (!directory.isEmpty())
        {
            m_lineEdit->setText(directory);
        }

    }
} // namespace O3DE::ProjectManager