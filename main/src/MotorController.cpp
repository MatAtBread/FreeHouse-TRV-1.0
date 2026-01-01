#include "MotorController.h"

#include "../common/gpio/gpio.hpp"
#include "../trv.h"
#include "pins.h"

#if BUILD_FREEHOUSE_MODEL == TRV1
#define maxMotorTime 24000
#else
#define maxMotorTime 12000
#endif

#define minMotorTime (maxMotorTime / 25)
#define peakLoPercent 92
#define peakHiPercent 102

/* In testing:
  typical Vshunt at full stall in 0.23v (batt=4110mv, R=0.66ohms), making I=0.338mA and Rmotor=12.16-Rshunt, or 11.48ohms
  In-rush Vshunt on *reversal* is 0.38v, from stationary is 0.21v
*/

static RTC_DATA_ATTR int trackRatio; // Initialised to 0 on boot, except for deep-sleep wake
const char* MotorController::lastStatus = "idle";

MotorController::MotorController(BatteryMonitor* battery, uint8_t& current, motor_params_t& params) : battery(battery), current(current), params(params) {
  target = current;
  GPIO::pinMode(NSLEEP, OUTPUT);
  GPIO::pinMode(MOTOR, OUTPUT);
  setDirection(0);
  if (trackRatio == 0) {
    calibrate();
    ESP_LOGI(TAG, "MotorController::MotorController initialized, trackRatio=%d", trackRatio);}
  }

int MotorController::getDirection() {
  if (GPIO::digitalRead(NSLEEP) == false) return 0;
  return GPIO::digitalRead(MOTOR) != params.reversed ? 1 : -1;
}

void MotorController::setDirection(int dir) {
  // We don't need to enable the task here, as this protected method can only be called from within the task()
  ESP_LOGI(TAG, "MotorController::setDirection %d", dir);
  switch (dir) {
    case -1:
      GPIO::digitalWrite(MOTOR, false != params.reversed);
      GPIO::digitalWrite(NSLEEP, true);
      break;
    case 1:
      GPIO::digitalWrite(MOTOR, true != params.reversed);
      GPIO::digitalWrite(NSLEEP, true);
      break;
    default:
      GPIO::digitalWrite(NSLEEP, false);
      break;
  }
}

void MotorController::setValvePosition(int pos) {
  if (pos == -1) {
    target = current;
    setDirection(0);
    return;
  }
  if (pos < 0 || pos > 100) {
    ESP_LOGW(TAG, "MotorController::setValvePosition - illegal position %d", pos);
    return;
  }
  ESP_LOGI(TAG, "MotorController::setValvePosition %d %d", pos, target);
  // Let task pick up the change (we could wait until the next dreamtime)
  target = pos;
  StartTask(MotorController);
}

uint8_t MotorController::getValvePosition() {
  return current;
}

void MotorController::calibrate() {
  ESP_LOGI(TAG, "MotorController::calibrate");
  setValvePosition(100);
  wait();
  setValvePosition(0);
  wait();
  setValvePosition(100);
  wait();
}

static char bar[200];
static void charChart(int b, char c) {
  if (b > sizeof(bar) - 3) b = sizeof(bar) - 2;
  bar[b] = c;
}

class MovingAverage {
  private:
    int *values;
    int index;
    int count;
    int total;
    int size;

  public:
    MovingAverage(int size) : index(0), count(0), total(0), size(size) {
      values = new int[size];
      memset(values, 0, sizeof(int) * size);
    }

    ~MovingAverage() {
      delete[] values;
    }

    int add(int value) {
      total -= values[index];
      values[index] = value;
      total += value;
      index = (index + 1) % size;
      if (count < size) count++;
      return total / count;
    }
};

// The task depends on the members target & getDirection(), which is why we start it when any of them change
void MotorController::task() {
  // Get a stable battery level
  auto noloadBatt = battery->getValue();

  if (noloadBatt < 3000) {
    ESP_LOGW(TAG, "MotorController: noloadBatt %f too low, stop", noloadBatt / 1000.0);
    target = 50;
    current = 50;
    return;
  }

  unsigned int startTime = 0;
  lastStatus = "start";

  int batt = noloadBatt;
  int stallStart = 0;
  int now = 0;
  int minRatio = 0, maxRatio = 0;
  int currentRatio = 0;

  MovingAverage battAvg(6);

  while (true) {
    if (target == current)
      break;
    const auto dir = target > current ? 1 : -1;
    now = millis();
    const auto runTime = startTime ? now - startTime : 0;
    if (dir != getDirection()) {
      setDirection(0);
      delay(100);
      setDirection(dir);
      currentRatio /= 2;
      startTime = now;
      lastStatus = "seeking";
      stallStart = 0;
    }

    // This should really be based on the current position when we started, not the whole time=out period
    // We should probably keep a running avereage of the actual typical run-time too
    if (dir < 0) {
      current = 99 - (runTime * 98 / maxMotorTime);
    } else {
      current = 1 + (runTime * 98 / maxMotorTime);
    }

    int spotBatt = battery->getValue();
    batt = battAvg.add(spotBatt);

    const auto shuntMilliVolts = noloadBatt > batt ? noloadBatt - batt : 1;
    currentRatio = shuntMilliVolts * 3000 / batt;

    if (runTime > maxMotorTime) {
      // Motor has timed-out
      lastStatus = "timed-out";
      target = current;
      break;
    }

    if (runTime <= minMotorTime) {
      if (currentRatio > trackRatio) {
        trackRatio = currentRatio;
      }
    } else {
      minRatio = (peakLoPercent * trackRatio) / 100 - 1;
      if (minRatio < 0) minRatio = 0;
      maxRatio = (peakHiPercent * trackRatio) / 100 + 1;
      if (currentRatio >= minRatio && currentRatio <= maxRatio) {
        // Within range
        if (!stallStart)
          stallStart = now;
        else if (now - stallStart > params.stall_ms) {
          // Motor has stalled
          if (runTime <= minMotorTime + 250) {
            lastStatus = "stuck";
            // Reduce ratio since stuck motors typically draw excess current
            currentRatio /= 2;
            break;
          } else {
            lastStatus = "stalled";
            current = target;
            break;
          }
        }
      } else if (currentRatio < minRatio) {
        // Fall in current
        stallStart = 0;
      } else if (currentRatio > maxRatio) {
        // Rise in current
        stallStart = 0;
        trackRatio = currentRatio;
      } else {
        ESP_LOGE(TAG, "MotorController: logic error");
      }
    }

    // Longging only
    if (true) {
      memset(bar, ' ', sizeof(bar) - 1);
      charChart((noloadBatt - spotBatt) / 3, '=');

      charChart(minRatio / 3, '<');
      charChart(maxRatio / 3, '>');
      charChart(currentRatio / 3, '|');
      bar[sizeof(bar) - 1] = 0;

      ESP_LOGI(TAG, "%s %3d", bar, stallStart ? (now - stallStart) : -1);

      ESP_LOGI(TAG, "MotorController %10s: dir: %2d, noloadBatt %4dmV, batt %4dmV, ΔV %3dmV, Vpeak %3dmV, target %3d, current %3d, runTime: %5lu, timeout: %5u, stallStart %4d    %s",
              lastStatus,
              getDirection(),
              noloadBatt, batt,
              shuntMilliVolts,
              trackRatio,
              target, current,
              runTime, maxMotorTime,
              now - stallStart,
              strcmp(lastStatus, "seeking") ? "" : "\x1b[1A\r");
    }
  }
  // Back-off
  const auto reverse = -getDirection();
  if (reverse) {
    setDirection(0);
    delay(100);
    setDirection(reverse);
    delay(params.backoff_ms);
    // Update trackRatio to be at the lower range of the average of the initial and current values
    trackRatio = ((trackRatio + currentRatio) * peakLoPercent) / 200;
  }
  setDirection(0);
  ESP_LOGI(TAG, "MotorController %10s: dir: %2d, noloadBatt %4dmV, batt %4dmV, ΔV %3dmV, Vpeak %3dmV, target %3d, current %3d, runTime: %5lu, timeout: %5u, stallStart %4d    ",
            lastStatus,
            getDirection(),
            noloadBatt, batt,
            noloadBatt - batt,
            trackRatio,
            target, current,
            now - startTime, maxMotorTime,
            stallStart);
}