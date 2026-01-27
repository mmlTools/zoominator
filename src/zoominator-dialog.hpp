#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QSpinBox;

class ZoominatorDialog final : public QDialog {
	Q_OBJECT

public:
	explicit ZoominatorDialog(QWidget *parent = nullptr);

protected:
	void closeEvent(QCloseEvent *event) override;

private slots:
	void refreshLists();
	void applyToController();
	void testZoom();
	void clearHotkey();

private:
	void buildUi();
	void loadFromController();
	void populateSources();

	QComboBox *cmbSource = nullptr;
	QComboBox *cmbMode = nullptr;

	QComboBox *cmbTrigger = nullptr;
	QComboBox *cmbMouseBtn = nullptr;
	QCheckBox *chkCtrl = nullptr;
	QCheckBox *chkAlt = nullptr;
	QCheckBox *chkShift = nullptr;
	QCheckBox *chkWin = nullptr;

	QKeySequenceEdit *editHotkey = nullptr;
	QPushButton *btnClearHotkey = nullptr;

	QDoubleSpinBox *spZoom = nullptr;
	QSpinBox *spIn = nullptr;
	QSpinBox *spOut = nullptr;
	QCheckBox *chkFollow = nullptr;
	QDoubleSpinBox *spFollowSpeed = nullptr;
	QCheckBox *chkPortraitCover = nullptr;
	QCheckBox *chkDebug = nullptr;

	QLabel *lblStatus = nullptr;
	QPushButton *btnRefresh = nullptr;
	QPushButton *btnApply = nullptr;
	QPushButton *btnTest = nullptr;

	bool loading = false;
};
