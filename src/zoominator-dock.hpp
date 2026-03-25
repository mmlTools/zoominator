#pragma once

#include <QWidget>

extern "C" {
#include <obs-frontend-api.h>
#include <obs.h>
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
	void queueRefresh();

	static void onFrontendEvent(obs_frontend_event event, void *private_data);
	static void onObsSignal(void *private_data, calldata_t *cd);

	QComboBox *cmbSource = nullptr;
	QPushButton *btnRefresh = nullptr;

	bool loading = false;
	signal_handler_t *obsSignals = nullptr;
};
