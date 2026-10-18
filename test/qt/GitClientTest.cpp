// SPDX-License-Identifier: GPL-2.0-or-later
#include "GitClient.h"
#include "RepositoryStatus.h"
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class GitClientTest : public QObject
{
	Q_OBJECT
private:
	QByteArray git(const QString& root, const QStringList& args, int expected = 0)
	{
		QProcess process;
		process.setWorkingDirectory(root);
		process.start("/usr/bin/git", args);
		if (!process.waitForFinished(15000) || process.exitCode() != expected)
			qFatal("Fixture Git failed: %s", process.readAllStandardError().constData());
		return process.readAllStandardOutput();
	}
	void init(const QString& root)
	{
		git(root, {"init", "-b", "main"});
		git(root, {"config", "user.name", "Qt Test"});
		git(root, {"config", "user.email", "qt-test@example.invalid"});
		git(root, {"config", "commit.gpgsign", "false"});
	}
	void write(const QString& path, const QByteArray& contents)
	{
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) qFatal("Cannot write fixture");
	}
private slots:
	void literalPathsAndIndex()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto root = directory.path();
		init(root);
		const QString name = "한글 space\n[1]*.txt";
		write(root + '/' + name, "first\n");
		write(root + "/another.txt", "leave untracked\n");
		tgit::GitClient client;
		int calls = 0;
		tgit::GitResult result;
		client.execute(root, {"status", "--porcelain=v2", "--branch", "-z"}, [&](const auto& r) { result = r; ++calls; });
		QTRY_COMPARE(calls, 1);
		QVERIFY(result.ok());
		const auto initial = tgit::parseStatus({result.output.constData(), static_cast<std::size_t>(result.output.size())});
		QVERIFY(static_cast<bool>(initial));
		QVERIFY(initial.status.unborn);
		QCOMPARE(initial.status.entries.size(), std::size_t(2));
		client.execute(root, {"add", "--", name}, [&](const auto& r) { result = r; ++calls; });
		QTRY_COMPARE(calls, 2);
		QVERIFY(result.ok());
		QCOMPARE(git(root, {"diff", "--cached", "--name-only", "-z"}), name.toUtf8() + '\0');
		client.execute(root, {"commit", "--file=-"}, [&](const auto& r) { result = r; ++calls; }, "한글 commit\n\nbody\n");
		QTRY_COMPARE(calls, 3);
		QVERIFY2(result.ok(), qPrintable(result.message()));
		QCOMPARE(git(root, {"log", "-1", "--format=%s"}), QByteArray("한글 commit\n"));
		QVERIFY(QFile::exists(root + "/another.txt"));
		write(root + '/' + name, "first\nsecond\n");
		client.execute(root, {"diff", "--no-ext-diff", "--", name}, [&](const auto& r) { result = r; ++calls; });
		QTRY_COMPARE(calls, 4);
		QVERIFY(result.ok());
		QVERIFY(result.output.contains("+second"));
		// Leading dashes and pathspec magic must select only the literal name.
		write(root + "/--all", "literal dash\n");
		write(root + "/:(glob)*", "literal magic\n");
		client.execute(root, {"add", "--", "--all", ":(glob)*"}, [&](const auto& r) { result = r; ++calls; });
		QTRY_COMPARE(calls, 5);
		QVERIFY(result.ok());
		const auto staged = git(root, {"diff", "--cached", "--name-only", "-z"});
		QVERIFY(staged.contains("--all\0"));
		QVERIFY(staged.contains(":(glob)*\0"));
		QVERIFY(!staged.contains("another.txt"));
	}

	void queueFailureAndRecovery()
	{
		QTemporaryDir directory; init(directory.path());
		tgit::GitClient client;
		QList<int> order;
		client.execute(directory.path(), {"not-a-real-git-command"}, [&](const auto& result) { QVERIFY(!result.ok()); order << 1; });
		client.execute(directory.path(), {"status", "--porcelain=v2", "-z"}, [&](const auto& result) { QVERIFY(result.ok()); order << 2; });
		QTRY_COMPARE(order.size(), 2);
		QCOMPARE(order, QList<int>({1, 2}));
		QTRY_VERIFY(!client.busy());
		client.setExecutable("/does/not/exist/git");
		bool finished = false;
		client.execute(directory.path(), {"status"}, [&](const auto& result) { QVERIFY(!result.ok()); QVERIFY(!result.error.isEmpty()); finished = true; });
		QTRY_VERIFY(finished);
	}

	void remoteRoundTripAndRejection()
	{
		QTemporaryDir directory;
		const auto root = directory.path();
		init(root);
		write(root + "/file.txt", "content\n");
		git(root, {"add", "."}); git(root, {"commit", "-m", "initial"});
		QTemporaryDir remote;
		git(remote.path(), {"init", "--bare"});
		git(root, {"remote", "add", "origin", remote.path()});
		tgit::GitClient client;
		int calls = 0;
		tgit::GitResult result;
		client.execute(root, {"push", "--porcelain", "--set-upstream", "--", "origin", "HEAD:refs/heads/main"}, [&](const auto& r) { result = r; ++calls; });
		QTRY_COMPARE(calls, 1);
		QVERIFY2(result.ok(), qPrintable(result.message()));
		QCOMPARE(git(root, {"rev-parse", "HEAD"}), git(remote.path(), {"rev-parse", "refs/heads/main"}));
		write(remote.path() + "/hooks/pre-receive", "#!/bin/sh\necho test-rejection >&2\nexit 1\n");
		QFile::setPermissions(remote.path() + "/hooks/pre-receive", QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
		git(root, {"commit", "--allow-empty", "-m", "rejected"});
		client.execute(root, {"push", "origin", "main"}, [&](const auto& r) { result = r; ++calls; });
		QTRY_COMPARE(calls, 2);
		QVERIFY(!result.ok());
		QVERIFY(result.message().contains("test-rejection"));
		QVERIFY(git(root, {"rev-parse", "HEAD"}) != git(remote.path(), {"rev-parse", "refs/heads/main"}));
	}

	void hookFailureKeepsIndex()
	{
		QTemporaryDir directory; const auto root = directory.path(); init(root);
		write(root + "/file", "content"); git(root, {"add", "."});
		write(root + "/.git/hooks/pre-commit", "#!/bin/sh\necho hook-rejected >&2\nexit 1\n");
		QFile::setPermissions(root + "/.git/hooks/pre-commit", QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
		tgit::GitClient client;
		bool finished = false;
		client.execute(root, {"commit", "--file=-"}, [&](const auto& result)
		{
			QVERIFY(!result.ok());
			QVERIFY(result.message().contains("hook-rejected"));
			finished = true;
		}, "test\n");
		QTRY_VERIFY(finished);
		QCOMPARE(git(root, {"diff", "--cached", "--name-only"}), QByteArray("file\n"));
	}

	void cancelAndTimeout()
	{
		QTemporaryDir directory; const auto root = directory.path(); init(root);
		git(root, {"config", "alias.wait-test", "!sleep 10"});
		tgit::GitClient client;
		bool cancelled = false, queuedCancelled = false;
		client.execute(root, {"wait-test"}, [&](const auto& result) { cancelled = result.cancelled; });
		client.execute(root, {"status"}, [&](const auto& result) { queuedCancelled = result.cancelled; });
		QTimer::singleShot(100, &client, &tgit::GitClient::cancel);
		QTRY_VERIFY_WITH_TIMEOUT(cancelled && queuedCancelled, 4000);
		QTRY_VERIFY(!client.busy());
		bool timedOut = false;
		client.execute(root, {"wait-test"}, [&](const auto& result) { timedOut = !result.ok() && result.message().contains("timed out"); }, {}, 100);
		QTRY_VERIFY_WITH_TIMEOUT(timedOut, 4000);
	}
	void inheritedRepositoryEnvironmentIsIgnored()
	{
		QTemporaryDir chosen, other;
		init(chosen.path()); init(other.path());
		write(chosen.path() + "/chosen", "chosen\n");
		write(other.path() + "/other", "other\n");
		const auto previous = qgetenv("GIT_DIR");
		qputenv("GIT_DIR", (other.path() + "/.git").toUtf8());
		tgit::GitClient client;
		bool finished = false;
		QByteArray output;
		client.execute(chosen.path(), {"status", "--porcelain=v2", "-z"}, [&](const auto& result) { output = result.output; finished = true; });
		if (previous.isNull()) qunsetenv("GIT_DIR"); else qputenv("GIT_DIR", previous);
		QTRY_VERIFY(finished);
		QVERIFY(output.contains("chosen"));
		QVERIFY(!output.contains("other"));
	}
	void cancelStopsHookDescendants()
	{
		QTemporaryDir directory; const auto root = directory.path(); init(root);
		git(root, {"config", "alias.child-test", "!(trap '' TERM; sleep 1; echo survived > cancelled-child) & wait"});
		tgit::GitClient client;
		bool finished = false;
		client.execute(root, {"child-test"}, [&](const auto& result) { finished = result.cancelled; });
		QTimer::singleShot(200, &client, &tgit::GitClient::cancel);
		QTRY_VERIFY_WITH_TIMEOUT(finished, 4000);
		QTest::qWait(1300);
		QVERIFY(!QFile::exists(root + "/cancelled-child"));
	}
};

QTEST_GUILESS_MAIN(GitClientTest)
#include "GitClientTest.moc"
