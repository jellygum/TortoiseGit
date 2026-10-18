// SPDX-License-Identifier: GPL-2.0-or-later
#include "MainWindow.h"
#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class MainWindowTest : public QObject
{
	Q_OBJECT
private:
	QByteArray git(const QString& root, const QStringList& arguments)
	{
		QProcess process;
		process.setWorkingDirectory(root);
		process.start("/usr/bin/git", arguments);
		if (!process.waitForFinished(10000) || process.exitCode() != 0)
			qFatal("Fixture Git failed: %s", process.readAllStandardError().constData());
		return process.readAllStandardOutput();
	}
	void write(const QString& path, const QByteArray& contents)
	{
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) qFatal("Cannot write UI fixture");
	}
	void initialize(const QString& root)
	{
		git(root, {"init", "-b", "main"});
		git(root, {"config", "user.name", "Qt UI Test"});
		git(root, {"config", "user.email", "qt-ui@example.invalid"});
		git(root, {"config", "commit.gpgsign", "false"});
	}
	void waitIdle(MainWindow& window)
	{
		QTRY_VERIFY_WITH_TIMEOUT(window.property("repositoryReady").toBool(), 10000);
		QTRY_VERIFY_WITH_TIMEOUT(!window.property("gitBusy").toBool(), 10000);
	}
	void select(QTableView* view, const QString& path)
	{
		for (int row = 0; row < view->model()->rowCount(); ++row)
			if (view->model()->index(row, 1).data().toString() == path) { view->selectRow(row); return; }
		QFAIL("Expected path missing from Changes");
	}
private slots:
	void initTestCase() { qApp->setProperty("noSettings", true); }
	void stageAndCommitIndexOnly()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto root = directory.path();
		initialize(root);
		write(root + "/tracked.txt", "initial\n");
		git(root, {"add", "."}); git(root, {"commit", "-m", "seed"});
		write(root + "/tracked.txt", "version two\n");
		write(root + "/leave-untracked.txt", "untouched\n");
		MainWindow window; window.show(); window.openRepository(root); waitIdle(window);
		auto* files = window.findChild<QTableView*>("changedFiles");
		QVERIFY(files);
		QCOMPARE(files->model()->rowCount(), 2);
		select(files, "tracked.txt"); waitIdle(window);
		auto* stage = window.findChild<QPushButton*>("stageSelected");
		QVERIFY(stage->isEnabled()); QTest::mouseClick(stage, Qt::LeftButton); waitIdle(window);
		QCOMPARE(git(root, {"diff", "--cached", "--name-only"}), QByteArray("tracked.txt\n"));
		write(root + "/tracked.txt", "version three\n");
		auto* message = window.findChild<QPlainTextEdit*>("commitMessage");
		message->setPlainText("commit from Qt UI");
		auto* commit = window.findChild<QPushButton*>("commitStaged");
		QTRY_VERIFY(commit->isEnabled()); QTest::mouseClick(commit, Qt::LeftButton); waitIdle(window);
		QCOMPARE(git(root, {"show", "HEAD:tracked.txt"}), QByteArray("version two\n"));
		QCOMPARE(git(root, {"log", "-1", "--format=%s"}), QByteArray("commit from Qt UI\n"));
		QVERIFY(git(root, {"status", "--porcelain"}).contains("leave-untracked.txt"));
		QVERIFY(git(root, {"diff"}).contains("+version three"));
		QVERIFY(message->toPlainText().isEmpty());
	}
	void unstageUnbornPreservesLiteralFile()
	{
		QTemporaryDir directory; const auto root = directory.path(); initialize(root);
		const QString path = "한글 [literal]*.txt";
		write(root + '/' + path, "retained\n");
		MainWindow window; window.show(); window.openRepository(root); waitIdle(window);
		auto* files = window.findChild<QTableView*>("changedFiles");
		select(files, path); waitIdle(window);
		QTest::mouseClick(window.findChild<QPushButton*>("stageSelected"), Qt::LeftButton); waitIdle(window);
		QCOMPARE(git(root, {"diff", "--cached", "--name-only", "-z"}), path.toUtf8() + '\0');
		select(files, path); waitIdle(window);
		write(root + '/' + path, "newer unstaged content\n");
		QTest::mouseClick(window.findChild<QPushButton*>("unstageSelected"), Qt::LeftButton); waitIdle(window);
		QVERIFY(git(root, {"diff", "--cached", "--name-only"}).isEmpty());
		QFile file(root + '/' + path); QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), QByteArray("newer unstaged content\n"));
	}
	void restageAndUnstageRename()
	{
		QTemporaryDir directory; const auto root = directory.path(); initialize(root);
		write(root + "/old.txt", "original\n");
		git(root, {"add", "."}); git(root, {"commit", "-m", "seed"});
		git(root, {"mv", "old.txt", "new.txt"});
		write(root + "/new.txt", "updated after rename\n");
		MainWindow window; window.show(); window.openRepository(root); waitIdle(window);
		auto* files = window.findChild<QTableView*>("changedFiles");
		QCOMPARE(files->model()->rowCount(), 1); files->selectRow(0); waitIdle(window);
		QTest::mouseClick(window.findChild<QPushButton*>("stageSelected"), Qt::LeftButton); waitIdle(window);
		QCOMPARE(git(root, {"show", ":new.txt"}), QByteArray("updated after rename\n"));
		// The staged rename may become an add/delete pair when similarity drops.
		files->selectAll(); waitIdle(window);
		QTest::mouseClick(window.findChild<QPushButton*>("unstageSelected"), Qt::LeftButton); waitIdle(window);
		QVERIFY(git(root, {"diff", "--cached", "--name-only"}).isEmpty());
		QVERIFY(QFile::exists(root + "/new.txt"));
	}
	void amendMessageWithoutStagedChanges()
	{
		QTemporaryDir directory; const auto root = directory.path(); initialize(root);
		write(root + "/file", "content\n");
		git(root, {"add", "."}); git(root, {"commit", "-m", "old message"});
		MainWindow window; window.show(); window.openRepository(root); waitIdle(window);
		window.findChild<QPlainTextEdit*>("commitMessage")->setPlainText("amended from Qt");
		window.findChild<QCheckBox*>()->setChecked(true);
		auto* commit = window.findChild<QPushButton*>("commitStaged");
		QVERIFY(commit->isEnabled());
		QTimer::singleShot(50, &window, []
		{
			if (auto* confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) confirmation->done(QMessageBox::Ok);
		});
		QTest::mouseClick(commit, Qt::LeftButton); waitIdle(window);
		QCOMPARE(git(root, {"log", "-1", "--format=%s"}), QByteArray("amended from Qt\n"));
	}
};

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
