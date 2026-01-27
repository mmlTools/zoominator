#pragma once
#include <QWidget>

class QComboBox;
class QPushButton;
class QShowEvent;

class ZoominatorDock final : public QWidget {
	Q_OBJECT
public:
	explicit ZoominatorDock(QWidget *parent = nullptr);
	~ZoominatorDock() override;

protected:
	void showEvent(QShowEvent *event) override;

public slots:
	void refreshLists();
	void loadFromController();

private slots:
	void onSourceChanged(int idx);

private:
	void buildUi();
	void populateSources();
	static void onFrontendEvent(enum obs_frontend_event event, void *private_data);

	QComboBox *cmbSource = nullptr;
	QPushButton *btnRefresh = nullptr;

	bool loading = false;
};
