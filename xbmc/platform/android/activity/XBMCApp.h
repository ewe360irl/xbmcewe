/*
 *  Copyright (C) 2012-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "IActivityHandler.h"
#include "IInputHandler.h"
#include "JNIMainActivity.h"
#include "JNIXBMCAudioManagerOnAudioFocusChangeListener.h"
#include "JNIXBMCDisplayManagerDisplayListener.h"
#include "JNIXBMCMainView.h"
#include "JNIXBMCMediaSession.h"
#include "interfaces/IAnnouncer.h"
#include "platform/xbmc.h"
#include "threads/Event.h"
#include "utils/Geometry.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <android/native_activity.h>
#include <androidjni/Activity.h>
#include <androidjni/AudioManager.h>
#include <androidjni/BroadcastReceiver.h>
#include <androidjni/SurfaceHolder.h>
#include <androidjni/View.h>

// forward declares
class CAESinkAUDIOTRACK;
class CVariant;
class IInputDeviceCallbacks;
class IInputDeviceEventHandler;
class CVideoSyncAndroid;

typedef struct _JNIEnv JNIEnv;

struct androidIcon
{
  unsigned int width;
  unsigned int height;
  void *pixels;
};

struct androidPackage
{
  std::string packageName;
  std::string packageLabel;
  int icon;
};

class CNativeWindow
{
  friend class CWinSystemAndroidGLESContext; // meh

public:
  static std::shared_ptr<CNativeWindow> CreateFromSurface(CJNISurfaceHolder holder);
  ~CNativeWindow();

  bool SetBuffersGeometry(int width, int height, int format);
  int32_t GetWidth() const;
  int32_t GetHeight() const;

private:
  explicit CNativeWindow(ANativeWindow* window);

  CNativeWindow() = delete;
  CNativeWindow(const CNativeWindow&) = delete;
  CNativeWindow& operator=(const CNativeWindow&) = delete;

  ANativeWindow* m_window{nullptr};
};

class CXBMCApp : public IActivityHandler,
                 public jni::CJNIMainActivity,
                 public CJNIBroadcastReceiver,
                 public ANNOUNCEMENT::IAnnouncer,
                 public CJNISurfaceHolderCallback
{
public:
  static CXBMCApp& Create(ANativeActivity* nativeActivity, IInputHandler& inputhandler)
  {
    m_appinstance.reset(new CXBMCApp(nativeActivity, inputhandler));
    return *m_appinstance;
  }
  static CXBMCApp& Get() { return *m_appinstance; }
  static void Destroy() { m_appinstance.reset(); }

  CXBMCApp() = delete;
  ~CXBMCApp() override;

  // IAnnouncer IF
  void Announce(ANNOUNCEMENT::AnnouncementFlag flag,
                const std::string& sender,
                const std::string& message,
                const CVariant& data) override;

  void onReceive(CJNIIntent intent) override;
  void onNewIntent(CJNIIntent intent) override;
  void onActivityResult(int requestCode, int resultCode, CJNIIntent resultData) override;
  void onVolumeChanged(int volume) override;
  virtual void onAudioFocusChange(int focusChange);
  void doFrame(int64_t frameTimeNanos) override;
  void onVisibleBehindCanceled() override;

  // implementation of CJNIInputManagerInputDeviceListener
  void onInputDeviceAdded(int deviceId) override;
  void onInputDeviceChanged(int deviceId) override;
  void onInputDeviceRemoved(int deviceId) override;

  // implementation of DisplayManager::DisplayListener
  void onDisplayAdded(int displayId) override;
  void onDisplayChanged(int displayId) override;
  void onDisplayRemoved(int displayId) override;
  jni::jhobject getDisplayListener() { return m_displayListener.get_raw(); }

  bool isValid() { return m_activity != NULL; }

  void onStart() override;
  void onResume() override;
  void onPause() override;
  void onStop() override;
  void onDestroy() override;

  void onSaveState(void **data, size_t *size) override;
  void onConfigurationChanged() override;
  void onLowMemory() override;

  void onCreateWindow(ANativeWindow* window) override;
  void onResizeWindow() override;
  void onDestroyWindow() override;
  void onGainFocus() override;
  void onLostFocus() override;

  void CXBMCApp::Initialize()
{
    // Existing implementation...
    
    // Setup memory monitoring
    m_memoryMonitorTimer.Start(10000); // Check every 10 seconds
    m_memoryMonitorTimer.SetCallback(std::bind(&CXBMCApp::MonitorMemoryUsage, this));
}
  void Deinitialize();

  bool Stop(int exitCode);
  void Quit();

  std::shared_ptr<CNativeWindow> GetNativeWindow(int timeout) const;

  bool SetBuffersGeometry(int width, int height, int format);
  static int android_printf(const char *format, ...);

  int GetBatteryLevel() const;
  void KeepScreenOn(bool on);
  bool HasFocus() const { return m_hasFocus; }

  static bool StartActivity(const std::string& package,
                            const std::string& intent = std::string(),
                            const std::string& dataType = std::string(),
                            const std::string& dataURI = std::string(),
                            const std::string& flags = std::string(),
                            const std::string& extras = std::string(),
                            const std::string& action = std::string(),
                            const std::string& category = std::string(),
                            const std::string& className = std::string());
  std::vector<androidPackage> GetApplications() const;

  static int GetMaxSystemVolume();
  static float GetSystemVolume();
  static void SetSystemVolume(float percent);

  void SetDisplayMode(int mode, float rate);
  int GetDPI() const;
  void SetVideoLayoutBackgroundColor(const int color);

  CRect MapRenderToDroid(const CRect& srcRect);

  // Playback callbacks
  void OnPlayBackStarted();
  void OnPlayBackPaused();
  void OnPlayBackStopped();

  // Info callback
  void UpdateSessionMetadata();
  void UpdateSessionState();

  // input device methods
  void RegisterInputDeviceCallbacks(IInputDeviceCallbacks* handler);
  void UnregisterInputDeviceCallbacks();
  static const CJNIViewInputDevice GetInputDevice(int deviceId);
  static std::vector<int> GetInputDeviceIds();

  void RegisterInputDeviceEventHandler(IInputDeviceEventHandler* handler);
  void UnregisterInputDeviceEventHandler();
  bool onInputDeviceEvent(const AInputEvent* event);

  void InitFrameCallback(CVideoSyncAndroid* syncImpl);
  void DeinitFrameCallback();

  // Application slow ping
  void ProcessSlow();

  bool WaitVSync(unsigned int milliSeconds);
  int64_t GetNextFrameTime() const;
  float GetFrameLatencyMs() const;

  bool getVideosurfaceInUse();
  void setVideosurfaceInUse(bool videosurfaceInUse);

protected:
  // limit who can access Volume
  friend class CAESinkAUDIOTRACK;

  static int GetMaxSystemVolume(JNIEnv *env);
  bool AcquireAudioFocus();
  bool ReleaseAudioFocus();
  void RequestVisibleBehind(bool requested);

private:
  static std::unique_ptr<CXBMCApp> m_appinstance;

  CXBMCApp(ANativeActivity* nativeActivity, IInputHandler& inputhandler);

  jni::CJNIXBMCAudioManagerOnAudioFocusChangeListener m_audioFocusListener;
  jni::CJNIXBMCDisplayManagerDisplayListener m_displayListener;
  std::unique_ptr<jni::CJNIXBMCMainView> m_mainView;
  std::unique_ptr<jni::CJNIXBMCMediaSession> m_mediaSession;
  std::string GetFilenameFromIntent(const CJNIIntent &intent);

  void run();
  void stop();
  void SetupEnv();
  static void SetDisplayModeCallback(void* modeVariant);
  static void KeepScreenOnCallback(void* onVariant);
  static void SetViewBackgroundColorCallback(void* mapVariant);

  static void RegisterDisplayListenerCallback(void*);
  void UnregisterDisplayListener();

  ANativeActivity* m_activity{nullptr};
  IInputHandler& m_inputHandler;
  int m_batteryLevel{0};
  bool m_hasFocus{false};
  bool m_headsetPlugged{false};
  bool m_hdmiSource{false};
  bool m_wakeUp{false};
  bool m_aeReset{false};
  bool m_hdmiPlugged{true};
  bool m_mediaSessionUpdated{false};
  IInputDeviceCallbacks* m_inputDeviceCallbacks{nullptr};
  IInputDeviceEventHandler* m_inputDeviceEventHandler{nullptr};
  bool m_hasReqVisible{false};
  bool m_firstrun{true};
  std::atomic<bool> m_exiting{false};
  int m_exitCode{0};
  bool m_bResumePlayback{false};
  std::thread m_thread;
  mutable CCriticalSection m_applicationsMutex;
  mutable std::vector<androidPackage> m_applications;

  std::shared_ptr<CNativeWindow> m_window;

  CVideoSyncAndroid* m_syncImpl{nullptr};
  CEvent m_vsyncEvent;
  CEvent m_displayChangeEvent;

  bool XBMC_DestroyDisplay();
  bool XBMC_SetupDisplay();

  void OnSleep();
  void OnWakeup();

  uint32_t m_playback_state{0};
  int64_t m_frameTimeNanos{0};
  float m_refreshRate{0.0f};

public:
  // CJNISurfaceHolderCallback interface
  void surfaceChanged(CJNISurfaceHolder holder, int format, int width, int height) override;
  void surfaceCreated(CJNISurfaceHolder holder) override;
  void surfaceDestroyed(CJNISurfaceHolder holder) override;

private:
    static constexpr size_t MIN_MEMORY_FOR_PLAYBACK = 50 * 1024 * 1024; // 50MB
    static constexpr size_t BUFFER_CACHE_SIZE = 10 * 1024 * 1024; // 10MB
    std::unique_ptr<CMemoryManager> m_memoryManager;
    
    void InitializeMemoryManager();
    void HandleLowMemory();
private:
    void MonitorMemoryUsage();
    CTimer m_memoryMonitorTimer;
void CXBMCApp::MonitorMemoryUsage()
{
    KODI::MEMORY::MemoryStatus status;
    KODI::MEMORY::GetMemoryStatus(&status);
    
    // If memory usage is high, take action
    if (status.availPhys < MIN_MEMORY_FOR_PLAYBACK)
    {
        CLog::Log(LOGWARNING, "Memory usage critical: %lu bytes available", status.availPhys);
        HandleLowMemory();
    }
}
};
