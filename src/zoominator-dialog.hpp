#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QKeySequenceEdit;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
struct calldata; 

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
	void clearFollowToggleHotkey();
	void chooseMarkerColor();
	void populateSourcesTab();

private:
	void buildUi();
	void loadFromController();
	void populateSources();
	void updateMarkerColorButton(const QColor &color);

	
	
	
	static void obsSourceChanged(void *data, struct calldata *cd);

	QTabWidget *tabWidget = nullptr;

	
	QComboBox        *cmbSource                 = nullptr;
	QComboBox        *cmbMode                   = nullptr;
	QKeySequenceEdit *editFollowToggleHotkey     = nullptr;
	QPushButton      *btnClearFollowToggleHotkey = nullptr;

	
	QComboBox        *cmbTrigger     = nullptr;
	QKeySequenceEdit *editHotkey     = nullptr;
	QPushButton      *btnClearHotkey = nullptr;
	QComboBox        *cmbMouseBtn    = nullptr;
	QCheckBox        *chkCtrl        = nullptr;
	QCheckBox        *chkAlt         = nullptr;
	QCheckBox        *chkShift       = nullptr;
	QCheckBox        *chkWin         = nullptr;
	QCheckBox        *chkLeftCtrl    = nullptr;
	QCheckBox        *chkRightCtrl   = nullptr;
	QCheckBox        *chkLeftAlt     = nullptr;
	QCheckBox        *chkRightAlt    = nullptr;
	QCheckBox        *chkLeftShift   = nullptr;
	QCheckBox        *chkRightShift  = nullptr;
	QCheckBox        *chkLeftWin     = nullptr;
	QCheckBox        *chkRightWin    = nullptr;

	
	QDoubleSpinBox *spZoom               = nullptr;
	QSpinBox       *spIn                 = nullptr;
	QSpinBox       *spOut                = nullptr;
	QCheckBox      *chkFollow            = nullptr;
	QDoubleSpinBox *spFollowSpeed        = nullptr;
	QCheckBox      *chkPortraitCover     = nullptr;
	QCheckBox      *chkShowCursorMarker  = nullptr;
	QCheckBox      *chkMarkerOnlyOnClick = nullptr;
	QSpinBox       *spMarkerSize         = nullptr;
	QSpinBox       *spMarkerThickness    = nullptr;
	QPushButton    *btnMarkerColor       = nullptr;
	QCheckBox      *chkDebug             = nullptr;

	
	QListWidget *lstSources = nullptr;

	
	QLabel      *lblStatus  = nullptr;
	QPushButton *btnRefresh = nullptr;
	QPushButton *btnApply   = nullptr;
	QPushButton *btnTest    = nullptr;

	
	QWidget *rowHotkeyWidget    = nullptr;
	QWidget *rowMouseWidget     = nullptr;
	QWidget *rowModifiersWidget = nullptr;

	bool loading = false;
};
