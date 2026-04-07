#pragma once

#include <obs-module.h>

#include <QObject>
#include <QPointer>
#include <QKeySequence>
#include <QTimer>
#include <QString>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#	undef WIN32_LEAN_AND_MEAN
#elif defined(__APPLE__)
#	include <CoreFoundation/CoreFoundation.h>
#	include <CoreGraphics/CoreGraphics.h>
#endif

class ZoominatorDialog;
class ZoominatorDock;

class ZoominatorController final : public QObject {
	Q_OBJECT

public:
	static ZoominatorController &instance();

	void initialize();
	void shutdown();
	void showDialog();
	void toggleDockVisibility(bool show);
	void saveSettings();
	void loadSettings();
	void notifySettingsChanged();

	QString sourceName;
	QString hotkeySequence;
	QString hotkeyMode;
	QString followToggleHotkeySequence;

	QString triggerType;       
	QString mouseButton;       
	bool modCtrl = false;
	bool modAlt = false;
	bool modShift = false;
	bool modWin = false;

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

signals:
	void settingsChanged();
	void dockVisibilityChanged(bool visible);

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

	obs_sceneitem_t *findTargetItemInCurrentScene() const;

	bool getCursorPos(int &x, int &y) const;
	bool mapCursorToSourcePixels(obs_source_t *src, int cursorX, int cursorY, float &sx, float &sy, bool &cursorInside) const;

	void captureOriginal(obs_sceneitem_t *item);
	void restoreOriginal(obs_sceneitem_t *item);
	QString markerImagePath() const;
	void ensureMarkerSource();
	obs_sceneitem_t *ensureMarkerItem(obs_scene_t *scene);
	void hideMarkerInScene(obs_scene_t *scene);
	void rebuildMarkerImage(int opacity255 = 255);
	void updateMarkerAppearance(int opacity255 = 255);
	void updateMarkerPosition(obs_scene_t *scene, double x, double y, int opacity255 = 255);
	bool captureMarkerClickPosition();
	bool isMarkerFlashActive(qint64 nowMs) const;
	int currentMarkerOpacity(qint64 nowMs);
	void applyZoom(obs_sceneitem_t *item, obs_source_t *src, double t);

	QTimer tickTimer;
	bool zoomPressed = false;
	bool zoomLatched = false;
	bool zoomActive = false;

	double animT = 0.0;
	int animDir = 0;

	bool followHasPos = false;
	float followX = 0.0f;
	float followY = 0.0f;

	bool markerClickHasPos = false;
	float markerClickX = 0.0f;
	float markerClickY = 0.0f;
	uint32_t markerAppearanceHash = 0;
	int markerRenderedOpacity = -1;
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
	} orig;

	QPointer<ZoominatorDialog> dialog;
	QPointer<ZoominatorDock> dock;
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
#endif
};
