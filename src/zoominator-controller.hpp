#pragma once

#include <obs-module.h>

#include <QObject>
#include <QPointer>
#include <QKeySequence>
#include <QTimer>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#	undef WIN32_LEAN_AND_MEAN
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
	double followSpeed = 8.0; 
	bool portraitCover = true;
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

	// Trigger (hooks)
	void rebuildTriggersFromSettings();
	void installHooks();
	void uninstallHooks();
	void onTriggerDown();
	void onTriggerUp();
	bool triggerMatchesKeyboard(int vk) const;
	bool triggerMatchesMouse(unsigned int msg, unsigned short mouseData) const;
	bool modsMatch() const;

	// Target resolution (current scene)
	obs_sceneitem_t *findTargetItemInCurrentScene() const;

	// Mouse mapping to source pixels
	bool getCursorPos(int &x, int &y) const;
	bool mapCursorToSourcePixels(obs_source_t *src, int cursorX, int cursorY, float &sx, float &sy, bool &cursorInside) const;

	// Apply
	void captureOriginal(obs_sceneitem_t *item);
	void restoreOriginal(obs_sceneitem_t *item);
	void applyZoom(obs_sceneitem_t *item, obs_source_t *src, double t);

	// Animation state
	QTimer tickTimer;
	bool zoomPressed = false;
	bool zoomLatched = false;
	bool zoomActive = false;

	// 0..1
	double animT = 0.0;
	int animDir = 0; // +1 in, -1 out

	// follow smoothing in source pixels
	bool followHasPos = false;
	float followX = 0.0f;
	float followY = 0.0f;

	// Original item state
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

	// Parsed keyboard trigger (Windows VK + modifiers)
	int hotkeyVk = 0;
	bool hkValid = false;

#ifdef _WIN32
	// Low-level hook callbacks must be plain functions. Declaring them as
	// static members keeps access to private controller state.
	static LRESULT CALLBACK kb_hook_proc(int nCode, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK mouse_hook_proc(int nCode, WPARAM wParam, LPARAM lParam);

	void *keyboardHook = nullptr;
	void *mouseHook = nullptr;
#endif
};
