#include <errno.h>
#include <pigpio.h>
#include <napi.h>

static void gpioISREventLoopHandler(uv_async_t* handle);
static void gpioAlertEventLoopHandler(uv_async_t* handle);

// TODO errors returned by uv calls are ignored


/* ------------------------------------------------------------------------ */
/* Gpio                                                                     */
/* ------------------------------------------------------------------------ */


class GpioCallback_t {
public:
  GpioCallback_t() : callback_(0), async_resource_(0) {
    Napi::HandleScope scope(env);
  }

  virtual ~GpioCallback_t() {
    Napi::HandleScope scope(env);

    if (callback_) {
      uv_unref((uv_handle_t *) &async_);
      delete callback_;
    }

    if (async_resource_) {
      delete async_resource_;
    }

    uv_close((uv_handle_t *) &async_, 0);

    callback_ = 0;
    async_resource_ = 0;
  }

  void AsyncSend() {
    uv_async_send(&async_);
  }

  void SetCallback(Napi::FunctionReference *callback) {
    if (callback_) {
      uv_unref((uv_handle_t *) &async_);
      delete callback_;
    }

    if (async_resource_) {
      delete async_resource_;
    }

    callback_ = callback;
    async_resource_ = 0;

    if (callback_) {
      async_resource_ = new Napi::AsyncResource("pigpio:eventHandler");
      uv_ref((uv_handle_t *) &async_);
    }
  }

  Napi::FunctionReference *Callback() {
    return callback_;
  }

  Napi::AsyncResource *Resource() {
    return async_resource_;
  }

protected:
  uv_async_t async_;

private:
  Napi::FunctionReference *callback_;
  Napi::AsyncResource *async_resource_;
};


class GpioISR_t : public GpioCallback_t {
public:
  GpioISR_t() : GpioCallback_t() {
    uv_async_init(uv_default_loop(), &async_, gpioISREventLoopHandler);

    // Prevent async from keeping event loop alive, for the time being.
    uv_unref((uv_handle_t *) &async_);
  }
};


class GpioAlert_t : public GpioCallback_t {
public:
  GpioAlert_t() : GpioCallback_t() {
    uv_async_init(uv_default_loop(), &async_, gpioAlertEventLoopHandler);

    // Prevent async from keeping event loop alive, for the time being.
    uv_unref((uv_handle_t *) &async_);
  }
};


static GpioISR_t *gpioISR_g;
static GpioAlert_t *gpioAlert_g;
static int gpio_g;
static int level_g;
static uint32_t tick_g;
static uv_sem_t sem_g;


void ThrowPigpioError(int err, const char *pigpiocall) {
  char buf[128];

  snprintf(buf, sizeof(buf), "pigpio error %d in %s", err, pigpiocall);

  Napi::Error::New(env, buf).ThrowAsJavaScriptException();

}


Napi::Value gpioHardwareRevision(const Napi::CallbackInfo& info) {
  return gpioHardwareRevision();
}


Napi::Value gpioCfgInterfaces(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioCfgInterfaces", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned ifFlags = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioCfgInterfaces(ifFlags);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioCfgInterfaces");
  }

  return rc;
}


Napi::Value gpioInitialise(const Napi::CallbackInfo& info) {
  int rc = gpioInitialise();
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioInitialise");
  }

  return rc;
}


Napi::Value gpioTerminate(const Napi::CallbackInfo& info) {
  gpioTerminate();
}


Napi::Value gpioSetMode(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioSetMode", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned mode = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioSetMode(gpio, mode);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioSetMode");
  }
}


Napi::Value gpioGetMode(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioGetMode", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned gpio = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioGetMode(gpio);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioGetMode");
  }

  return rc;
}


Napi::Value gpioSetPullUpDown(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioSetPullUpDown", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned pud = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioSetPullUpDown(gpio, pud);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioSetPullUpDown");
  }
}


Napi::Value gpioRead(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioRead", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned gpio = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioRead(gpio);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioRead");
  }

  return rc;
}


Napi::Value gpioWrite(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioSetPullUpDown", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned level = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioWrite(gpio, level);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioWrite");
  }
}


Napi::Value gpioTrigger(const Napi::CallbackInfo& info) {
  if (info.Length() < 3 || !info[0].IsUint32() || !info[1].IsUint32() || !info[2].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioTrigger", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned pulseLen = info[1].As<Napi::Number>().Uint32Value();
  unsigned level = info[2].As<Napi::Number>().Uint32Value();

  int rc = gpioTrigger(gpio, pulseLen, level);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioTrigger");
  }
}


Napi::Value gpioTick(const Napi::CallbackInfo& info) {
    uint32_t rc = gpioTick();
    return rc;
}


Napi::Value gpioPWM(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioPWM", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned dutycycle = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioPWM(user_gpio, dutycycle);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioPWM");
  }
}


Napi::Value gpioHardwarePWM(const Napi::CallbackInfo& info) {
  if (info.Length() < 3 ||
      !info[0].IsUint32() ||
      !info[1].IsUint32() ||
      !info[2].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioHardwarePWM", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned frequency = info[1].As<Napi::Number>().Uint32Value();
  unsigned dutycycle = info[2].As<Napi::Number>().Uint32Value();

  int rc = gpioHardwarePWM(gpio, frequency, dutycycle);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioHardwarePWM");
  }
}


Napi::Value gpioGetPWMdutycycle(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioGetPWMdutycycle", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioGetPWMdutycycle(user_gpio);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioGetPWMdutycycle");
  }

  return rc;
}


Napi::Value gpioSetPWMrange(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioSetPWMrange", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned range = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioSetPWMrange(user_gpio, range);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioSetPWMrange");
  }
}


Napi::Value gpioGetPWMrange(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioGetPWMrange", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioGetPWMrange(user_gpio);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioGetPWMrange");
  }

  return rc;
}


Napi::Value gpioGetPWMrealRange(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioGetPWMrealRange", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioGetPWMrealRange(user_gpio);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioGetPWMrealRange");
  }

  return rc;
}


Napi::Value gpioSetPWMfrequency(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioSetPWMfrequency", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned frequency = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioSetPWMfrequency(user_gpio, frequency);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioSetPWMfrequency");
  }
}


Napi::Value gpioGetPWMfrequency(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioGetPWMfrequency", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioGetPWMfrequency(user_gpio);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioGetPWMfrequency");
  }

  return rc;
}


Napi::Value gpioServo(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioServo", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned pulsewidth = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioServo(user_gpio, pulsewidth);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioServo");
  }
}


Napi::Value gpioGetServoPulsewidth(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioGetServoPulsewidth", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioGetServoPulsewidth(user_gpio);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioGetServoPulsewidth");
  }

  return rc;
}


// gpioISRHandler is not executed in the event loop thread
static void gpioISRHandler(int gpio, int level, uint32_t tick) {
  uv_sem_wait(&sem_g);

  gpio_g = gpio;
  level_g = level;
  tick_g = tick;

  gpioISR_g[gpio].AsyncSend();
}


// gpioISREventLoopHandler is executed in the event loop thread.
static void gpioISREventLoopHandler(uv_async_t* handle) {
  Napi::HandleScope scope(env);

  if (gpioISR_g[gpio_g].Callback()) {
    Napi::Value args[3] = {
      Napi::Number::New(env, gpio_g),
      Napi::Number::New(env, level_g),
      Napi::Number::New(env, tick_g)
    };

    gpioISR_g[gpio_g].Callback()->Call(
      3,
      args,
      gpioISR_g[gpio_g].Resource()
    );
  }

  uv_sem_post(&sem_g);
}


static Napi::Value gpioSetISRFunc(const Napi::CallbackInfo& info) {
  if (info.Length() < 3 ||
      !info[0].IsUint32() ||
      !info[1].IsUint32() ||
      !info[2].IsNumber()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioSetISRFunc", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() >= 4 &&
      !info[3].IsFunction() &&
      !info[3].IsNull() &&
      !info[3].IsUndefined()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioSetISRFunc", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned edge = info[1].As<Napi::Number>().Uint32Value();
  int timeout = info[2].As<Napi::Number>().Int32Value();
  Napi::FunctionReference *callback = 0;
  gpioISRFunc_t isrFunc = 0;

  if (info.Length() >= 4 && info[3].IsFunction()) {
    callback = new Napi::FunctionReference(info[3].As<Napi::Function>());
    isrFunc = gpioISRHandler;
  }

  gpioISR_g[user_gpio].SetCallback(callback);

  int rc = gpioSetISRFunc(user_gpio, edge, timeout, isrFunc);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioSetISRFunc");
  }
}


// gpioAlertHandler is not executed in the event loop thread
static void gpioAlertHandler(int gpio, int level, uint32_t tick) {
  uv_sem_wait(&sem_g);

  gpio_g = gpio;
  level_g = level;
  tick_g = tick;

  gpioAlert_g[gpio].AsyncSend();
}


// gpioAlertEventLoopHandler is executed in the event loop thread.
static void gpioAlertEventLoopHandler(uv_async_t* handle) {
  Napi::HandleScope scope(env);

  if (gpioAlert_g[gpio_g].Callback()) {
    Napi::Value args[3] = {
      Napi::Number::New(env, gpio_g),
      Napi::Number::New(env, level_g),
      Napi::Number::New(env, tick_g)
    };

    gpioAlert_g[gpio_g].Callback()->Call(
      3,
      args,
      gpioAlert_g[gpio_g].Resource()
    );
  }

  uv_sem_post(&sem_g);
}


static Napi::Value gpioSetAlertFunc(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 ||
      !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioSetAlertFunc")).ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() >= 2 &&
      !info[1].IsFunction() &&
      !info[1].IsNull() &&
      !info[1].IsUndefined()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioSetAlertFunc")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();
  Napi::FunctionReference *callback = 0;
  gpioAlertFunc_t alertFunc = 0;

  if (info.Length() >= 2 && info[1].IsFunction()) {
    callback = new Napi::FunctionReference(info[1].As<Napi::Function>());
    alertFunc = gpioAlertHandler;
  }

  gpioAlert_g[user_gpio].SetCallback(callback);

  int rc = gpioSetAlertFunc(user_gpio, alertFunc);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioSetAlertFunc");
  }
}


Napi::Value gpioGlitchFilter(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioGlitchFilter", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned user_gpio = info[0].As<Napi::Number>().Uint32Value();
  unsigned steady = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioGlitchFilter(user_gpio, steady);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioGlitchFilter");
  }
}


/* ------------------------------------------------------------------------ */
/* GpioBank                                                                 */
/* ------------------------------------------------------------------------ */


Napi::Value GpioReadBits_0_31(const Napi::CallbackInfo& info) {
  return gpioRead_Bits_0_31();
}


Napi::Value GpioReadBits_32_53(const Napi::CallbackInfo& info) {
  return gpioRead_Bits_32_53();
}


Napi::Value GpioWriteBitsSet_0_31(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "GpioWriteBitsSet_0_31", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned bits = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioWrite_Bits_0_31_Set(bits);
  if (rc < 0) {
    return ThrowPigpioError(rc, "GpioWriteBitsSet_0_31");
  }

  return rc;
}


Napi::Value GpioWriteBitsSet_32_53(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "GpioWriteBitsSet_32_53", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned bits = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioWrite_Bits_32_53_Set(bits);
  if (rc < 0) {
    return ThrowPigpioError(rc, "GpioWriteBitsSet_32_53");
  }

  return rc;
}


Napi::Value GpioWriteBitsClear_0_31(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "GpioWriteBitsClear_0_31", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned bits = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioWrite_Bits_0_31_Clear(bits);
  if (rc < 0) {
    return ThrowPigpioError(rc, "GpioWriteBitsClear_0_31");
  }

  return rc;
}


Napi::Value GpioWriteBitsClear_32_53(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "GpioWriteBitsClear_32_53", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned bits = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioWrite_Bits_32_53_Clear(bits);
  if (rc < 0) {
    return ThrowPigpioError(rc, "GpioWriteBitsClear_32_53");
  }

  return rc;
}


/* ------------------------------------------------------------------------ */
/* Notifier                                                                 */
/* ------------------------------------------------------------------------ */


Napi::Value gpioNotifyOpen(const Napi::CallbackInfo& info) {
  int rc = gpioNotifyOpen();
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioNotifyOpen");
  }

  return rc;
}


Napi::Value gpioNotifyOpenWithSize(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioNotifyOpenWithSize", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned bufSize = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioNotifyOpenWithSize(bufSize);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioNotifyOpen");
  }

  return rc;
}


Napi::Value gpioNotifyBegin(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioNotifyBegin", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned handle = info[0].As<Napi::Number>().Uint32Value();
  unsigned bits = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioNotifyBegin(handle, bits);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioNotifyBegin");
  }
}


Napi::Value gpioNotifyPause(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioNotifyPause", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned handle = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioNotifyPause(handle);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioNotifyPause");
  }
}


Napi::Value gpioNotifyClose(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioNotifyClose", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned handle = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioNotifyClose(handle);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioNotifyClose");
  }
}


/* ------------------------------------------------------------------------ */
/* Waves                                                                    */
/* ------------------------------------------------------------------------ */


Napi::Value gpioWaveClear(const Napi::CallbackInfo& info) {
  int rc = gpioWaveClear();
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioWaveClear");
  }
}


Napi::Value gpioWaveAddNew(const Napi::CallbackInfo& info) {
  int rc = gpioWaveAddNew();
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioWaveAddNew");
  }
}

Napi::Value gpioWaveAddGeneric(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsArray()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioWaveAddGeneric", "")).ThrowAsJavaScriptException();
    return env.Null();
  }


  Napi::Array array = info[0].As<Napi::Array>();
  unsigned numPulses = array->Length();

  gpioPulse_t pulses[numPulses];

  for (unsigned i = 0; i < array->Length(); i++) {
    if ((array).Has(i)) {
      Napi::Value element = (array).Get(i);

      if (!element.IsObject()) {
        Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioWaveAddGeneric", "")).ThrowAsJavaScriptException();
        return env.Null(); 
      }

      Napi::Object pulse = element.As<Napi::Object>();

      Napi::Value _on =
        (pulse).Get(Napi::String::New(env, "gpioOn")).
        ToLocalChecked();
      Napi::Value _off =
        (pulse).Get(Napi::String::New(env, "gpioOff")).
        ToLocalChecked();
      Napi::Value _delay =
        (pulse).Get(Napi::String::New(env, "usDelay")).
        ToLocalChecked();

      if (!_on->IsUint32() || !_off->IsUint32() || !_delay->IsUint32()){
        Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioWaveAddGeneric", "")).ThrowAsJavaScriptException();
        return env.Null();
      }

      uint32_t on = _on->Uint32Value(Napi::GetCurrentContext());
      uint32_t off = _off->Uint32Value(Napi::GetCurrentContext());
      uint32_t delay = _delay->Uint32Value(Napi::GetCurrentContext());

      if (on > 0) {
        on = 1 << on;
      }
      if (off > 0) {
        off = 1 << off;
      }

      pulses[i].gpioOn = on;
      pulses[i].gpioOff = off;
      pulses[i].usDelay = delay;
    }
  }

  int rc = gpioWaveAddGeneric(numPulses, pulses);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioWaveAddGeneric");
  }

  return rc;
}


Napi::Value gpioWaveCreate(const Napi::CallbackInfo& info) {
  int rc = gpioWaveCreate();
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioWaveCreate");
  }

  return rc;
}


Napi::Value gpioWaveDelete(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioWaveDelete", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned waveId = info[0].As<Napi::Number>().Uint32Value();

  int rc = gpioWaveDelete(waveId);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioWaveDelete");
  }
}


Napi::Value gpioWaveTxSend(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioWaveTxSend", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned waveId = info[0].As<Napi::Number>().Uint32Value();
  unsigned waveMode = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioWaveTxSend(waveId, waveMode);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioWaveTxSend");
  }

  return rc;
}


Napi::Value gpioWaveChain(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || info[0].IsNull() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioWaveChain", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  char * buf = info[0].As<Napi::Buffer<char>>().Data();
  unsigned bufSize = info[1].As<Napi::Number>().Uint32Value();

  int rc = gpioWaveChain(buf, bufSize);
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioWaveChain");
  }
}


Napi::Value gpioWaveTxAt(const Napi::CallbackInfo& info) {
  int rc = gpioWaveTxAt();
  if (rc >= PI_WAVE_NOT_FOUND) {
    return ThrowPigpioError(rc, "gpioWaveTxAt");
  }

  return rc;
}


Napi::Value gpioWaveTxBusy(const Napi::CallbackInfo& info) {
  int rc = gpioWaveTxBusy();
  return rc;
}


Napi::Value gpioWaveTxStop(const Napi::CallbackInfo& info) {
  int rc = gpioWaveTxStop();
  if (rc < 0) {
    return ThrowPigpioError(rc, "gpioWaveTxStop");
  }
}


Napi::Value gpioWaveGetMicros(const Napi::CallbackInfo& info) {
  int rc = gpioWaveGetMicros();
  return rc;
}


Napi::Value gpioWaveGetHighMicros(const Napi::CallbackInfo& info) {
  int rc = gpioWaveGetHighMicros();
  return rc;
}


Napi::Value gpioWaveGetMaxMicros(const Napi::CallbackInfo& info) {
  int rc = gpioWaveGetMaxMicros();
  return rc;
}


Napi::Value gpioWaveGetPulses(const Napi::CallbackInfo& info) {
  int rc = gpioWaveGetPulses();
  return rc;
}


Napi::Value gpioWaveGetHighPulses(const Napi::CallbackInfo& info) {
  int rc = gpioWaveGetHighPulses();
  return rc;
}


Napi::Value gpioWaveGetMaxPulses(const Napi::CallbackInfo& info) {
  int rc = gpioWaveGetMaxPulses();
  return rc;
}


Napi::Value gpioWaveGetCbs(const Napi::CallbackInfo& info) {
  int rc = gpioWaveGetCbs();
  return rc;
}


Napi::Value gpioWaveGetHighCbs(const Napi::CallbackInfo& info) {
  int rc = gpioWaveGetHighCbs();
  return rc;
}


Napi::Value gpioWaveGetMaxCbs(const Napi::CallbackInfo& info) {
  int rc = gpioWaveGetMaxCbs();
  return rc;
}


/* ------------------------------------------------------------------------ */
/* Configuration                                                            */
/* ------------------------------------------------------------------------ */


Napi::Value gpioCfgClock(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsUint32() || !info[1].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioCfgClock", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned cfgMicros = info[0].As<Napi::Number>().Uint32Value();
  unsigned cfgPeripheral = info[1].As<Napi::Number>().Uint32Value();
  unsigned cfgSource = 0;

  gpioCfgClock(cfgMicros, cfgPeripheral, cfgSource);
}


Napi::Value gpioCfgSocketPort(const Napi::CallbackInfo& info) {
  if (info.Length() < 1 || !info[0].IsUint32()) {
    Napi::Error::New(env, Napi::ErrnoException(EINVAL, "gpioCfgSocketPort", "")).ThrowAsJavaScriptException();
    return env.Null();
  }

  unsigned cfgPort = info[0].As<Napi::Number>().Uint32Value();

  gpioCfgSocketPort(cfgPort);
}


/*static void SetConst(
  Napi::Env& env, Napi::Object& target,
  const char* name,
  int value
) {
  (target).Set(Napi::String::New(env, name),
    Napi::Number::New(env, value)
  );
}*/


static void SetFunction(
  Napi::Env& env, Napi::Object& target,
  const char* name,
  Napi::FunctionCallback callback
) {
  (target).Set(Napi::New(env, name),
    Napi::GetFunction(Napi::Function::New(env, callback))
  );
}


Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  uv_sem_init(&sem_g, 1);

  /* mode constants */
/*  SetConst(target, "PI_INPUT", PI_INPUT);
  SetConst(target, "PI_OUTPUT", PI_OUTPUT);
  SetConst(target, "PI_ALT0", PI_ALT0);
  SetConst(target, "PI_ALT1", PI_ALT1);
  SetConst(target, "PI_ALT2", PI_ALT2);
  SetConst(target, "PI_ALT3", PI_ALT3);
  SetConst(target, "PI_ALT4", PI_ALT4);
  SetConst(target, "PI_ALT5", PI_ALT5);*/

  /* pud constants */
/*  SetConst(target, "PI_PUD_OFF", PI_PUD_OFF);
  SetConst(target, "PI_PUD_DOWN", PI_PUD_DOWN);
  SetConst(target, "PI_PUD_UP", PI_PUD_UP);*/

  /* isr constants */
/*  SetConst(target, "RISING_EDGE", RISING_EDGE);
  SetConst(target, "FALLING_EDGE", FALLING_EDGE);
  SetConst(target, "EITHER_EDGE", EITHER_EDGE);*/

  /* timeout constant */
/*  SetConst(target, "PI_TIMEOUT", PI_TIMEOUT); */

  /* gpio number constants */
/*  SetConst(target, "PI_MIN_GPIO", PI_MIN_GPIO);
  SetConst(target, "PI_MAX_GPIO", PI_MAX_GPIO);
  SetConst(target, "PI_MAX_USER_GPIO", PI_MAX_USER_GPIO);*/

  /* error code constants */
/*  SetConst(target, "PI_INIT_FAILED", PI_INIT_FAILED);*/

  /* gpioCfgClock cfgPeripheral constants */
/*  SetConst(target, "PI_CLOCK_PWM", PI_CLOCK_PWM);
  SetConst(target, "PI_CLOCK_PCM", PI_CLOCK_PCM);*/

  /* functions */
  SetFunction(target, "gpioHardwareRevision", gpioHardwareRevision);
  SetFunction(target, "gpioCfgInterfaces", gpioCfgInterfaces);
  SetFunction(target, "gpioInitialise", gpioInitialise);
  SetFunction(target, "gpioTerminate", gpioTerminate);

  SetFunction(target, "gpioSetMode", gpioSetMode);
  SetFunction(target, "gpioGetMode", gpioGetMode);

  SetFunction(target, "gpioSetPullUpDown", gpioSetPullUpDown);

  SetFunction(target, "gpioRead", gpioRead);
  SetFunction(target, "gpioWrite", gpioWrite);
  SetFunction(target, "gpioTrigger", gpioTrigger);

  SetFunction(target, "gpioPWM", gpioPWM);
  SetFunction(target, "gpioHardwarePWM", gpioHardwarePWM);
  SetFunction(target, "gpioGetPWMdutycycle", gpioGetPWMdutycycle);
  SetFunction(target, "gpioSetPWMrange", gpioSetPWMrange);
  SetFunction(target, "gpioGetPWMrange", gpioGetPWMrange);
  SetFunction(target, "gpioGetPWMrealRange", gpioGetPWMrealRange);
  SetFunction(target, "gpioSetPWMfrequency", gpioSetPWMfrequency);
  SetFunction(target, "gpioGetPWMfrequency", gpioGetPWMfrequency);

  SetFunction(target, "gpioServo", gpioServo);
  SetFunction(target, "gpioGetServoPulsewidth", gpioGetServoPulsewidth);

  SetFunction(target, "gpioSetISRFunc", gpioSetISRFunc);
  SetFunction(target, "gpioSetAlertFunc", gpioSetAlertFunc);
  SetFunction(target, "gpioGlitchFilter", gpioGlitchFilter);

  SetFunction(target, "GpioReadBits_0_31", GpioReadBits_0_31);
  SetFunction(target, "GpioReadBits_32_53", GpioReadBits_32_53);
  SetFunction(target, "GpioWriteBitsSet_0_31", GpioWriteBitsSet_0_31);
  SetFunction(target, "GpioWriteBitsSet_32_53", GpioWriteBitsSet_32_53);
  SetFunction(target, "GpioWriteBitsClear_0_31", GpioWriteBitsClear_0_31);
  SetFunction(target, "GpioWriteBitsClear_32_53", GpioWriteBitsClear_32_53);

  SetFunction(target, "gpioNotifyOpen", gpioNotifyOpen);
  SetFunction(target, "gpioNotifyOpenWithSize", gpioNotifyOpenWithSize);
  SetFunction(target, "gpioNotifyBegin", gpioNotifyBegin);
  SetFunction(target, "gpioNotifyPause", gpioNotifyPause);
  SetFunction(target, "gpioNotifyClose", gpioNotifyClose);

  SetFunction(target, "gpioWaveClear", gpioWaveClear);
  SetFunction(target, "gpioWaveAddNew", gpioWaveAddNew);
  SetFunction(target, "gpioWaveAddGeneric", gpioWaveAddGeneric);
  SetFunction(target, "gpioWaveCreate", gpioWaveCreate);
  SetFunction(target, "gpioWaveDelete", gpioWaveDelete);
  SetFunction(target, "gpioWaveTxSend", gpioWaveTxSend);
  SetFunction(target, "gpioWaveChain", gpioWaveChain);
  SetFunction(target, "gpioWaveTxAt", gpioWaveTxAt);
  SetFunction(target, "gpioWaveTxBusy", gpioWaveTxBusy);
  SetFunction(target, "gpioWaveTxStop", gpioWaveTxStop);
  SetFunction(target, "gpioWaveGetMicros", gpioWaveGetMicros);
  SetFunction(target, "gpioWaveGetHighMicros", gpioWaveGetHighMicros);
  SetFunction(target, "gpioWaveGetMaxMicros", gpioWaveGetMaxMicros);
  SetFunction(target, "gpioWaveGetPulses", gpioWaveGetPulses);
  SetFunction(target, "gpioWaveGetHighPulses", gpioWaveGetHighPulses);
  SetFunction(target, "gpioWaveGetMaxPulses", gpioWaveGetMaxPulses);
  SetFunction(target, "gpioWaveGetCbs", gpioWaveGetCbs);
  SetFunction(target, "gpioWaveGetHighCbs", gpioWaveGetHighCbs);
  SetFunction(target, "gpioWaveGetMaxCbs", gpioWaveGetMaxCbs);

  SetFunction(target, "gpioCfgClock", gpioCfgClock);
  SetFunction(target, "gpioCfgSocketPort", gpioCfgSocketPort);

  SetFunction(target, "gpioTick", gpioTick);

  gpioISR_g = new GpioISR_t[PI_MAX_USER_GPIO + 1];
  gpioAlert_g = new GpioAlert_t[PI_MAX_USER_GPIO + 1];
}

NODE_API_MODULE(pigpio, InitAll)
