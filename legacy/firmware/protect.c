/*
 * This file is part of the Trezor project, https://trezor.io/
 *
 * Copyright (C) 2014 Pavol Rusnak <stick@satoshilabs.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "protect.h"
#include "buttons.h"
#include "config.h"
#include "debug.h"
#include "fsm.h"
#include "gettext.h"
#include "layout2.h"
#include "memory.h"
#include "memzero.h"
#include "messages.h"
#include "messages.pb.h"
#include "oled.h"
#include "pinmatrix.h"
#include "rng.h"
#include "usb.h"
#include "util.h"

#define MAX_WRONG_PINS 15

#define BACKSPACE '\x08'
#define SPACE '\x09'
#define DONE '\x06'

#define INPUT_DONE -1

#define CHAR_AND_SPACE_WIDTH (5 + 1)

#define NUM_PASSPHRASE_LINES 3
#define PASSPHRASE_WIDTH \
  ((MAX_PASSPHRASE_LEN + 1) / NUM_PASSPHRASE_LINES * CHAR_AND_SPACE_WIDTH)

#define PIN_WIDTH (MAX_PIN_LEN * CHAR_AND_SPACE_WIDTH)

#define CARET_SHOW 80
#define CARET_CYCLE (CARET_SHOW * 2)

bool protectAbortedByCancel = false;
bool protectAbortedByInitialize = false;

void buttonCheckRepeat(bool *yes, bool *no, bool *confirm) {
  *yes = false;
  *no = false;
  *confirm = false;

  const int Threshold0 = 20;
  const int Thresholds[] = {Threshold0, 80, 20, 18, 16, 14, 12, 10, 8, 6, 4};
  const int MaxThresholdLevel = sizeof(Thresholds) / sizeof(Thresholds[0]) - 1;

  static int yesthreshold = Threshold0;
  static int nothreshold = Threshold0;

  static int yeslevel = 0;
  static int nolevel = 0;

  static bool both = false;

  usbSleep(5);
  buttonUpdate();

  if (both) {
    if (!button.YesDown && !button.NoDown) {
      both = false;
      yeslevel = 0;
      nolevel = 0;
      yesthreshold = Thresholds[0];
      nothreshold = Thresholds[0];
    }
  } else if ((button.YesDown && button.NoDown) ||
             (button.YesUp && button.NoDown) ||
             (button.YesDown && button.NoUp) || (button.YesUp && button.NoUp)) {
    if (!yeslevel && !nolevel) {
      both = true;
      *confirm = true;
    }
  } else {
    if (button.YesUp) {
      if (!yeslevel) *yes = true;
      yeslevel = 0;
      yesthreshold = Thresholds[0];
    } else if (button.YesDown >= yesthreshold) {
      if (yeslevel < MaxThresholdLevel) yeslevel++;
      yesthreshold += Thresholds[yeslevel];
      *yes = true;
    }
    if (button.NoUp) {
      if (!nolevel) *no = true;
      nolevel = 0;
      nothreshold = Thresholds[0];
    } else if (button.NoDown >= nothreshold) {
      if (nolevel < MaxThresholdLevel) nolevel++;
      nothreshold += Thresholds[nolevel];
      *no = true;
    }
  }
}

void buttonWaitForYesUp(void) {
  buttonUpdate();

  for (;;) {
    usbSleep(5);
    buttonUpdate();
    if (button.YesUp) break;
  }
}

void buttonWaitForIdle(void) {
  buttonUpdate();

  for (;;) {
    usbSleep(5);
    buttonUpdate();
    if (!button.YesDown && !button.YesUp && !button.NoDown && !button.NoUp)
      break;
  }
}

void requestOnDeviceTextInput(void) {
  layoutDialog(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
               _("Do you like to use"), _("on-device text input?"), NULL, NULL,
               NULL, NULL);

  buttonUpdate();

  for (;;) {
    usbSleep(5);
    buttonUpdate();
    if (button.YesUp || button.NoUp) break;
  }

  layoutSwipe();

  session_setUseOnDeviceTextInput(button.YesUp);
}

int findCharIndex(const char entries[], char needle, int numtotal,
                  int startindex, bool forward) {
  int index = startindex;
  int step = forward ? 1 : -1;
  while (index >= 0 && index < numtotal) {
    if (entries[index] == needle) return index;
    index += step;
  }
  return startindex;
}

int inputTextScroll(char *text, int *textcharindex, int maxtextcharindex,
                    const char entries[], int textwidth, int entryindex,
                    int numtotal, int numscreen, int horizontalpadding,
                    const int groups[], int numgroup, int numskipingroups,
                    int *caret) {
  for (;; *caret = (*caret + 1) % CARET_CYCLE) {
    bool yes, no, confirm;
    buttonCheckRepeat(&yes, &no, &confirm);

    if (confirm) {
      buttonWaitForIdle();

      if (entries[entryindex] == BACKSPACE) {
        if (*textcharindex > 0) {
          --(*textcharindex);
          text[*textcharindex] = 0;
        }
      } else if (entries[entryindex] == DONE) {
        return INPUT_DONE;
      } else {
        if (*textcharindex < maxtextcharindex) {
          text[*textcharindex] = entries[entryindex];
          ++(*textcharindex);
        }
        return entryindex;
      }

      entryindex = random32() % numtotal;
    } else {
      if (yes) entryindex = (entryindex + 1) % numtotal;
      if (no) entryindex = (entryindex - 1 + numtotal) % numtotal;
    }

    layoutScrollInput(text, textwidth, numtotal, numscreen, entryindex, entries,
                      horizontalpadding, numgroup, groups, numskipingroups,
                      *caret < CARET_SHOW);
  }
}

bool protectButton(ButtonRequestType type, bool confirm_only) {
  ButtonRequest resp = {0};
  bool result = false;
  bool acked = false;
#if DEBUG_LINK
  bool debug_decided = false;
#endif

  memzero(&resp, sizeof(ButtonRequest));
  resp.has_code = true;
  resp.code = type;
  usbTiny(1);
  buttonUpdate();  // Clear button state
  msg_write(MessageType_MessageType_ButtonRequest, &resp);

  for (;;) {
    usbPoll();

    // check for ButtonAck
    if (msg_tiny_id == MessageType_MessageType_ButtonAck) {
      msg_tiny_id = 0xFFFF;
      acked = true;
    }

    // button acked - check buttons
    if (acked) {
      usbSleep(5);
      buttonUpdate();
      if (button.YesUp) {
        result = true;
        break;
      }
      if (!confirm_only && button.NoUp) {
        result = false;
        break;
      }
    }

    // check for Cancel / Initialize
    protectAbortedByCancel = (msg_tiny_id == MessageType_MessageType_Cancel);
    protectAbortedByInitialize =
        (msg_tiny_id == MessageType_MessageType_Initialize);
    if (protectAbortedByCancel || protectAbortedByInitialize) {
      msg_tiny_id = 0xFFFF;
      result = false;
      break;
    }

#if DEBUG_LINK
    // check DebugLink
    if (msg_tiny_id == MessageType_MessageType_DebugLinkDecision) {
      msg_tiny_id = 0xFFFF;
      DebugLinkDecision *dld = (DebugLinkDecision *)msg_tiny;
      result = dld->yes_no;
      debug_decided = true;
    }

    if (acked && debug_decided) {
      break;
    }

    if (msg_tiny_id == MessageType_MessageType_DebugLinkGetState) {
      msg_tiny_id = 0xFFFF;
      fsm_msgDebugLinkGetState((DebugLinkGetState *)msg_tiny);
    }
#endif
  }

  usbTiny(0);

  return result;
}

bool requestPinComputer(PinMatrixRequestType type, const char *text,
                        char pin[]) {
  PinMatrixRequest resp = {0};
  memzero(&resp, sizeof(PinMatrixRequest));
  resp.has_type = true;
  resp.type = type;
  usbTiny(1);
  msg_write(MessageType_MessageType_PinMatrixRequest, &resp);
  pinmatrix_start(text);
  for (;;) {
    usbPoll();
    if (msg_tiny_id == MessageType_MessageType_PinMatrixAck) {
      msg_tiny_id = 0xFFFF;
      PinMatrixAck *pma = (PinMatrixAck *)msg_tiny;
      pinmatrix_done(pma->pin);  // convert via pinmatrix
      usbTiny(0);
      strlcpy(pin, pma->pin, sizeof(pma->pin));
      return true;
    }
    // check for Cancel / Initialize
    protectAbortedByCancel = (msg_tiny_id == MessageType_MessageType_Cancel);
    protectAbortedByInitialize =
        (msg_tiny_id == MessageType_MessageType_Initialize);
    if (protectAbortedByCancel || protectAbortedByInitialize) {
      pinmatrix_done(0);
      msg_tiny_id = 0xFFFF;
      usbTiny(0);
      pin[0] = 0;
      return false;
    }
#if DEBUG_LINK
    if (msg_tiny_id == MessageType_MessageType_DebugLinkGetState) {
      msg_tiny_id = 0xFFFF;
      fsm_msgDebugLinkGetState((DebugLinkGetState *)msg_tiny);
    }
#endif
  }
}

secbool protectPinUiCallback(uint32_t wait, uint32_t progress,
                             const char *message) {
  // Convert wait to secstr string.
  char secstrbuf[] = _("________0 seconds");
  char *secstr = secstrbuf + 9;
  uint32_t secs = wait;
  do {
    secstr--;
    *secstr = (secs % 10) + '0';
    secs /= 10;
  } while (secs > 0 && secstr >= secstrbuf);
  if (wait == 1) {
    // Change "seconds" to "second".
    secstrbuf[16] = 0;
  }
  oledClear();
  oledDrawStringCenter(OLED_WIDTH / 2, 0 * 9, message, FONT_STANDARD);
  oledDrawStringCenter(OLED_WIDTH / 2, 2 * 9, _("Please wait"), FONT_STANDARD);
  oledDrawStringCenter(OLED_WIDTH / 2, 3 * 9, secstr, FONT_STANDARD);
  oledDrawStringCenter(OLED_WIDTH / 2, 4 * 9, _("to continue ..."),
                       FONT_STANDARD);
  // progressbar
  oledFrame(0, OLED_HEIGHT - 8, OLED_WIDTH - 1, OLED_HEIGHT - 1);
  oledBox(1, OLED_HEIGHT - 7, OLED_WIDTH - 2, OLED_HEIGHT - 2, 0);
  progress = progress * (OLED_WIDTH - 4) / 1000;
  if (progress > OLED_WIDTH - 4) {
    progress = OLED_WIDTH - 4;
  }
  oledBox(2, OLED_HEIGHT - 6, 1 + progress, OLED_HEIGHT - 3, 1);
  oledRefresh();
  // Check for Cancel / Initialize.
  protectAbortedByCancel = (msg_tiny_id == MessageType_MessageType_Cancel);
  protectAbortedByInitialize =
      (msg_tiny_id == MessageType_MessageType_Initialize);
  if (protectAbortedByCancel || protectAbortedByInitialize) {
    msg_tiny_id = 0xFFFF;
    usbTiny(0);
    fsm_sendFailure(FailureType_Failure_PinCancelled, NULL);
    return sectrue;
  }

  return secfalse;
}

void userEnterPin(char pin[]) {
  const char Entries[] = {
      '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', BACKSPACE, DONE,
  };
  const int EntriesGroups[] = {0, 12};

  int numentries = sizeof(Entries) / sizeof(Entries[0]);
  int numentriesgroups = sizeof(EntriesGroups) / sizeof(EntriesGroups[0]);

  usbSleep(5);
  buttonUpdate();

  int pincharindex = strlen(pin);
  int caret = 0;

  for (;;) {
    int entryindex = random32() % numentries;
    if (pincharindex >= MAX_PIN_LEN)
      entryindex = findCharIndex(Entries, DONE, numentries, entryindex, true);
    entryindex = inputTextScroll(pin, &pincharindex, MAX_PIN_LEN, Entries,
                                 PIN_WIDTH, entryindex, numentries, 9, 9,
                                 EntriesGroups, numentriesgroups, 2, &caret);
    if (entryindex == INPUT_DONE && pincharindex > 0) return;
  }
}

bool userCheckPin(char pin[]) {
  layoutCheckInput(pin, PIN_WIDTH, true, true, "Confirm PIN:", NULL, NULL);

  buttonUpdate();

  for (;;) {
    usbSleep(5);
    buttonUpdate();
    if (button.YesUp) return true;
    if (button.NoUp) return false;
  }
}

void requestPinDevice(const char *line1, const char *line2, const char *line3,
                      char pin[]) {
  buttonUpdate();

  layoutDialog(NULL, NULL, _("Next"), NULL, line1, line2, line3, NULL, NULL,
               NULL);
  buttonWaitForYesUp();
  layoutSwipe();

  for (;;) {
    userEnterPin(pin);
    layoutSwipe();

    if (userCheckPin(pin)) break;

    oledSwipeRight();
  }
}

bool protectPin(bool use_cached) {
  if (use_cached && session_isUnlocked()) {
    return true;
  }

  static CONFIDENTIAL char pin[MAX_PIN_LEN + 1];

  if (config_hasPin()) {
    memzero(pin, sizeof(pin));

    if (!session_isUseOnDeviceTextInputCached()) {
      requestOnDeviceTextInput();
    }

    if (session_isUseOnDeviceTextInput()) {
      requestPinDevice("Please enter current PIN", "on the next screen.", NULL,
                       pin);
    } else {
      requestPinComputer(PinMatrixRequestType_PinMatrixRequestType_Current,
                         _("Please enter current PIN:"), pin);
      if (!pin[0]) {
        memzero(pin, sizeof(pin));
        fsm_sendFailure(FailureType_Failure_PinCancelled, NULL);
        return false;
      }
    }
  }

  bool ret = config_unlock(pin);
  memzero(pin, sizeof(pin));
  if (!ret) {
    fsm_sendFailure(FailureType_Failure_PinInvalid, NULL);
  }
  return ret;
}

bool protectChangePin(bool removal) {
  static CONFIDENTIAL char old_pin[MAX_PIN_LEN + 1] = "";
  static CONFIDENTIAL char new_pin[MAX_PIN_LEN + 1] = "";
  static CONFIDENTIAL char pin[MAX_PIN_LEN + 1];

  if (!session_isUseOnDeviceTextInputCached()) {
    requestOnDeviceTextInput();
  }

  if (config_hasPin()) {
    if (session_isUseOnDeviceTextInput()) {
      memzero(pin, sizeof(pin));
      requestPinDevice("Please enter current PIN", "on the next screen.", NULL,
                       pin);
    } else {
      requestPinComputer(PinMatrixRequestType_PinMatrixRequestType_Current,
                         _("Please enter current PIN:"), pin);
      if (!pin[0]) {
        fsm_sendFailure(FailureType_Failure_PinCancelled, NULL);
        return false;
      }
    }

    // If removing, defer the check to config_changePin().
    if (!removal) {
      usbTiny(1);
      bool ret = config_unlock(pin);
      usbTiny(0);
      if (ret == false) {
        fsm_sendFailure(FailureType_Failure_PinInvalid, NULL);
        return false;
      }
    }

    strlcpy(old_pin, pin, sizeof(old_pin));
  } else {
    memzero(old_pin, sizeof(old_pin));
  }

  if (!removal) {
    if (session_isUseOnDeviceTextInput()) {
      memzero(pin, sizeof(pin));
      requestPinDevice("Please enter new PIN", "on the next screen.", NULL,
                       pin);
    } else {
      requestPinComputer(PinMatrixRequestType_PinMatrixRequestType_NewFirst,
                         _("Please enter new PIN:"), pin);
      if (!pin[0]) {
        memzero(old_pin, sizeof(old_pin));
        fsm_sendFailure(FailureType_Failure_PinCancelled, NULL);
        return false;
      }
    }
    strlcpy(new_pin, pin, sizeof(new_pin));

    if (session_isUseOnDeviceTextInput()) {
      memzero(pin, sizeof(pin));
      requestPinDevice("Please re-enter new PIN", "on the next screen.", NULL,
                       pin);
    } else {
      requestPinComputer(PinMatrixRequestType_PinMatrixRequestType_NewSecond,
                         _("Please re-enter new PIN:"), pin);
      if (!pin[0]) {
        memzero(old_pin, sizeof(old_pin));
        memzero(new_pin, sizeof(new_pin));
        fsm_sendFailure(FailureType_Failure_PinCancelled, NULL);
        return false;
      }
    }

    if (strncmp(new_pin, pin, sizeof(new_pin)) != 0) {
      memzero(old_pin, sizeof(old_pin));
      memzero(new_pin, sizeof(new_pin));
      fsm_sendFailure(FailureType_Failure_PinMismatch, NULL);
      return false;
    }
  } else {
    memzero(new_pin, sizeof(new_pin));
  }

  bool ret = config_changePin(old_pin, new_pin);
  memzero(old_pin, sizeof(old_pin));
  memzero(new_pin, sizeof(new_pin));
  memzero(pin, sizeof(pin));
  if (ret == false) {
    fsm_sendFailure(FailureType_Failure_PinInvalid, NULL);
  }
  return ret;
}

bool protectPassphraseComputer(void) {
  PassphraseRequest resp = {0};
  memzero(&resp, sizeof(PassphraseRequest));
  usbTiny(1);
  msg_write(MessageType_MessageType_PassphraseRequest, &resp);

  layoutDialogSwipe(&bmp_icon_info, NULL, NULL, NULL, _("Please enter your"),
                    _("passphrase using"), _("the computer's"), _("keyboard."),
                    NULL, NULL);

  bool result;
  for (;;) {
    usbPoll();
    // TODO: correctly process PassphraseAck with state field set (mismatch =>
    // Failure)
    if (msg_tiny_id == MessageType_MessageType_PassphraseAck) {
      msg_tiny_id = 0xFFFF;
      PassphraseAck *ppa = (PassphraseAck *)msg_tiny;
      session_cachePassphrase(ppa->has_passphrase ? ppa->passphrase : "");
      result = true;
      break;
    }
    // check for Cancel / Initialize
    protectAbortedByCancel = (msg_tiny_id == MessageType_MessageType_Cancel);
    protectAbortedByInitialize =
        (msg_tiny_id == MessageType_MessageType_Initialize);
    if (protectAbortedByCancel || protectAbortedByInitialize) {
      msg_tiny_id = 0xFFFF;
      result = false;
      break;
    }
  }
  usbTiny(0);
  layoutHome();
  return result;
}

void userEnterPassphrase(char *passphrase) {
  const char Entries[] = {
      'a',       'b',       'c',       'd',       'e',       'f',    'g',
      'h',       'i',       BACKSPACE, DONE,      'j',       'k',    'l',
      'm',       'n',       'o',       'p',       'q',       'r',    BACKSPACE,
      DONE,      's',       't',       'u',       'v',       'w',    'x',
      'y',       'z',       SPACE,     BACKSPACE, DONE,      'A',    'B',
      'C',       'D',       'E',       'F',       'G',       'H',    'I',
      BACKSPACE, DONE,      'J',       'K',       'L',       'M',    'N',
      'O',       'P',       'Q',       'R',       BACKSPACE, DONE,   'S',
      'T',       'U',       'V',       'W',       'X',       'Y',    'Z',
      SPACE,     BACKSPACE, DONE,      '1',       '2',       '3',    '4',
      '5',       '6',       '7',       '8',       '9',       '0',    BACKSPACE,
      DONE,      '!',       '@',       '#',       '$',       '\x25', '^',
      '&',       '*',       '(',       ')',       BACKSPACE, DONE,   '`',
      '-',       '=',       '[',       ']',       '\\',      ';',    '\'',
      ',',       '.',       '/',       BACKSPACE, DONE,      '~',    '_',
      '+',       '{',       '}',       '|',       ':',       '\'',   '<',
      '>',       '?',       BACKSPACE, DONE};
  const int EntriesGroups[] = {0, 11, 22, 33, 44, 55, 66, 78, 90, 103, 116};

  int numentries = sizeof(Entries) / sizeof(Entries[0]);
  int numentriesgroups = sizeof(EntriesGroups) / sizeof(EntriesGroups[0]);

  usbSleep(5);
  buttonUpdate();

  int passphrasecharindex = strlen(passphrase);
  int caret = 0;

  for (;;) {
    int entryindex = random32() % numentries;
    if (passphrasecharindex >= MAX_PASSPHRASE_LEN)
      entryindex = findCharIndex(Entries, DONE, numentries, entryindex,
                                 entryindex < numentries / 2);
    entryindex =
        inputTextScroll(passphrase, &passphrasecharindex, MAX_PASSPHRASE_LEN,
                        Entries, PASSPHRASE_WIDTH, entryindex, numentries, 9, 9,
                        EntriesGroups, numentriesgroups, 2, &caret);
    if (entryindex == INPUT_DONE) return;
  }
}

bool userCheckPassphrase(const char *passphrase, bool enable_edit,
                         bool enable_done) {
  layoutCheckInput(passphrase, PASSPHRASE_WIDTH, enable_edit, enable_done,
                   "Confirm passphrase:", "Passphrases mismatched:",
                   "Passphrase confirmed:");

  buttonUpdate();

  for (;;) {
    usbSleep(5);
    buttonUpdate();
    if (enable_done && button.YesUp) return true;
    if (enable_edit && button.NoUp) return false;
  }
}

bool protectPassphraseDevice(void) {
  static CONFIDENTIAL char passphrase[MAX_PASSPHRASE_LEN + 1];

  memzero(passphrase, sizeof(passphrase));
  buttonUpdate();

  layoutDialog(NULL, NULL, _("Next"), NULL, _("You are about to enter"),
               _("the passphrase."), _("Select how many times"),
               _("you'd like to do it."), NULL, NULL);
  buttonWaitForYesUp();
  layoutSwipe();

  layoutDialog(NULL, _("Twice"), _("Once"), NULL,
               _("If you are creating a new"), _("wallet, it is advised"),
               _("that you select Twice."), NULL, NULL, NULL);
  for (;;) {
    usbSleep(5);
    buttonUpdate();
    if (button.YesUp || button.NoUp) break;
  }
  layoutSwipe();

  bool twice = button.NoUp;

  layoutDialog(NULL, NULL, _("Next"), NULL, _("Enter the passphrase"),
               _("on the next screen."), _("- Single button: scroll."),
               _("- Hold: auto-scroll."), _("- Both buttons: confirm."), NULL);
  buttonWaitForYesUp();
  layoutSwipe();

  for (;;) {
    userEnterPassphrase(passphrase);

    if (userCheckPassphrase(passphrase, true, true)) break;

    oledSwipeRight();
  }

  if (twice) {
    static CONFIDENTIAL char passphrase2[MAX_PASSPHRASE_LEN + 1];

    memzero(passphrase2, sizeof(passphrase2));

    layoutSwipe();
    layoutDialog(NULL, NULL, _("Next"), NULL, _("Re-enter the passphrase."),
                 NULL, NULL, NULL, NULL, NULL);
    buttonWaitForYesUp();
    layoutSwipe();

    for (;;) {
      userEnterPassphrase(passphrase2);

      if (strcmp(passphrase, passphrase2) == 0) break;

      userCheckPassphrase(passphrase2, true, false);
      oledSwipeRight();
    }

    memzero(passphrase2, sizeof(passphrase2));
  }

  userCheckPassphrase(passphrase, false, true);

  for (int i = 0; i < MAX_PASSPHRASE_LEN + 1 && passphrase[i]; ++i)
    if (passphrase[i] == SPACE) passphrase[i] = ' ';

  session_cachePassphrase(passphrase);
  memzero(passphrase, sizeof(passphrase));

  layoutHome();

  return true;
}

bool protectPassphrase(void) {
  bool passphrase_protection = false;
  config_getPassphraseProtection(&passphrase_protection);
  if (!passphrase_protection || session_isPassphraseCached()) {
    return true;
  }

  bool result;
  if (!session_isUseOnDeviceTextInputCached()) {
    requestOnDeviceTextInput();
  }
  if (session_isUseOnDeviceTextInput())
    result = protectPassphraseDevice();
  else
    result = protectPassphraseComputer();
  return result;
}
