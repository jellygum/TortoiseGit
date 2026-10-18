// SPDX-License-Identifier: GPL-2.0-or-later
#include "OperationDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

OperationDialog::OperationDialog(const QString& title, const QString& description, QWidget* parent) : QDialog(parent)
{
	setWindowTitle(title);
	setMinimumWidth(480);
	auto* layout = new QVBoxLayout(this);
	auto* explanation = new QLabel(description, this);
	explanation->setWordWrap(true);
	explanation->setTextFormat(Qt::PlainText);
	layout->addWidget(explanation);
	m_form = new QFormLayout;
	layout->addLayout(m_form);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

void OperationDialog::addText(const QString& key, const QString& label, const QString& value)
{
	auto* edit = new QLineEdit(value, this);
	m_text.insert(key, edit);
	m_form->addRow(label, edit);
}

void OperationDialog::addChoice(const QString& key, const QString& label, const QStringList& values)
{
	auto* choice = new QComboBox(this);
	choice->addItems(values);
	m_choices.insert(key, choice);
	m_form->addRow(label, choice);
}

QString OperationDialog::value(const QString& key) const
{
	if (m_text.contains(key)) return m_text.value(key)->text();
	if (m_choices.contains(key)) return m_choices.value(key)->currentText();
	return {};
}
