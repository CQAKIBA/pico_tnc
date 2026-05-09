/*
Copyright (c) 2021, Kazuhisa Yokota, JN1DFF
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright notice, 
  this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, 
  this list of conditions and the following disclaimer in the documentation 
  and/or other materials provided with the distribution.
* Neither the name of the <organization> nor the names of its contributors 
  may be used to endorse or promote products derived from this software 
  without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*
Modifications:
Copyright (c) 2026 Daisuke JA1UMW / CQAKIBA.TOKYO
Released under the MIT License.
See LICENSE and LICENSE-3RD-PARTY for details.
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "class/cdc/cdc_device.h"
#include "pico/util/queue.h"

#include "usb_input.h"
#include "tty.h"

#define USB_RX_QUEUE_SIZE 512

static queue_t usb_rx_queue;
static bool usb_rx_queue_ready = false;
static volatile uint32_t usb_rx_overrun_count = 0;

void usb_input_init(void)
{
    queue_init(&usb_rx_queue, sizeof(uint8_t), USB_RX_QUEUE_SIZE);
    usb_rx_queue_ready = true;
}

uint32_t usb_input_overrun_count(void)
{
    return usb_rx_overrun_count;
}

bool usb_read_char_nonblocking(uint8_t *ch)
{
    if (!usb_rx_queue_ready || !ch) return false;
    return queue_try_remove(&usb_rx_queue, ch);
}

void usb_input(void)
{
    uint8_t ch;

    if (!usb_rx_queue_ready) return;

    while (queue_try_remove(&usb_rx_queue, &ch)) {
        tty_input(&tty[TTY_USB], ch);
    }
}

// TinyUSB CDC RX callback.
// Keep this callback intentionally small: drain TinyUSB's RX FIFO into a local
// queue only. Command parsing, echo output, flash writes, and any other heavy
// work must run later from the main loop via usb_input().
void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;

    if (!usb_rx_queue_ready) {
        // If this fires before initialization, drain and drop data rather than
        // leaving the CDC OUT endpoint backed up.
        while (tud_cdc_available()) {
            (void)tud_cdc_read_char();
            ++usb_rx_overrun_count;
        }
        return;
    }

    while (tud_cdc_available()) {
        uint8_t ch = (uint8_t)tud_cdc_read_char();

        if (!queue_try_add(&usb_rx_queue, &ch)) {
            // Queue full: drop the byte, but keep draining TinyUSB so the host
            // is not flow-controlled forever by a full device FIFO.
            ++usb_rx_overrun_count;
        }
    }
}
