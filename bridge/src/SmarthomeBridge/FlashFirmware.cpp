#include "FlashFirmware.h"

#include "ClunetCommands.h"

extern LinkedList<clunet_packet*> uartQueue;
extern LinkedList<clunet_packet*> multicastQueue;
extern volatile unsigned char uart_rx_data_len;
extern bool uart_rx_overflow;

extern uint8_t uart_can_send(uint8_t length);
extern uint8_t uart_send_message(char code, char* data, uint8_t length);

namespace FlashFirmware {

static constexpr uint8_t UART_MESSAGE_CODE_CLUNET = 1;
static constexpr unsigned long BOOTLOADER_ACTIVITY_TIMEOUT = 10000;
static constexpr uint8_t FLASH_LOCAL_SRC_ADDRESS = 0xEE;
static constexpr uint16_t FLASH_FIRMWARE_MAX_SIZE = 8192;
static constexpr uint8_t FLASH_WRITE_CHUNK_MAX = 64;
static constexpr uint16_t FLASH_UPLOAD_MAX_LINE_LENGTH = 640;
static constexpr uint32_t FLASH_UPLOAD_MAX_RAW_SIZE = 65535UL;
static constexpr uint16_t FLASH_DEFAULT_RESPONSE_TIMEOUT = 1500;
static constexpr uint16_t FLASH_DEFAULT_BOOT_TIMEOUT = 2500;
static constexpr uint16_t FLASH_MIN_WRITE_ACK_DELAY_MS = 15;

static constexpr uint8_t COMMAND_FIRMWARE_UPDATE_START = 0;
static constexpr uint8_t COMMAND_FIRMWARE_UPDATE_INIT = 1;
static constexpr uint8_t COMMAND_FIRMWARE_UPDATE_READY = 2;
static constexpr uint8_t COMMAND_FIRMWARE_UPDATE_WRITE = 3;
static constexpr uint8_t COMMAND_FIRMWARE_UPDATE_WRITTEN = 4;
static constexpr uint8_t COMMAND_FIRMWARE_UPDATE_DONE = 5;
static constexpr uint8_t COMMAND_FIRMWARE_UPDATE_ERROR = 255;

enum flash_stage : uint8_t {
  FLASH_STAGE_IDLE = 0,
  FLASH_STAGE_SEND_REBOOT = 1,
  FLASH_STAGE_WAIT_START = 2,
  FLASH_STAGE_SEND_INIT = 3,
  FLASH_STAGE_WAIT_READY = 4,
  FLASH_STAGE_SEND_WRITE = 5,
  FLASH_STAGE_WAIT_WRITTEN = 6,
  FLASH_STAGE_SEND_DONE = 7,
  FLASH_STAGE_DONE = 8,
  FLASH_STAGE_ERROR = 9
};

typedef struct {
  bool inProgress;
  bool hasError;
  bool eofSeen;
  uint32_t rawSize;
  uint32_t baseOffset;
  uint16_t firmwareLength;
  uint16_t lineLength;
  char line[FLASH_UPLOAD_MAX_LINE_LENGTH];
  char error[96];
} flash_upload_state;

typedef struct {
  bool active;
  flash_stage stage;
  uint8_t target;
  uint16_t responseTimeoutMs;
  uint16_t bootTimeoutMs;
  uint16_t pageSize;
  uint16_t firmwareLength;
  uint16_t firmwareOffset;
  uint16_t pendingChunkLength;
  uint8_t currentPage;
  uint16_t currentPageOffset;
  unsigned long writeSentAt;
  unsigned long waitDeadline;
  unsigned long startedAt;
  unsigned long finishedAt;
  char status[96];
  char error[96];
} flash_session_state;

static uint8_t bootloaderTargetAddress = 0;
static unsigned long bootloaderActivityDeadline = 0;

static flash_upload_state flashUpload = {};
static flash_session_state flashSession = {};
static uint8_t flashFirmware[FLASH_FIRMWARE_MAX_SIZE];
static uint16_t flashFirmwareLength = 0;
static bool flashFirmwareReady = false;

static const __FlashStringHelper* flashStageName(flash_stage stage){
  switch(stage){
    case FLASH_STAGE_IDLE: return F("idle");
    case FLASH_STAGE_SEND_REBOOT: return F("send_reboot");
    case FLASH_STAGE_WAIT_START: return F("wait_start");
    case FLASH_STAGE_SEND_INIT: return F("send_init");
    case FLASH_STAGE_WAIT_READY: return F("wait_ready");
    case FLASH_STAGE_SEND_WRITE: return F("send_write");
    case FLASH_STAGE_WAIT_WRITTEN: return F("wait_written");
    case FLASH_STAGE_SEND_DONE: return F("send_done");
    case FLASH_STAGE_DONE: return F("done");
    case FLASH_STAGE_ERROR: return F("error");
    default: return F("unknown");
  }
}

static bool flashStageHasTimeout(flash_stage stage){
  return stage == FLASH_STAGE_WAIT_START || stage == FLASH_STAGE_WAIT_READY || stage == FLASH_STAGE_WAIT_WRITTEN;
}

static void flashSetStatus(const char* status){
  if (!status){
    flashSession.status[0] = 0;
    return;
  }
  strncpy(flashSession.status, status, sizeof(flashSession.status) - 1);
  flashSession.status[sizeof(flashSession.status) - 1] = 0;
}

static void flashSetUploadError(const char* message){
  flashUpload.hasError = true;
  if (message){
    strncpy(flashUpload.error, message, sizeof(flashUpload.error) - 1);
    flashUpload.error[sizeof(flashUpload.error) - 1] = 0;
  } else {
    flashUpload.error[0] = 0;
  }
}

static void flashSetSessionError(const char* message){
  flashSession.active = false;
  flashSession.stage = FLASH_STAGE_ERROR;
  flashSession.finishedAt = millis();
  if (message){
    strncpy(flashSession.error, message, sizeof(flashSession.error) - 1);
    flashSession.error[sizeof(flashSession.error) - 1] = 0;
    flashSetStatus(message);
  } else {
    flashSession.error[0] = 0;
    flashSetStatus("flash error");
  }
}

static void flashResetUploadState(){
  memset(&flashUpload, 0, sizeof(flashUpload));
  memset(flashFirmware, 0xFF, sizeof(flashFirmware));
  flashFirmwareLength = 0;
  flashFirmwareReady = false;
}

static int8_t flashHexNibble(char c){
  if (c >= '0' && c <= '9'){
    return c - '0';
  }
  if (c >= 'a' && c <= 'f'){
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F'){
    return c - 'A' + 10;
  }
  return -1;
}

static bool flashHexByte(const char* chars, uint8_t* out){
  int8_t hi = flashHexNibble(chars[0]);
  int8_t lo = flashHexNibble(chars[1]);
  if (hi < 0 || lo < 0){
    return false;
  }
  *out = static_cast<uint8_t>((hi << 4) | lo);
  return true;
}

static bool flashProcessHexLine(const char* line, uint16_t lineLength){
  if (!lineLength){
    return true;
  }
  if (line[0] != ':'){
    flashSetUploadError("invalid HEX line prefix");
    return false;
  }
  if (lineLength < 11){
    flashSetUploadError("invalid HEX line length");
    return false;
  }

  uint8_t recordLength = 0;
  uint8_t offsetHi = 0;
  uint8_t offsetLo = 0;
  uint8_t recordType = 0;
  if (!flashHexByte(line + 1, &recordLength) ||
      !flashHexByte(line + 3, &offsetHi) ||
      !flashHexByte(line + 5, &offsetLo) ||
      !flashHexByte(line + 7, &recordType)){
    flashSetUploadError("invalid HEX header");
    return false;
  }

  uint16_t expectedLength = static_cast<uint16_t>(11 + (recordLength * 2));
  if (lineLength != expectedLength){
    flashSetUploadError("invalid HEX record length");
    return false;
  }

  uint8_t recordData[255];
  for (uint16_t i = 0; i < recordLength; i++){
    if (!flashHexByte(line + 9 + i * 2, &recordData[i])){
      flashSetUploadError("invalid HEX data");
      return false;
    }
  }

  uint8_t checksum = 0;
  if (!flashHexByte(line + 9 + recordLength * 2, &checksum)){
    flashSetUploadError("invalid HEX checksum");
    return false;
  }

  uint16_t crc = recordLength + offsetHi + offsetLo + recordType;
  for (uint16_t i = 0; i < recordLength; i++){
    crc += recordData[i];
  }
  uint8_t expectedChecksum = static_cast<uint8_t>((0x100 - (crc & 0xFF)) & 0xFF);
  if (checksum != expectedChecksum){
    flashSetUploadError("HEX checksum mismatch");
    return false;
  }

  uint16_t recordOffset = static_cast<uint16_t>((offsetHi << 8) | offsetLo);
  switch(recordType){
    case 0: {
      uint32_t offset = flashUpload.baseOffset + recordOffset;
      if (offset + recordLength > FLASH_FIRMWARE_MAX_SIZE){
        flashSetUploadError("firmware exceeds 8KB limit");
        return false;
      }
      if (recordLength){
        memcpy(flashFirmware + offset, recordData, recordLength);
      }
      uint16_t firmwareLength = static_cast<uint16_t>(offset + recordLength);
      if (firmwareLength > flashUpload.firmwareLength){
        flashUpload.firmwareLength = firmwareLength;
      }
      break;
    }
    case 1:
      flashUpload.eofSeen = true;
      break;
    case 2:
      if (recordLength != 2){
        flashSetUploadError("invalid HEX segment address");
        return false;
      }
      flashUpload.baseOffset = static_cast<uint32_t>(((recordData[0] << 8) | recordData[1]) << 4);
      break;
    case 4:
      if (recordLength != 2){
        flashSetUploadError("invalid HEX linear address");
        return false;
      }
      flashUpload.baseOffset = static_cast<uint32_t>(((recordData[0] << 8) | recordData[1]) << 16);
      break;
    default:
      break;
  }

  return true;
}

static void flashUploadConsumeChunk(const uint8_t* data, size_t length){
  if (!data || !length || flashUpload.hasError){
    return;
  }

  for (size_t i = 0; i < length; i++){
    char c = static_cast<char>(data[i]);
    if (c == '\r'){
      continue;
    }
    if (c == '\n'){
      flashUpload.line[flashUpload.lineLength] = 0;
      if (flashUpload.lineLength && !flashProcessHexLine(flashUpload.line, flashUpload.lineLength)){
        return;
      }
      flashUpload.lineLength = 0;
      continue;
    }
    if (flashUpload.lineLength + 1 >= FLASH_UPLOAD_MAX_LINE_LENGTH){
      flashSetUploadError("HEX line too long");
      return;
    }
    flashUpload.line[flashUpload.lineLength++] = c;
  }
}

static void flashUploadFinalize(){
  if (flashUpload.lineLength && !flashUpload.hasError){
    flashUpload.line[flashUpload.lineLength] = 0;
    flashProcessHexLine(flashUpload.line, flashUpload.lineLength);
  }

  if (!flashUpload.hasError && !flashUpload.eofSeen){
    flashSetUploadError("HEX EOF record not found");
  }
  if (!flashUpload.hasError && flashUpload.firmwareLength == 0){
    flashSetUploadError("HEX has no firmware data");
  }

  flashUpload.inProgress = false;
  if (flashUpload.hasError){
    flashFirmwareReady = false;
    flashFirmwareLength = 0;
    return;
  }

  flashFirmwareReady = true;
  flashFirmwareLength = flashUpload.firmwareLength;
  flashUpload.error[0] = 0;
}

static bool flashSendClunetPacket(uint8_t dst, uint8_t command, const uint8_t* data, uint8_t size){
  if (size > FLASH_WRITE_CHUNK_MAX + 4){
    return false;
  }

  const uint8_t packetLength = static_cast<uint8_t>(size + 4);
  if (!uart_can_send(packetLength)){
    return false;
  }

  char packet[4 + FLASH_WRITE_CHUNK_MAX + 4];
  packet[0] = static_cast<char>(FLASH_LOCAL_SRC_ADDRESS);
  packet[1] = static_cast<char>(dst);
  packet[2] = static_cast<char>(command);
  packet[3] = static_cast<char>(size);
  if (size && data){
    memcpy(packet + 4, data, size);
  }

  if (!uart_send_message(UART_MESSAGE_CODE_CLUNET, packet, packetLength)){
    return false;
  }

  touchBootloaderActivity(dst);
  return true;
}

static void clearPacketQueue(LinkedList<clunet_packet*>& queue){
  while (!queue.isEmpty()){
    queue.remove(queue.front());
  }
}

static void flashStartSession(uint8_t target, uint16_t responseTimeoutMs, uint16_t bootTimeoutMs){
  memset(&flashSession, 0, sizeof(flashSession));

  flashSession.active = true;
  flashSession.stage = FLASH_STAGE_SEND_REBOOT;
  flashSession.target = target;
  flashSession.responseTimeoutMs = responseTimeoutMs ? responseTimeoutMs : FLASH_DEFAULT_RESPONSE_TIMEOUT;
  flashSession.bootTimeoutMs = bootTimeoutMs;
  flashSession.firmwareLength = flashFirmwareLength;
  flashSession.startedAt = millis();
  flashSetStatus("sending reboot command");

  clearPacketQueue(uartQueue);
  clearPacketQueue(multicastQueue);
  while (Serial.available() > 0){
    Serial.read();
  }
  uart_rx_data_len = 0;
  uart_rx_overflow = false;
  touchBootloaderActivity(target);
}

static void flashAbortSession(const char* reason){
  if (!flashSession.active && flashSession.stage != FLASH_STAGE_ERROR){
    return;
  }
  flashSetSessionError(reason ? reason : "flash aborted");
}

bool handleBootControlResponse(clunet_packet* packet){
  if (!packet || !flashSession.active || packet->command != CLUNET_COMMAND_BOOT_CONTROL){
    return false;
  }
  if (packet->src != flashSession.target || packet->size < 1){
    return false;
  }

  uint8_t subcommand = static_cast<uint8_t>(packet->data[0]);
  if (subcommand == COMMAND_FIRMWARE_UPDATE_ERROR){
    flashSetSessionError("device reported bootloader error");
    return true;
  }

  switch(flashSession.stage){
    case FLASH_STAGE_WAIT_START:
      if (subcommand == COMMAND_FIRMWARE_UPDATE_START){
        flashSession.stage = FLASH_STAGE_SEND_INIT;
        flashSetStatus("bootloader detected, sending init");
      }
      break;
    case FLASH_STAGE_WAIT_READY:
      if (subcommand == COMMAND_FIRMWARE_UPDATE_READY && packet->size >= 3){
        uint16_t pageSize = static_cast<uint8_t>(packet->data[1]) |
          (static_cast<uint16_t>(static_cast<uint8_t>(packet->data[2])) << 8);
        if (!pageSize || pageSize > FLASH_FIRMWARE_MAX_SIZE){
          flashSetSessionError("invalid page size from bootloader");
        } else {
          flashSession.pageSize = pageSize;
          flashSession.stage = FLASH_STAGE_SEND_WRITE;
          flashSetStatus("bootloader ready, writing firmware");
        }
      }
      break;
    case FLASH_STAGE_WAIT_WRITTEN:
      if (subcommand == COMMAND_FIRMWARE_UPDATE_WRITTEN){
        if ((millis() - flashSession.writeSentAt) < FLASH_MIN_WRITE_ACK_DELAY_MS){
          return true;
        }
        flashSession.firmwareOffset = static_cast<uint16_t>(flashSession.firmwareOffset + flashSession.pendingChunkLength);
        flashSession.pendingChunkLength = 0;
        if (flashSession.firmwareOffset >= flashSession.firmwareLength){
          flashSession.stage = FLASH_STAGE_SEND_DONE;
          flashSetStatus("finishing flash");
        } else {
          flashSession.stage = FLASH_STAGE_SEND_WRITE;
          flashSetStatus("writing firmware");
        }
      }
      break;
    default:
      break;
  }

  return true;
}

static void flashProcessSession(){
  if (!flashSession.active){
    return;
  }

  if (flashStageHasTimeout(flashSession.stage) &&
      flashSession.waitDeadline != 0 &&
      (long)(millis() - flashSession.waitDeadline) >= 0){
    flashSetSessionError("timeout while waiting bootloader response");
    return;
  }

  switch(flashSession.stage){
    case FLASH_STAGE_SEND_REBOOT:
      if (flashSendClunetPacket(flashSession.target, CLUNET_COMMAND_REBOOT, nullptr, 0)){
        flashSession.stage = FLASH_STAGE_WAIT_START;
        flashSession.waitDeadline = flashSession.bootTimeoutMs ? (millis() + flashSession.bootTimeoutMs) : 0;
        flashSetStatus("waiting bootloader start");
      }
      break;
    case FLASH_STAGE_SEND_INIT: {
      uint8_t data[] = {COMMAND_FIRMWARE_UPDATE_INIT};
      if (flashSendClunetPacket(flashSession.target, CLUNET_COMMAND_BOOT_CONTROL, data, sizeof(data))){
        flashSession.stage = FLASH_STAGE_WAIT_READY;
        flashSession.waitDeadline = millis() + flashSession.responseTimeoutMs;
        flashSetStatus("waiting bootloader ready");
      }
      break;
    }
    case FLASH_STAGE_SEND_WRITE: {
      if (!flashSession.pageSize){
        flashSetSessionError("page size is zero");
        break;
      }
      if (flashSession.firmwareOffset >= flashSession.firmwareLength){
        flashSession.stage = FLASH_STAGE_SEND_DONE;
        flashSetStatus("finishing flash");
        break;
      }

      uint16_t pageBase = static_cast<uint16_t>((flashSession.firmwareOffset / flashSession.pageSize) * flashSession.pageSize);
      uint16_t pageLength = flashSession.pageSize;
      uint16_t firmwareRemaining = static_cast<uint16_t>(flashSession.firmwareLength - pageBase);
      if (pageLength > firmwareRemaining){
        pageLength = firmwareRemaining;
      }

      uint16_t chunkOffset = static_cast<uint16_t>(flashSession.firmwareOffset - pageBase);
      uint16_t chunkLength = static_cast<uint16_t>(pageLength - chunkOffset);
      if (chunkLength > FLASH_WRITE_CHUNK_MAX){
        chunkLength = FLASH_WRITE_CHUNK_MAX;
      }
      if (!chunkLength){
        flashSetSessionError("invalid firmware chunk length");
        break;
      }

      uint8_t payload[4 + FLASH_WRITE_CHUNK_MAX];
      payload[0] = COMMAND_FIRMWARE_UPDATE_WRITE;
      payload[1] = static_cast<uint8_t>(pageBase / flashSession.pageSize);
      payload[2] = static_cast<uint8_t>(chunkLength & 0xFF);
      payload[3] = static_cast<uint8_t>((chunkLength >> 8) & 0xFF);
      memcpy(payload + 4, flashFirmware + flashSession.firmwareOffset, chunkLength);

      if (flashSendClunetPacket(flashSession.target, CLUNET_COMMAND_BOOT_CONTROL, payload, chunkLength + 4)){
        flashSession.currentPage = payload[1];
        flashSession.currentPageOffset = chunkOffset;
        flashSession.pendingChunkLength = chunkLength;
        flashSession.writeSentAt = millis();
        flashSession.stage = FLASH_STAGE_WAIT_WRITTEN;
        flashSession.waitDeadline = millis() + flashSession.responseTimeoutMs;
      }
      break;
    }
    case FLASH_STAGE_SEND_DONE: {
      uint8_t data[] = {COMMAND_FIRMWARE_UPDATE_DONE};
      if (flashSendClunetPacket(flashSession.target, CLUNET_COMMAND_BOOT_CONTROL, data, sizeof(data))){
        flashSession.active = false;
        flashSession.stage = FLASH_STAGE_DONE;
        flashSession.finishedAt = millis();
        flashSetStatus("firmware flashed");
      }
      break;
    }
    default:
      break;
  }
}

static AsyncResponseStream* beginFlashResponse(AsyncWebServerRequest* request){
  AsyncResponseStream* response = request->beginResponseStream("application/json");
  response->addHeader(F("Cache-Control"), F("no-store, no-cache, must-revalidate, max-age=0"));
  response->addHeader(F("Pragma"), F("no-cache"));
  response->addHeader(F("Expires"), F("0"));
  return response;
}

static void fillFlashStatusResponse(AsyncResponseStream* response){
  uint8_t progress = 0;
  if (flashSession.firmwareLength){
    progress = static_cast<uint8_t>((static_cast<uint32_t>(flashSession.firmwareOffset) * 100UL) / flashSession.firmwareLength);
  }

  response->print(F("{\"firmwareReady\":"));
  response->print(flashFirmwareReady ? F("true") : F("false"));
  response->print(F(",\"firmwareBytes\":"));
  response->print(flashFirmwareLength);
  response->print(F(",\"uploadInProgress\":"));
  response->print(flashUpload.inProgress ? F("true") : F("false"));
  response->print(F(",\"uploadRawBytes\":"));
  response->print(flashUpload.rawSize);
  response->print(F(",\"uploadError\":\""));
  response->print(flashUpload.error);
  response->print(F("\",\"active\":"));
  response->print(flashSession.active ? F("true") : F("false"));
  response->print(F(",\"stage\":\""));
  response->print(flashStageName(flashSession.stage));
  response->print(F("\",\"target\":"));
  response->print(flashSession.target);
  response->print(F(",\"responseTimeoutMs\":"));
  response->print(flashSession.responseTimeoutMs);
  response->print(F(",\"bootTimeoutMs\":"));
  response->print(flashSession.bootTimeoutMs);
  response->print(F(",\"pageSize\":"));
  response->print(flashSession.pageSize);
  response->print(F(",\"sentBytes\":"));
  response->print(flashSession.firmwareOffset);
  response->print(F(",\"progress\":"));
  response->print(progress);
  response->print(F(",\"page\":"));
  response->print(flashSession.currentPage);
  response->print(F(",\"pageOffset\":"));
  response->print(flashSession.currentPageOffset);
  response->print(F(",\"startedAt\":"));
  response->print(flashSession.startedAt);
  response->print(F(",\"finishedAt\":"));
  response->print(flashSession.finishedAt);
  response->print(F(",\"status\":\""));
  response->print(flashSession.status);
  response->print(F("\",\"error\":\""));
  response->print(flashSession.error);
  response->print(F("\"}"));
}

void init(){
  flashResetUploadState();
  memset(&flashSession, 0, sizeof(flashSession));
  flashSession.stage = FLASH_STAGE_IDLE;
  flashSetStatus("idle");
  bootloaderTargetAddress = 0;
  bootloaderActivityDeadline = 0;
}

bool isBootloaderUartIsolated(uint8_t address){
  return bootloaderActivityDeadline != 0 && (long)(millis() - bootloaderActivityDeadline) < 0 &&
    (address == 0 || address == bootloaderTargetAddress);
}

bool isTrafficMuted(){
  return isBootloaderUartIsolated();
}

void touchBootloaderActivity(uint8_t address){
  bootloaderTargetAddress = address;
  bootloaderActivityDeadline = millis() + BOOTLOADER_ACTIVITY_TIMEOUT;
}

bool shouldForwardMulticastToUart(clunet_packet* packet){
  if (!packet){
    return false;
  }
  bool bootControl = packet->command == CLUNET_COMMAND_BOOT_CONTROL;
  bool allowedIsolatedBootControl = !flashSession.active &&
      bootControl && packet->dst == bootloaderTargetAddress;
  bool allowedToUart = !isBootloaderUartIsolated() || allowedIsolatedBootControl;
  if (bootControl && !flashSession.active){
    touchBootloaderActivity(packet->dst);
  }
  return allowedToUart;
}

void setupRoutes(AsyncWebServer& server){
  server.on("/flash/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncResponseStream* response = beginFlashResponse(request);
    fillFlashStatusResponse(response);
    request->send(response);
  });

  server.on("/flash/abort", HTTP_POST, [](AsyncWebServerRequest* request) {
    flashAbortSession("flash aborted by user");
    AsyncResponseStream* response = beginFlashResponse(request);
    fillFlashStatusResponse(response);
    request->send(response);
  });

  server.on("/flash/start", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (flashUpload.inProgress){
      request->send(409, "application/json", "{\"ok\":false,\"error\":\"firmware upload is in progress\"}");
      return;
    }
    if (!flashFirmwareReady){
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"firmware is not uploaded\"}");
      return;
    }
    if (flashSession.active){
      request->send(409, "application/json", "{\"ok\":false,\"error\":\"flashing already active\"}");
      return;
    }
    if (!request->hasParam("a", true)){
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing parameter: a\"}");
      return;
    }

    uint8_t target = static_cast<uint8_t>(request->getParam("a", true)->value().toInt());
    uint16_t responseTimeoutMs = request->hasParam("rt", true) ?
      static_cast<uint16_t>(request->getParam("rt", true)->value().toInt()) : FLASH_DEFAULT_RESPONSE_TIMEOUT;
    uint16_t bootTimeoutMs = request->hasParam("bt", true) ?
      static_cast<uint16_t>(request->getParam("bt", true)->value().toInt()) : FLASH_DEFAULT_BOOT_TIMEOUT;

    if (responseTimeoutMs < 100){
      responseTimeoutMs = 100;
    }
    if (bootTimeoutMs > 0 && bootTimeoutMs < 100){
      bootTimeoutMs = 100;
    }

    flashStartSession(target, responseTimeoutMs, bootTimeoutMs);
    AsyncResponseStream* response = beginFlashResponse(request);
    fillFlashStatusResponse(response);
    request->send(response);
  });

  server.on("/flash/firmware", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (flashUpload.hasError){
      AsyncResponseStream* response = beginFlashResponse(request);
      response->setCode(400);
      response->print(F("{\"ok\":false,\"error\":\""));
      response->print(flashUpload.error);
      response->print(F("\"}"));
      request->send(response);
      return;
    }
    if (!flashFirmwareReady){
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"firmware upload failed\"}");
      return;
    }
    AsyncResponseStream* response = beginFlashResponse(request);
    fillFlashStatusResponse(response);
    request->send(response);
  }, [](AsyncWebServerRequest* /*request*/, String /*filename*/, size_t index, uint8_t *data, size_t len, bool final) {
    if (index == 0){
      flashResetUploadState();
      flashUpload.inProgress = true;
      flashUpload.rawSize = 0;
      flashUpload.baseOffset = 0;
      flashUpload.lineLength = 0;
      flashUpload.error[0] = 0;
      if (flashSession.active){
        flashSetUploadError("can't upload during active flashing");
      }
    }

    if (flashUpload.hasError){
      if (final){
        flashUpload.inProgress = false;
      }
      return;
    }

    flashUpload.rawSize += len;
    if (flashUpload.rawSize > FLASH_UPLOAD_MAX_RAW_SIZE){
      flashSetUploadError("HEX file is too large");
      if (final){
        flashUpload.inProgress = false;
      }
      return;
    }

    flashUploadConsumeChunk(data, len);

    if (final){
      flashUploadFinalize();
    }
  });
}

void process(){
  flashProcessSession();
}

} // namespace FlashFirmware
