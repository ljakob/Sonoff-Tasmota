/*
  xdrv_23_zigbee.ino - zigbee support for Tasmota

  Copyright (C) 2019  Theo Arends and Stephan Hadinger

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_ZIGBEE

#include <vector>
#include <map>

typedef struct Z_Device {
  uint16_t              shortaddr;      // unique key if not null, or unspecified if null
  uint64_t              longaddr;       // 0x00 means unspecified
  uint32_t              firstSeen;      // date when the device was first seen
  uint32_t              lastSeen;       // date when the device was last seen
  String                manufacturerId;
  String                modelId;
  String                friendlyName;
  std::vector<uint32_t> endpoints;      // encoded as high 16 bits is endpoint, low 16 bits is ProfileId
  std::vector<uint32_t> clusters_in;    // encoded as high 16 bits is endpoint, low 16 bits is cluster number
  std::vector<uint32_t> clusters_out;   // encoded as high 16 bits is endpoint, low 16 bits is cluster number
} Z_Device;

// All devices are stored in a Vector
// Invariants:
// - shortaddr is unique if not null
// - longaddr is unique if not null
// - shortaddr and longaddr cannot be both null
// - clusters_in and clusters_out containt only endpoints listed in endpoints
class Z_Devices {
public:
  Z_Devices() {};

  // Add new device, provide ShortAddr and optional longAddr
  // If it is already registered, update information, otherwise create the entry
  void updateDevice(uint16_t shortaddr, uint64_t longaddr = 0);

  // Add an endpoint to a device
  void addEndoint(uint16_t shortaddr, uint8_t endpoint);

  // Add endpoint profile
  void addEndointProfile(uint16_t shortaddr, uint8_t endpoint, uint16_t profileId);

  // Add cluster
  void addCluster(uint16_t shortaddr, uint8_t endpoint, uint16_t cluster, bool out);

  uint8_t findClusterEndpointIn(uint16_t shortaddr, uint16_t cluster);

  void setManufId(uint16_t shortaddr, const char * str);
  void setModelId(uint16_t shortaddr, const char * str);
  void setFriendlyNameId(uint16_t shortaddr, const char * str);

  // device just seen on the network, update the lastSeen field
  void updateLastSeen(uint16_t shortaddr);

  // Dump json
  String dump(uint8_t dump_mode) const;

private:
  std::vector<Z_Device> _devices = {};

  template < typename T>
  static bool findInVector(const std::vector<T>  & vecOfElements, const T  & element);

  template < typename T>
  static int32_t findEndpointInVector(const std::vector<T>  & vecOfElements, const T  & element);

  // find the first endpoint match for a cluster
  static int32_t findClusterEndpoint(const std::vector<uint32_t>  & vecOfElements, uint16_t element);

  Z_Device & getShortAddr(uint16_t shortaddr);   // find Device from shortAddr, creates it if does not exist
  Z_Device & getLongAddr(uint64_t longaddr);     // find Device from shortAddr, creates it if does not exist

  int32_t findShortAddr(uint16_t shortaddr);
  int32_t findLongAddr(uint64_t longaddr);

  void _updateLastSeen(Z_Device &device) {
    if (&device != nullptr) {
      device.lastSeen = Rtc.utc_time;
    }
  };

  // Create a new entry in the devices list - must be called if it is sure it does not already exist
  Z_Device & createDeviceEntry(uint16_t shortaddr, uint64_t longaddr = 0);
};

Z_Devices zigbee_devices = Z_Devices();

// https://thispointer.com/c-how-to-find-an-element-in-vector-and-get-its-index/
template < typename T>
bool Z_Devices::findInVector(const std::vector<T>  & vecOfElements, const T  & element) {
	// Find given element in vector
	auto it = std::find(vecOfElements.begin(), vecOfElements.end(), element);

	if (it != vecOfElements.end()) {
		return true;
	} else {
		return false;
	}
}

template < typename T>
int32_t Z_Devices::findEndpointInVector(const std::vector<T>  & vecOfElements, const T  & element) {
	// Find given element in vector

  int32_t found = 0;
  for (auto &elem : vecOfElements) {
    if ((elem >> 16) & 0xFF == element) { return found; }
    found++;
  }

  return -1;
}

//
// Find the first endpoint match for a cluster, whether in or out
// Clusters are stored in the format 0x00EECCCC (EE=endpoint, CCCC=cluster number)
// In:
//    _devices.clusters_in or _devices.clusters_out
//    cluster number looked for
// Out:
//    Index of found Endpoint_Cluster number, or -1 if not found
//
int32_t Z_Devices::findClusterEndpoint(const std::vector<uint32_t>  & vecOfElements, uint16_t cluster) {
  int32_t found = 0;
  for (auto &elem : vecOfElements) {
    if ((elem & 0xFFFF) == cluster) { return found; }
    found++;
  }
  return -1;
}

//
// Create a new Z_Device entry in _devices. Only to be called if you are sure that no
// entry with same shortaddr or longaddr exists.
//
Z_Device & Z_Devices::createDeviceEntry(uint16_t shortaddr, uint64_t longaddr) {
  if (!shortaddr && !longaddr) { return *(Z_Device*) nullptr; }      // it is not legal to create an enrty with both short/long addr null
  Z_Device device = { shortaddr, longaddr,
                      Rtc.utc_time, Rtc.utc_time,
                      String(),   // ManufId
                      String(),   // DeviceId
                      String(),   // FriendlyName
                      std::vector<uint32_t>(),
                      std::vector<uint32_t>(),
                      std::vector<uint32_t>() };
  _devices.push_back(device);
  return _devices.back();
}

//
// Scan all devices to find a corresponding shortaddr
// Looks info device.shortaddr entry
// In:
//    shortaddr (non null)
// Out:
//    index in _devices of entry, -1 if not found
//
int32_t Z_Devices::findShortAddr(uint16_t shortaddr) {
  if (!shortaddr) { return -1; }              // does not make sense to look for 0x0000 shortaddr (localhost)
  int32_t found = 0;
  if (shortaddr) {
    for (auto &elem : _devices) {
      if (elem.shortaddr == shortaddr) { return found; }
      found++;
    }
  }
  return -1;
}
//
// Scan all devices to find a corresponding longaddr
// Looks info device.longaddr entry
// In:
//    longaddr (non null)
// Out:
//    index in _devices of entry, -1 if not found
//
int32_t Z_Devices::findLongAddr(uint64_t longaddr) {
  if (!longaddr) { return -1; }
  int32_t found = 0;
  if (longaddr) {
    for (auto &elem : _devices) {
      if (elem.longaddr == longaddr) { return found; }
      found++;
    }
  }
  return -1;
}

//
// We have a seen a shortaddr on the network, get the corresponding
//
Z_Device & Z_Devices::getShortAddr(uint16_t shortaddr) {
  if (!shortaddr) { return *(Z_Device*) nullptr; }   // this is not legal
  int32_t found = findShortAddr(shortaddr);
  if (found >= 0) {
    return _devices[found];
  }
//Serial.printf("Device entry created for shortaddr = 0x%02X, found = %d\n", shortaddr, found);
  return createDeviceEntry(shortaddr, 0);
}

// find the Device object by its longaddr (unique key if not null)
Z_Device & Z_Devices::getLongAddr(uint64_t longaddr) {
  if (!longaddr) { return *(Z_Device*) nullptr; }
  int32_t found = findLongAddr(longaddr);
  if (found > 0) {
    return _devices[found];
  }
  return createDeviceEntry(0, longaddr);
}

//
// We have just seen a device on the network, update the info based on short/long addr
// In:
//    shortaddr
//    longaddr (both can't be null at the same time)
void Z_Devices::updateDevice(uint16_t shortaddr, uint64_t longaddr) {
  int32_t s_found = findShortAddr(shortaddr);       // is there already a shortaddr entry
  int32_t l_found = findLongAddr(longaddr);         // is there already a longaddr entry

  if ((s_found >= 0) && (l_found >= 0)) {           // both shortaddr and longaddr are already registered
    if (s_found == l_found) {
      updateLastSeen(shortaddr);                    // short/long addr match, all good
    } else {                                        // they don't match
      // the device with longaddr got a new shortaddr
      _devices[l_found].shortaddr = shortaddr;      // update the shortaddr corresponding to the longaddr
      // erase the previous shortaddr
      _devices.erase(_devices.begin() + s_found);
      updateLastSeen(shortaddr);
    }
  } else if (s_found >= 0) {
    // shortaddr already exists but longaddr not
    // add the longaddr to the entry
    _devices[s_found].longaddr = longaddr;
    updateLastSeen(shortaddr);
  } else if (l_found >= 0) {
    // longaddr entry exists, update shortaddr
    _devices[l_found].shortaddr = shortaddr;
  } else {
    // neither short/lonf addr are found.
    if (shortaddr || longaddr) {
      createDeviceEntry(shortaddr, longaddr);
    }
  }
}

//
// Add an endpoint to a shortaddr
//
void Z_Devices::addEndoint(uint16_t shortaddr, uint8_t endpoint) {
  if (!shortaddr) { return; }
  uint32_t ep_profile = (endpoint << 16);
  Z_Device &device = getShortAddr(shortaddr);
  if (&device == nullptr) { return; }                 // don't crash if not found
  _updateLastSeen(device);
  if (findEndpointInVector(device.endpoints, ep_profile) < 0) {
    device.endpoints.push_back(ep_profile);
  }
}

void Z_Devices::addEndointProfile(uint16_t shortaddr, uint8_t endpoint, uint16_t profileId) {
  if (!shortaddr) { return; }
  uint32_t ep_profile = (endpoint << 16) | profileId;
  Z_Device &device = getShortAddr(shortaddr);
  if (&device == nullptr) { return; }                 // don't crash if not found
  _updateLastSeen(device);
  int32_t found = findEndpointInVector(device.endpoints, ep_profile);
  if (found < 0) {
    device.endpoints.push_back(ep_profile);
  } else {
    device.endpoints[found] = ep_profile;
  }
}

void Z_Devices::addCluster(uint16_t shortaddr, uint8_t endpoint, uint16_t cluster, bool out) {
  if (!shortaddr) { return; }
  Z_Device & device = getShortAddr(shortaddr);
  if (&device == nullptr) { return; }                 // don't crash if not found
  _updateLastSeen(device);
  uint32_t ep_cluster = (endpoint << 16) | cluster;
  if (!out) {
    if (!findInVector(device.clusters_in, ep_cluster)) {
      device.clusters_in.push_back(ep_cluster);
    }
  } else { // out
    if (!findInVector(device.clusters_out, ep_cluster)) {
      device.clusters_out.push_back(ep_cluster);
    }
  }
}

// Look for the best endpoint match to send a command for a specific Cluster ID
// return 0x00 if none found
uint8_t Z_Devices::findClusterEndpointIn(uint16_t shortaddr, uint16_t cluster){
  int32_t short_found = findShortAddr(shortaddr);
  if (short_found < 0)  return 0;     // avoid creating an entry if the device was never seen
  Z_Device &device = getShortAddr(shortaddr);
  if (&device == nullptr) { return 0; }                 // don't crash if not found
  int32_t found = findClusterEndpoint(device.clusters_in, cluster);
  if (found >= 0) {
    return (device.clusters_in[found] >> 16) & 0xFF;
  } else {
    return 0;
  }
}


void Z_Devices::setManufId(uint16_t shortaddr, const char * str) {
  Z_Device & device = getShortAddr(shortaddr);
  if (&device == nullptr) { return; }                 // don't crash if not found
  _updateLastSeen(device);
  device.manufacturerId = str;
}
void Z_Devices::setModelId(uint16_t shortaddr, const char * str) {
  Z_Device & device = getShortAddr(shortaddr);
  if (&device == nullptr) { return; }                 // don't crash if not found
  _updateLastSeen(device);
  device.modelId = str;
}
void Z_Devices::setFriendlyNameId(uint16_t shortaddr, const char * str) {
  Z_Device & device = getShortAddr(shortaddr);
  if (&device == nullptr) { return; }                 // don't crash if not found
  _updateLastSeen(device);
  device.friendlyName = str;
}

// device just seen on the network, update the lastSeen field
void Z_Devices::updateLastSeen(uint16_t shortaddr) {
  Z_Device & device = getShortAddr(shortaddr);
  if (&device == nullptr) { return; }                 // don't crash if not found
  _updateLastSeen(device);
}

// Dump the internal memory of Zigbee devices
// Mode = 1: simple dump of devices addresses and names
// Mode = 2: Mode 1 + also dump the endpoints, profiles and clusters
String Z_Devices::dump(uint8_t dump_mode) const {
  DynamicJsonBuffer jsonBuffer;
  JsonArray& json = jsonBuffer.createArray();
  JsonArray& devices = json;
  //JsonArray& devices = json.createNestedArray(F("ZigbeeDevices"));

  for (std::vector<Z_Device>::const_iterator it = _devices.begin(); it != _devices.end(); ++it) {
    const Z_Device& device = *it;
    uint16_t shortaddr = device.shortaddr;
    char hex[20];

    JsonObject& dev = devices.createNestedObject();

    snprintf_P(hex, sizeof(hex), PSTR("0x%04X"), shortaddr);
    dev[F(D_JSON_ZIGBEE_DEVICE)] = hex;

    if (device.friendlyName.length() > 0) {
      dev[F(D_JSON_ZIGBEE_NAME)] = device.friendlyName;
    }

    if (1 == dump_mode) {
      Uint64toHex(device.longaddr, hex, 64);
      dev[F("IEEEAddr")] = hex;
      if (device.modelId.length() > 0) {
        dev[F(D_JSON_MODEL D_JSON_ID)] = device.modelId;
      }
      if (device.manufacturerId.length() > 0) {
        dev[F("Manufacturer")] = device.manufacturerId;
      }
    }

    // If dump_mode == 2, dump a lot more details
    if (2 == dump_mode) {
      JsonObject& dev_endpoints = dev.createNestedObject(F("Endpoints"));
      for (std::vector<uint32_t>::const_iterator ite = device.endpoints.begin() ; ite != device.endpoints.end(); ++ite) {
        uint32_t ep_profile = *ite;
        uint8_t endpoint = (ep_profile >> 16) & 0xFF;
        uint16_t profileId = ep_profile & 0xFFFF;

        snprintf_P(hex, sizeof(hex), PSTR("0x%02X"), endpoint);
        JsonObject& ep = dev_endpoints.createNestedObject(hex);

        snprintf_P(hex, sizeof(hex), PSTR("0x%04X"), profileId);
        ep[F("ProfileId")] = hex;

        int32_t found = -1;
        for (uint32_t i = 0; i < sizeof(Z_ProfileIds) / sizeof(Z_ProfileIds[0]); i++) {
          if (pgm_read_word(&Z_ProfileIds[i]) == profileId) {
            found = i;
            break;
          }
        }
        if (found > 0) {
          GetTextIndexed(hex, sizeof(hex), found, Z_ProfileNames);
          ep[F("ProfileIdName")] = hex;
        }

        ep.createNestedArray(F("ClustersIn"));
        ep.createNestedArray(F("ClustersOut"));
      }

      for (std::vector<uint32_t>::const_iterator itc = device.clusters_in.begin() ; itc != device.clusters_in.end(); ++itc) {
        uint16_t cluster = *itc & 0xFFFF;
        uint8_t  endpoint = (*itc >> 16) & 0xFF;

        snprintf_P(hex, sizeof(hex), PSTR("0x%02X"), endpoint);
        JsonArray &cluster_arr = dev_endpoints[hex][F("ClustersIn")];

        snprintf_P(hex, sizeof(hex), PSTR("0x%04X"), cluster);
        cluster_arr.add(hex);
      }

      for (std::vector<uint32_t>::const_iterator itc = device.clusters_out.begin() ; itc != device.clusters_out.end(); ++itc) {
        uint16_t cluster = *itc & 0xFFFF;
        uint8_t  endpoint = (*itc >> 16) & 0xFF;

        snprintf_P(hex, sizeof(hex), PSTR("0x%02X"), endpoint);
        JsonArray &cluster_arr = dev_endpoints[hex][F("ClustersOut")];

        snprintf_P(hex, sizeof(hex), PSTR("0x%04X"), cluster);
        cluster_arr.add(hex);
      }
    }
  }
  String payload = "";
  payload.reserve(200);
  json.printTo(payload);
  return payload;
}

#endif // USE_ZIGBEE
