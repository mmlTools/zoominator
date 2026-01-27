#pragma once
#include <QWidget>

// OBS frontend API (obs_frontend_event / obs_frontend_event_cb)
extern "C" {
#include <obs-frontend-api.h>
}

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
	static void onFrontendEvent(obs_frontend_event event, void *private_data);

	QComboBox *cmbSource = nullptr;
	QPushButton *btnRefresh = nullptr;

	bool loading = false;
};
