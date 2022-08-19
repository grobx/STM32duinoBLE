/*
  This file is part of the STM32duinoBLE library.
  Copyright (c) 2022 STMicroelectronics Srl. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _HCI_VENDOR_CLASS_H_
#define _HCI_VENDOR_CLASS_H_

#include "HCI.h"
#include "BLEChip.h"

#include <limits>

#define HCI_EVENT_EXT_PKT   0x82
// #define OGF_VENDOR_SPECIFIC 0x3F
#define EVT_VENDOR_SPECIFIC 0xFF

#define VS_EVT_BLUE_INITIALIZE 0x0001

#define VS_OPCODE_GATT_INIT  0xFD01
#define VS_OPCODE_GAP_INIT   0xFC8A
#define VS_OPCODE_WRITE_CONF 0xFC0C
#define VS_OPCODE_READ_CONF  0xFC0D

class HCIVendorClass: public HCIClass {
  enum {
    STARTED,
    RUNNING
  } status = STARTED;

public:
  virtual int reset()
  {
    int ret = -1;

    // We're sure _HCITransport is a HCISpiTransportClass*
    // should it change, we will need an HCIVendorTransportInterface
    const BLEChip_t ble_chip = static_cast<HCISpiTransportClass*>(_HCITransport)->ble_chip();

    if (ble_chip == BLUENRG_LP) {
      // if it's already running, we should hci reset it
      if (status == RUNNING) {
        status = STARTED;
        ret = HCIClass::reset();
        if (ret < 0) return -1;
      }

      // wait for blue initialize
      poll();
      if (status != RUNNING) return -2;

      // enable link-layer only
      uint8_t ll_only_params[] = {0x2C, 0x01, 0x01};
      ret = sendCommand(VS_OPCODE_WRITE_CONF, sizeof(ll_only_params), &ll_only_params);
      if (ret < 0) return -3;

      // GATT init
      ret = sendCommand(VS_OPCODE_GATT_INIT);
      if (ret < 0) return -4;

      // GAP init
      uint8_t gap_init_params[] = {0x0F, 0x00, 0x00, 0x00};
      ret = sendCommand(VS_OPCODE_GAP_INIT, sizeof(gap_init_params), &gap_init_params);
      if (ret < 0) return -5;

      // READ random address
      uint8_t read_raddr_params[] = {0x80};
      ret = sendCommand(VS_OPCODE_READ_CONF, sizeof(read_raddr_params), &read_raddr_params);
      if (ret < 0) return -6;

      // store random address
      uint8_t random_address[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      memcpy(random_address, &_cmdResponse[1], 6);

      // HCI reset
      status = STARTED;
      ret = HCIClass::reset();
      if (ret < 0) return -7;

      // wait for blue initialize
      poll();
      if (status != RUNNING) return -8;

      // enable link-layer only
      ret = sendCommand(VS_OPCODE_WRITE_CONF, sizeof(ll_only_params), &ll_only_params);
      if (ret < 0) return -9;

      // set random address
      ret = sendCommand(OGF_LE_CTL << 10 | OCF_LE_SET_RANDOM_ADDRESS, sizeof(random_address), random_address);
      if (ret < 0) return -10;
    } else {
      ret = -11;
    }

    return ret;
  }

  virtual void poll(unsigned long timeout = 0)
  {
    if (timeout) {
      _HCITransport->wait(timeout);
    }

    // If it's not an extended event, let HCIClass handle it
    if (_HCITransport->available()) {
      byte b = _HCITransport->peek();

      if (b != HCI_EVENT_EXT_PKT) {
        return HCIClass::poll(0);
      }
    }

    while (_HCITransport->available()) {
      byte b = _HCITransport->read();

      _recvBuffer[_recvIndex++] = b;

      // if (_recvBuffer[0] == HCI_EVENT_EXT_PKT) { // _recvBuffer[0] is always HCI_EVENT_EXT_PKT
        if (_recvIndex > 4 && _recvIndex >= (4 + (_recvBuffer[2] + (_recvBuffer[3] << 8)))) {
          if (_debug) {
            dumpPkt("HCI VS EVENT RX <- ", _recvIndex, _recvBuffer);
          }

          int pktLen = _recvIndex - 1;
          _recvIndex = 0;

          handleExtEventPkt(pktLen, &_recvBuffer[1]);
        }
      // }
    }
  }

protected:
  virtual void handleExtEventPkt(uint16_t plen, uint8_t pdata[])
  {
    constexpr uint8_t uint8_max = std::numeric_limits<uint8_t>::max();

    struct __attribute__ ((packed)) HCIExtEventHdr {
      uint8_t evt;
      uint16_t plen;
    } *eventHdr = (HCIExtEventHdr*)pdata;

    if (eventHdr->evt == EVT_VENDOR_SPECIFIC) {
      uint16_t ecode = pdata[sizeof(HCIExtEventHdr)];
      if (ecode == VS_EVT_BLUE_INITIALIZE) {
        struct __attribute__ ((packed)) BlueInitialize {
          uint8_t reason;
        } *blueInitialize = (BlueInitialize*)&pdata[sizeof(HCIExtEventHdr) + 2];
        if (blueInitialize->reason == 0x01) { // Firmware started properly
          status = RUNNING;
        }
      }
    } else if (eventHdr->plen <= uint8_max) {
      // if the plen is less than 8b, we can reuse handleEventPkt
      HCIClass::handleEventPkt(plen, pdata);
    } else {
      // FIXME: we cant just pass it to handleEventPkt, cause it has more than uint8_max bytes
      if (_debug) {
        _debug->printf("Extended event with more than %dB not yet implemented!\r\n", uint8_max);
      }
    }
  }
};

#endif /* _HCI_VENDOR_CLASS_H_ */
