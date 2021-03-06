#ifndef __HAL__LIBOPENCM3HAL_H__
#define __HAL__LIBOPENCM3HAL_H__

namespace hal {

/*
 * \brief Class LibOpencm3Hal
 */
class LibOpencm3Hal {
 public:
  void begin() {
    beginClock();
    beginGpio();
    beginLocoNet();
    beginXpressNet();
  }

  void led(bool on);
  void toggleLed();

 private:
  void beginClock();
  void beginGpio();
  void beginLocoNet();
  void beginXpressNet();
};

}  // namespace hal

#endif  // __HAL__LIBOPENCM3HAL_H__
