#pragma once

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QObject>
#include <QPointer>
#include <QKeySequence>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <QElapsedTimer>
#include <QString>
#include <QHash>
#include <vector>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#	undef WIN32_LEAN_AND_MEAN
#elif defined(__APPLE__)
#	include <CoreFoundation/CoreFoundation.h>
#	include <CoreGraphics/CoreGraphics.h>
#elif defined(__linux__)

struct _XDisplay;
#endif

class ZoominatorDialog;

class ZoominatorController final : public QObject {
	Q_OBJECT

public:
	static ZoominatorController &instance();

	void initialize();
	void shutdown();
	void showDialog();
	void saveSettings();
	void loadSettings();
	void notifySettingsChanged();

	QString screenKey;
	QString hotkeySequence;
	QString hotkeyMode;
	QString followToggleHotkeySequence;

	QString triggerType;       
	QString mouseButton;       
	bool modCtrl = false;
	bool modAlt = false;
	bool modShift = false;
	bool modWin = false;
	bool modLeftCtrl = false;
	bool modRightCtrl = false;
	bool modLeftAlt = false;
	bool modRightAlt = false;
	bool modLeftShift = false;
	bool modRightShift = false;
	bool modLeftWin = false;
	bool modRightWin = false;

	double zoomFactor = 2.0;
	int animInMs = 180;
	int animOutMs = 180;
	bool followMouse = true;
	bool followMouseRuntimeEnabled = true;
	double followSpeed = 8.0; 
	bool portraitCover = true;
	bool showCursorMarker = false;
	bool markerOnlyOnClick = false;
	uint32_t markerColor = 0xFFFF0000;
	int markerSize = 26;
	int markerThickness = 4;
	bool debug = false;

	
	
	QSet<QString> excludedSources;

signals:
	void settingsChanged();

private slots:
	void onTick();

private:
	ZoominatorController();
	~ZoominatorController() override;
	ZoominatorController(const ZoominatorController &) = delete;
	ZoominatorController &operator=(const ZoominatorController &) = delete;

	QString configPath() const;

	void ensureTicking(bool on);
	void startZoomIn();
	void startZoomOut();
	void resetState();
	void rebuildTriggersFromSettings();
	void installHooks();
	void uninstallHooks();
	void onTriggerDown();
	void onTriggerUp();
	void toggleFollowMouseRuntime();
	bool triggerMatchesKeyboard(int vk) const;
	bool triggerMatchesMouse(unsigned int msg, unsigned short mouseData) const;
	bool modsMatch() const;

	bool getSelectedScreenRect(int &x, int &y, int &w, int &h) const;
	void enumerateTargetItemsInCurrentScene(std::vector<obs_sceneitem_t *> &items) const;

	bool getCursorPos(int &x, int &y) const;
	bool mapCursorToScenePixels(int cursorX, int cursorY, float &sx, float &sy, bool &cursorInside) const;

	void captureOriginal(obs_sceneitem_t *item);
	void restoreOriginal(obs_sceneitem_t *item);
	void captureOriginalSceneItems(const std::vector<obs_sceneitem_t *> &items);
	void restoreOriginalSceneItems(const std::vector<obs_sceneitem_t *> &items);
	void restoreOriginalSceneItemsFromState();
	QString markerImagePath() const;
	void ensureMarkerSource();
	obs_sceneitem_t *ensureMarkerItem(obs_scene_t *scene);
	void hideMarkerInScene(obs_scene_t *scene);
	void rebuildMarkerImage();
	void ensureMarkerFilter();
	void applyMarkerOpacity(int opacity255);
	void updateMarkerAppearance();
	void updateMarkerPosition(obs_scene_t *scene, double x, double y, int opacity255);
	bool captureMarkerClickPosition();
	bool isMarkerFlashActive(qint64 nowMs) const;
	int currentMarkerOpacity(qint64 nowMs);
	void applyZoomToScene(double t);

	QTimer tickTimer;
	bool zoomPressed = false;
	bool zoomLatched = false;
	bool zoomActive = false;

	double animT = 0.0;
	int animDir = 0;

	bool followHasPos = false;
	float followX = 0.0f;
	float followY = 0.0f;

	bool targetHasPos = false;
	double tickDeltaSeconds = 1.0 / 60.0;
	qint64 lastTickMs = 0;
	qint64 lastTransformApplyMs = 0;
	float lastFollowAnchorX = 0.0f;
	float lastFollowAnchorY = 0.0f;
	bool lastFollowAnchorValid = false;
	float targetX = 0.0f;
	float targetY = 0.0f;

	bool markerClickHasPos = false;
	float markerClickX = 0.0f;
	float markerClickY = 0.0f;
	uint32_t markerAppearanceHash = 0;
	int markerCurrentOpacity = -1;
	obs_source_t *markerFilter = nullptr;
	qint64 markerClickFlashStartMs = 0;
	qint64 markerClickFlashHoldUntilMs = 0;
	qint64 markerClickFlashFadeOutEndMs = 0;

	struct OrigState {
		bool valid = false;
		vec2 pos{};
		vec2 scale{};
		float rot = 0.0f;
		uint32_t align = 0;
		obs_bounds_type boundsType = OBS_BOUNDS_NONE;
		uint32_t boundsAlign = 0;
		vec2 bounds{};
		obs_sceneitem_crop crop{};
		vec2 effectivePos{};
		vec2 effectiveScale{};
	};

	struct SceneItemState {
		obs_sceneitem_t *item = nullptr;
		OrigState orig;
		bool normalized = false;
		bool lastAppliedValid = false;
		vec2 lastAppliedPos{};
		vec2 lastAppliedScale{};
	};

	QString sceneItemKey(obs_sceneitem_t *item) const;
	OrigState readSceneItemTransform(obs_sceneitem_t *item) const;
	void applySceneItemTransform(obs_sceneitem_t *item, const OrigState &state);
	void loadRecoveryMap(obs_data_t *data);
	void saveRecoveryMap(obs_data_t *data);
	void scheduleSettingsSave(int delayMs = 250);
	void restoreRecoveryIfNeeded();
	void markRecoveryActive();
	void clearRecoveryActive();
	void requestRecoveryRestore();
	static void frontendEventCallback(enum obs_frontend_event event, void *data);

	std::vector<SceneItemState> sceneItems;
	QHash<QString, OrigState> recoveryTransforms;
	bool pendingSettingsSave = false;
	bool shuttingDown = false;
	bool recoveryActive = false;
	bool restoringRecovery = false;
	bool sceneContentBoundsValid = false;
	vec2 sceneContentMin{};
	vec2 sceneContentMax{};

	QPointer<ZoominatorDialog> dialog;
	obs_source_t *markerSource = nullptr;

	int hotkeyVk = 0;
	bool hkValid = false;
	int followToggleHotkeyVk = 0;
	bool followToggleHkValid = false;
	bool followToggleModCtrl = false;
	bool followToggleModAlt = false;
	bool followToggleModShift = false;
	bool followToggleModWin = false;

#ifdef _WIN32
	static LRESULT CALLBACK kb_hook_proc(int nCode, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK mouse_hook_proc(int nCode, WPARAM wParam, LPARAM lParam);

	void *keyboardHook = nullptr;
	void *mouseHook = nullptr;
#elif defined(__APPLE__)
	static CGEventRef eventTapCallback(CGEventTapProxy proxy, CGEventType type,
					   CGEventRef event, void *refcon);
	CFMachPortRef eventTap = nullptr;
	CFRunLoopSourceRef runLoopSource = nullptr;
#elif defined(__linux__)
	_XDisplay *xiDisplay = nullptr;
	int xiOpcode = 0;
	QSocketNotifier *xiNotifier = nullptr;
	void processXInput2Events();
#endif
};
