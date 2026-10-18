// SPDX-License-Identifier: GPL-2.0-or-later
#include "MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	app.setOrganizationName("TortoiseGit");
	app.setApplicationName("TortoiseGitQt");
	app.setApplicationVersion("0.1.0");
	QCommandLineParser parser;
	parser.setApplicationDescription("TortoiseGit Qt native desktop preview");
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addOption({{"r", "repository"}, "Open a Git working tree", "path"});
	parser.addOption({"no-settings", "Do not read or write saved settings"});
	parser.addOption({"smoke-test", "Save a window screenshot after repository load, then exit", "png"});
	parser.addPositionalArgument("path", "Git working tree to open", "[path]");
	parser.process(app);
	app.setProperty("noSettings", parser.isSet("no-settings") || parser.isSet("smoke-test"));
	MainWindow window;
	window.show();
	QString path = parser.value("repository");
	if (path.isEmpty()) path = parser.positionalArguments().value(0);
	if (path.isEmpty() && !app.property("noSettings").toBool()) path = QSettings().value("lastRepository").toString();
	if (!path.isEmpty() && QFileInfo::exists(path)) QTimer::singleShot(0, &window, [&window, path] { window.openRepository(path); });
	if (parser.isSet("smoke-test"))
	{
		auto* timer = new QTimer(&app);
		timer->setInterval(300);
		QObject::connect(timer, &QTimer::timeout, &app, [&app, &window, &parser]
		{
			if (window.property("repositoryReady").toBool() && !window.property("gitBusy").toBool())
				app.exit(window.grab().save(parser.value("smoke-test")) ? 0 : 2);
		});
		timer->start();
		QTimer::singleShot(30000, &app, [&app] { app.exit(3); });
	}
	return app.exec();
}
