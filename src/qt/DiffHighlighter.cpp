// SPDX-License-Identifier: GPL-2.0-or-later
#include "DiffHighlighter.h"
#include <QColor>
#include <QTextCharFormat>

void DiffHighlighter::highlightBlock(const QString& text)
{
	QTextCharFormat format;
	if (text.startsWith("+++") || text.startsWith("---") || text.startsWith("diff "))
	{
		format.setFontWeight(QFont::Bold);
		format.setForeground(QColor("#7898b9"));
	}
	else if (text.startsWith('+'))
		format.setForeground(QColor("#38a169"));
	else if (text.startsWith('-'))
		format.setForeground(QColor("#db6363"));
	else if (text.startsWith("@@"))
		format.setForeground(QColor("#a88cdb"));
	else
		return;
	setFormat(0, text.size(), format);
}
