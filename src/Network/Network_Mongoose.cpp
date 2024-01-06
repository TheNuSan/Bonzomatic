#include <stdio.h>
#include "jsonxx.h"
#include "mongoose.h"
#include "../Cmdline.h"

#include "Network.h"

namespace Network
{
	static int s_done = 0;
	static int s_is_connected = 0;

	bool NewShaderToGrab = false;
  bool ShaderHasBeenCompiled = false;
	jsonxx::Object LastGrabberShader;
  float NetworkTime = 0.0;
	float LastSendTime = 0.0f;
  float LastShaderGrabTime = -1000.0f;
	float ShaderUpdateInterval = 0.3f;

  bool SyncTimeWithSender = true;
  bool NewTimeToGrab = false;
  float GrabShaderTime = 0.0;

  bool SendMidiControls = true;
  bool GrabMidiControls = true;

  bool TryingToConnect = false;
  float ReconnectionInterval = 1.0f;
  float LastReconnectionAttempt = 0.0f;
  
	enum NetMode {
		NetMode_Sender,
		NetMode_Grabber
	};

	bool bNetworkEnabled = false;
	bool bNetworkLaunched = false;
	std::string ServerURL;
	std::string NetworkModeString;
	NetMode NetworkMode = NetMode::NetMode_Sender;

  std::string ServerName;
  std::string RoomName;
  std::string NickName;

	void RecieveShader(size_t size, unsigned char *data);

	static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
		(void)nc;

		switch (ev) {
			case MG_EV_CONNECT: {
				int status = *((int *)ev_data);
				if (status != 0) {
					printf("-- Connection error: %d\n", status);
				}
        TryingToConnect = false;
				break;
			}
			case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
				struct http_message *hm = (struct http_message *) ev_data;
				if (hm->resp_code == 101) {
					printf("-- Connected\n");
					s_is_connected = 1;
					bNetworkLaunched = true;
				}
				else {
					printf("-- Connection failed! HTTP code %d\n", hm->resp_code);
					/* Connection will be closed after this. */
				}
				break;
			}
			case MG_EV_POLL: {
				//char msg[500];
				//strcpy(msg, "Bonjour de loin");
				//mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, msg, strlen(msg));
				break;
			}
			case MG_EV_WEBSOCKET_FRAME: {
				struct websocket_message *wm = (struct websocket_message *) ev_data;
				//printf("Grabbed message %d.\n", (int)wm->size);
				RecieveShader(wm->size, wm->data);
				// TODO: clean the buffer with mbuf_remove(); ? or maybe not needed with websocket ...
				break;
			}
			case MG_EV_CLOSE: {
				if (s_is_connected) printf("-- Disconnected\n");
				s_done = 1;
				bNetworkLaunched = false;
        ShaderHasBeenCompiled = false; // So we will try to recompile when connection is on again
				break;
			}
		}
	}

	struct mg_mgr mgr;
	struct mg_connection *nc;
  
	void LoadSettings(jsonxx::Object & o, NETWORK_SETTINGS* DialogSettings)
	{
		if (o.has<jsonxx::Object>("network"))
		{
			jsonxx::Object netjson = o.get<jsonxx::Object>("network");
			if (netjson.has<jsonxx::Number>("updateInterval"))
				ShaderUpdateInterval = netjson.get<jsonxx::Number>("updateInterval");
      if (netjson.has<jsonxx::Boolean>("SyncTimeWithSender"))
        SyncTimeWithSender = netjson.get<jsonxx::Boolean>("SyncTimeWithSender");
      if (netjson.has<jsonxx::Boolean>("SendMidiControls"))
        SendMidiControls = netjson.get<jsonxx::Boolean>("SendMidiControls");
      if (netjson.has<jsonxx::Boolean>("GrabMidiControls"))
        GrabMidiControls = netjson.get<jsonxx::Boolean>("GrabMidiControls");
		}
    	bNetworkEnabled = DialogSettings->EnableNetwork;
      ServerURL = DialogSettings->ServerURL;
	    NetworkModeString = DialogSettings->NetworkModeString;
	}

	void CommandLine(int argc, const char *argv[], NETWORK_SETTINGS* DialogSettings) {
		if (CmdHasOption(argc, argv, "serverURL", &DialogSettings->ServerURL)) {
			DialogSettings->EnableNetwork = true;
			printf("Set server URL to %s \n", DialogSettings->ServerURL.c_str());
		}
		if (CmdHasOption(argc, argv, "networkMode", &DialogSettings->NetworkModeString)) {
			DialogSettings->EnableNetwork = true;
			printf("Set server mode to %s \n", DialogSettings->NetworkModeString.c_str());
		}
	}

	void PrepareConnection()
	{
		if (!bNetworkEnabled) return;
		
		if (NetworkModeString == "grabber") {
			NetworkMode = NetMode_Grabber;
		}

		if (NetworkMode == NetMode_Grabber) {
			printf("Network Launching as grabber\n");
		} else {
			printf("Network Launching as sender\n");
		}

    Network_Break_URL(ServerURL, ServerName, RoomName, NickName);

		mg_mgr_init(&mgr, NULL);
  }

  void OpenConnection()
	{
    if (!bNetworkEnabled) return;
    LastReconnectionAttempt = NetworkTime;
    TryingToConnect = true;

		// ws://127.0.0.1:8000
		printf("Try to connect to %s\n", ServerURL.c_str());
		nc = mg_connect_ws(&mgr, ev_handler, ServerURL.c_str(), "ws_chat", NULL);
		if (nc == NULL) {
			fprintf(stderr, "Invalid address\n");
			return;
		}
	}

	void BroadcastMessage(const char* msg) {
		if (!bNetworkLaunched) return;

		mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, msg, strlen(msg)+1);
	}

	void SendShader(ShaderMessage NewMessage, float shaderOffset, const jsonxx::Object& shaderParameters) {
		if (!bNetworkLaunched) return;
		if (NetworkMode != NetMode_Sender) return;

		using namespace jsonxx;

		jsonxx::Object Data;
		Data << "Code" << std::string(NewMessage.Code);
		Data << "Compile" << NewMessage.NeedRecompile;
		Data << "Caret" << NewMessage.CaretPosition;
		Data << "Anchor" << NewMessage.AnchorPosition;
    Data << "FirstVisibleLine" << NewMessage.FirstVisibleLine;
    Data << "RoomName" << RoomName;
    Data << "NickName" << NickName;
    Data << "ShaderTime" << NetworkTime + shaderOffset;
    if(SendMidiControls) Data << "Parameters" << shaderParameters;
    
		jsonxx::Object Message = Object("Data", Data);
		std::string TextJson = Message.json();
		//printf("JSON: %s\n", TextJson.c_str());
		mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, TextJson.c_str(), TextJson.length()+1);
		LastSendTime = NetworkTime;
	}

	bool IsShaderNeedUpdate() {
		if (!bNetworkLaunched) return false;
		if (NetworkMode != NetMode_Sender) return false;
		return (NetworkTime - LastSendTime >= ShaderUpdateInterval);
	}

	void RecieveShader(size_t size, unsigned char *data) {
		// TODO: very very bad, we should:
		// - use json
		// - verify size
		// - non-ascii symbols ?
		// - asynchronous update ?
		std::string TextJson = std::string((char*)data, size);
		//printf("JSON: %s\n", TextJson.c_str());
		jsonxx::Object NewShader;
    jsonxx::Object Data;
		bool ErrorFound = false;
		if (NewShader.parse(TextJson)) {
			if(NewShader.has<jsonxx::Object>("Data")) {
        Data = NewShader.get<jsonxx::Object>("Data");
				if (!Data.has<jsonxx::String>("Code")) ErrorFound = true;
				if (!Data.has<jsonxx::Number>("Caret")) ErrorFound = true;
				if (!Data.has<jsonxx::Number>("Anchor")) ErrorFound = true;
        if (!Data.has<jsonxx::Number>("FirstVisibleLine")) ErrorFound = true;
				if (!Data.has<jsonxx::Boolean>("Compile")) ErrorFound = true;
			} else {
				ErrorFound = true;
			}
		} else {
			ErrorFound = true;
		}
		if(ErrorFound) {
			printf("Invalid json formatting\n");
			return;
		}

		LastGrabberShader = NewShader;
		NewShaderToGrab = true;

    if (Data.has<jsonxx::Number>("ShaderTime")) {
      NewTimeToGrab = true;
      GrabShaderTime = Data.get<jsonxx::Number>("ShaderTime");
    }
    else {
      NewTimeToGrab = false;
    }
	}

  bool IsNetworkEnabled() {
    return bNetworkEnabled;
  }

	bool IsConnected() {
		return bNetworkLaunched;
	}

	bool IsGrabber() {
		return NetworkMode == NetMode_Grabber;
	}

  bool IsLive() {
    if(!bNetworkLaunched) return false;
    if(NetworkMode == NetMode_Grabber) {
      // if we got a shader less that 2s ago, we can say that there is someone sending
      float MaxLiveStatusDuration = 2.0;
      return (NetworkTime-LastShaderGrabTime < MaxLiveStatusDuration);
    }
    return true;
  }

  std::string GetNickName() {
    return NickName;
  }

  std::string GetModeString() {
    return NetworkModeString;
  }

  bool CanSendMidiControls() {
    return SendMidiControls && bNetworkLaunched && NetworkMode == NetMode_Sender;
  }

  bool CanGrabMidiControls() {
    return GrabMidiControls && bNetworkLaunched && NetworkMode == NetMode_Grabber;
  }

	bool HasNewShader() {
		if (NetworkMode != NetMode_Grabber) return false;
		return NewShaderToGrab;
	}

	bool GetNewShader(ShaderMessage& OutShader, std::map<std::string, ShaderParamCache>& networkParamCache) {
		if (NetworkMode != NetMode_Grabber) return false;
		NewShaderToGrab = false;
		jsonxx::Object Data = LastGrabberShader.get<jsonxx::Object>("Data");
		OutShader.Code = Data.get<jsonxx::String>("Code");
		OutShader.CaretPosition = Data.get<jsonxx::Number>("Caret");
		OutShader.AnchorPosition = Data.get<jsonxx::Number>("Anchor");
    OutShader.FirstVisibleLine = Data.get<jsonxx::Number>("FirstVisibleLine");
    bool NeedRecompile = Data.get<jsonxx::Boolean>("Compile");
    // If we grab a shader for the first time, we will try to recompile it, in case it's a valid one
    if(!ShaderHasBeenCompiled) {
      NeedRecompile=true;
      ShaderHasBeenCompiled=true;
    }
		OutShader.NeedRecompile = NeedRecompile;
    float duration = NetworkTime - LastShaderGrabTime;
    if (duration < 0.0f) duration = 0.0f;
    LastShaderGrabTime = NetworkTime;

    if (GrabMidiControls && Data.has<jsonxx::Object>("Parameters")) {
      const std::map<std::string, jsonxx::Value*>& shadParams = Data.get<jsonxx::Object>("Parameters").kv_map();
      for (auto it = shadParams.begin(); it != shadParams.end(); it++)
      {
        float goalValue = it->second->number_value_;
        auto cache = networkParamCache.find(it->first);
        if (cache == networkParamCache.end()) {
          ShaderParamCache newCache;
          newCache.lastValue = goalValue;
          newCache.currentValue = goalValue;
          newCache.goalValue = goalValue;
          newCache.duration = duration;
          networkParamCache[it->first] = newCache;
          cache = networkParamCache.find(it->first);
        }
        ShaderParamCache& cur = cache->second;
        cur.lastValue = cur.currentValue;
        cur.goalValue = goalValue;
        cur.duration = duration;
      }
    }
		return true;
	}

  bool AdjustShaderTimeOffset(float time, float& timeOffset) {
    if (NetworkMode != NetMode_Grabber) return false;
    if (!NewTimeToGrab || !SyncTimeWithSender) return false;
    NewTimeToGrab = false;
    float MaxTimeOffsetAllowed = 2.0f;
    if (abs(time + timeOffset - GrabShaderTime) < MaxTimeOffsetAllowed) {
      // time difference is too low to care about
      return false;
    }
    // adjust time offset
    timeOffset = GrabShaderTime - time;
    return true;
  }

	void Tick(float time) {
    NetworkTime = time;
		if (!bNetworkEnabled) return;

    if(!bNetworkLaunched && !TryingToConnect) {
      if(time-LastReconnectionAttempt>=ReconnectionInterval) {
        OpenConnection();
      }
    }
		// TODO: should be in another thread?
		mg_mgr_poll(&mgr, 10);
	}

	void Release() {
		if (!bNetworkLaunched) return;

		mg_mgr_free(&mgr);
	}
}