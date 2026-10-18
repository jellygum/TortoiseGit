// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "GitClient.h"
#include "RepositoryStatus.h"
#include <QMainWindow>
#include <QFileSystemWatcher>
#include <QTimer>

class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSortFilterProxyModel;
class QStackedWidget;
class QTableView;
class QTableWidget;
class QTabWidget;
class StatusModel;

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	explicit MainWindow(QWidget* parent = nullptr);
	void openRepository(const QString& path);
protected:
	void closeEvent(QCloseEvent* event) override;
private:
	void buildUi();
	void buildMenus();
	void refresh();
	void loadHistory();
	void loadReferences();
	void loadStashes();
	void showDiff();
	void stage(bool all);
	void unstage();
	void discard();
	void commit();
	void cloneRepository();
	void initRepository();
	void push();
	void branchOrTag(bool tag);
	void checkout();
	void operation(const QString& title, const QStringList& prefix, const QString& field,
		const QString& description, const QStringList& suffix = {});
	void runMutation(const QString& title, const QStringList& arguments, const QByteArray& input = {});
	void showResult(const QString& title, const QStringList& arguments);
	void showPatch(const QString& hash);
	void showBlame();
	void savePatch();
	void applyPatch();
	void configureGit();
	void configureRemote();
	void editConflict();
	void openImageDiff();
	void updateControls();
	void updateWatchers();
	void reportError(const QString& title, const tgit::GitResult& result);
	bool confirm(const QString& title, const QString& details);
	QStringList selectedPaths(bool includeOriginal = true) const;
	std::vector<tgit::StatusEntry> selectedEntries() const;
	QAction* addRepositoryAction(class QMenu* menu, const QString& text, const std::function<void()>& action);
	tgit::GitClient m_git;
	QString m_root;
	StatusModel* m_model = nullptr;
	QSortFilterProxyModel* m_proxy = nullptr;
	QTableView* m_files = nullptr;
	QTableWidget* m_history = nullptr;
	QTableWidget* m_refs = nullptr;
	QTableWidget* m_stashes = nullptr;
	QPlainTextEdit* m_diff = nullptr;
	QPlainTextEdit* m_commitMessage = nullptr;
	QPlainTextEdit* m_output = nullptr;
	QPlainTextEdit* m_commitDetails = nullptr;
	QTabWidget* m_diffTabs = nullptr;
	QStackedWidget* m_pages = nullptr;
	QListWidget* m_navigation = nullptr;
	QLabel* m_repositoryLabel = nullptr;
	QLabel* m_branchLabel = nullptr;
	QLabel* m_summary = nullptr;
	QPushButton* m_commitButton = nullptr;
	QCheckBox* m_amend = nullptr;
	QAction* m_cancelAction = nullptr;
	QAction* m_openAction = nullptr;
	QAction* m_cloneAction = nullptr;
	QAction* m_initAction = nullptr;
	QList<QAction*> m_repositoryActions;
	QList<QPushButton*> m_mutationButtons;
	QFileSystemWatcher m_watcher;
	QTimer m_refreshTimer;
	QTimer m_pollTimer;
	quint64 m_generation = 0;
	quint64 m_diffGeneration = 0;
	bool m_refreshPending = false;
};
