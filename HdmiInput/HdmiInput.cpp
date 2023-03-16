/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2019 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
**/

#include "HdmiInput.h"
#include "UtilsJsonRpc.h"
#include "UtilsIarm.h"

#include "hdmiIn.hpp"
#include "exception.hpp"
#include "dsUtl.h"
#include "dsError.h"
#include "dsMgr.h"

#include <vector>
#include <algorithm>

#define HDMI_HOT_PLUG_EVENT_CONNECTED 0
#define HDMI_HOT_PLUG_EVENT_DISCONNECTED 1


#define HDMICECSINK_CALLSIGN "org.rdk.HdmiCecSink"
#define HDMICECSINK_CALLSIGN_VER HDMICECSINK_CALLSIGN".1"
#define TVSETTINGS_CALLSIGN "org.rdk.tv.ControlSettings"
#define TVSETTINGS_CALLSIGN_VER TVSETTINGS_CALLSIGN".2"

#define HDMIINPUT_METHOD_GET_HDMI_INPUT_DEVICES "getHDMIInputDevices"
#define HDMIINPUT_METHOD_WRITE_EDID "writeEDID"
#define HDMIINPUT_METHOD_READ_EDID "readEDID"
#define HDMIINPUT_METHOD_READ_RAWHDMISPD "getRawHDMISPD"
#define HDMIINPUT_METHOD_READ_HDMISPD "getHDMISPD"
#define HDMIINPUT_METHOD_SET_EDID_VERSION "setEdidVersion"
#define HDMIINPUT_METHOD_GET_EDID_VERSION "getEdidVersion"
#define HDMIINPUT_METHOD_START_HDMI_INPUT "startHdmiInput"
#define HDMIINPUT_METHOD_STOP_HDMI_INPUT "stopHdmiInput"
#define HDMIINPUT_METHOD_SCALE_HDMI_INPUT "setVideoRectangle"
#define HDMIINPUT_METHOD_SUPPORTED_GAME_FEATURES "getSupportedGameFeatures"
#define HDMIINPUT_METHOD_GAME_FEATURE_STATUS "getHdmiGameFeatureStatus"

#define HDMIINPUT_METHOD_GET_AV_LATENCY "getAVLatency"
#define HDMIINPUT_METHOD_GET_LOW_LATENCY_MODE "getTVLowLatencyMode"

#define HDMIINPUT_EVENT_ON_DEVICES_CHANGED "onDevicesChanged"
#define HDMIINPUT_EVENT_ON_SIGNAL_CHANGED "onSignalChanged"
#define HDMIINPUT_EVENT_ON_STATUS_CHANGED "onInputStatusChanged"
#define HDMIINPUT_EVENT_ON_VIDEO_MODE_UPDATED "videoStreamInfoUpdate"
#define HDMIINPUT_EVENT_ON_GAME_FEATURE_STATUS_CHANGED "hdmiGameFeatureStatusUpdate"

// TODO: remove this
#define registerMethod(...) for (uint8_t i = 1; GetHandler(i); i++) GetHandler(i)->Register<JsonObject, JsonObject>(__VA_ARGS__)

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 3

using namespace std;

namespace WPEFramework
{
    namespace {

        static Plugin::Metadata<Plugin::HdmiInput> metadata(
            // Version (Major, Minor, Patch)
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
            // Preconditions
            {},
            // Terminations
            {},
            // Controls
            {}
        );
    }

    namespace Plugin
    {
        SERVICE_REGISTRATION(HdmiInput, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

        HdmiInput* HdmiInput::_instance = nullptr;

        HdmiInput::HdmiInput()
        : PluginHost::JSONRPC()
        {
            HdmiInput::_instance = this;

            //InitializeIARM();

            CreateHandler({2});
	    m_client = nullptr;
	    m_tv_client = nullptr;

            registerMethod(HDMIINPUT_METHOD_GET_HDMI_INPUT_DEVICES, &HdmiInput::getHDMIInputDevicesWrapper, this);
            registerMethod(HDMIINPUT_METHOD_WRITE_EDID, &HdmiInput::writeEDIDWrapper, this);
            registerMethod(HDMIINPUT_METHOD_READ_EDID, &HdmiInput::readEDIDWrapper, this);
            registerMethod(HDMIINPUT_METHOD_READ_RAWHDMISPD, &HdmiInput::getRawHDMISPDWrapper, this);
            registerMethod(HDMIINPUT_METHOD_READ_HDMISPD, &HdmiInput::getHDMISPDWrapper, this);
            registerMethod(HDMIINPUT_METHOD_SET_EDID_VERSION, &HdmiInput::setEdidVersionWrapper, this);
            registerMethod(HDMIINPUT_METHOD_GET_EDID_VERSION, &HdmiInput::getEdidVersionWrapper, this);
            registerMethod(HDMIINPUT_METHOD_START_HDMI_INPUT, &HdmiInput::startHdmiInput, this);
            registerMethod(HDMIINPUT_METHOD_STOP_HDMI_INPUT, &HdmiInput::stopHdmiInput, this);
            registerMethod(HDMIINPUT_METHOD_SCALE_HDMI_INPUT, &HdmiInput::setVideoRectangleWrapper, this);

            registerMethod(HDMIINPUT_METHOD_SUPPORTED_GAME_FEATURES, &HdmiInput::getSupportedGameFeatures, this);
            registerMethod(HDMIINPUT_METHOD_GAME_FEATURE_STATUS, &HdmiInput::getHdmiGameFeatureStatusWrapper, this);
            registerMethod(HDMIINPUT_METHOD_GET_AV_LATENCY, &HdmiInput::getAVLatency, this);
            registerMethod(HDMIINPUT_METHOD_GET_LOW_LATENCY_MODE, &HdmiInput::getTVLowLatencyMode, this);

        }

        HdmiInput::~HdmiInput()
        {
		delete m_client;
		delete m_tv_client;
        }
	    
	const string HdmiInput::Initialize(PluginHost::IShell * /* service */)
	{
  	    HdmiInput::_instance = this;
	    InitializeIARM();

	    return (string());
	}

        void setResponseArray(JsonObject& response, const char* key, const vector<string>& items)
        {
            JsonArray arr;
            for(auto& i : items) arr.Add(JsonValue(i));

            response[key] = arr;

            string json;
            response.ToString(json);
        }

        void HdmiInput::Deinitialize(PluginHost::IShell* /* service */)
        {
            HdmiInput::_instance = nullptr;

            DeinitializeIARM();
        }

        void HdmiInput::InitializeIARM()
        {
            if (Utils::IARM::init())
            {
                IARM_Result_t res;
                IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_HOTPLUG, dsHdmiEventHandler) );
		IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_SIGNAL_STATUS, dsHdmiSignalStatusEventHandler) );
		IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_STATUS, dsHdmiStatusEventHandler) );
		IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_VIDEO_MODE_UPDATE, dsHdmiVideoModeEventHandler) );
		IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_ALLM_STATUS, dsHdmiGameFeatureStatusEventHandler) );
       		IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_AV_LATENCY, dsHdmiAVLatencyEventHandler) );
		IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_TVMGR_NAME,IARM_BUS_TVMGR_EVENT_TV_LOW_LATENCY_MODE, tvLowLatencyModeEventHandler) );

	    }
        }

        void HdmiInput::DeinitializeIARM()
        {
            if (Utils::IARM::isConnected())
            {
                IARM_Result_t res;
                IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_HOTPLUG) );
		IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_SIGNAL_STATUS) );
		IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_STATUS) );
		IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_VIDEO_MODE_UPDATE) );
		IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_ALLM_STATUS) );
           	IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_AV_LATENCY) );
		IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_TVMGR_NAME,IARM_BUS_TVMGR_EVENT_TV_LOW_LATENCY_MODE) );

	    }
        }

        uint32_t HdmiInput::startHdmiInput(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            returnIfParamNotFound(parameters, "portId");

            string sPortId = parameters["portId"].String();
            int portId = 0;
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
		    returnResponse(false);
            }
            bool success = true;
            try
            {
                device::HdmiInput::getInstance().selectPort(portId);
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(sPortId);
                success = false;
            }
            returnResponse(success);

        }

        uint32_t HdmiInput::stopHdmiInput(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();

            bool success = true;
            try
            {
                device::HdmiInput::getInstance().selectPort(-1);
            }
            catch (const device::Exception& err)
            {
                LOGWARN("HdmiInputService::stopHdmiInput Failed");
                success = false;
            }
            returnResponse(success);

        }

        uint32_t HdmiInput::setVideoRectangleWrapper(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();

            bool result = true;
            if (!parameters.HasLabel("x") && !parameters.HasLabel("y"))
            {
                result = false;
                response["message"] = "please specif coordinates (x,y)";
            }

            if (!parameters.HasLabel("w") && !parameters.HasLabel("h"))
            {
                result = false;
                response["message"] = "please specify window width and height (w,h)";
            }

            if (result)
            {
                int x = 0;
                int y = 0;
                int w = 0;
                int h = 0;

                try
		{
		       if (parameters.HasLabel("x"))
                       {
                           x = std::stoi(parameters["x"].String());
                       }
                       if (parameters.HasLabel("y"))
                       {
                           y = std::stoi(parameters["y"].String());
                       }
                       if (parameters.HasLabel("w"))
                       {
                           w = std::stoi(parameters["w"].String());
                       }
                       if (parameters.HasLabel("h"))
                       {
                           h = std::stoi(parameters["h"].String());
                       }
		}
                catch (const std::exception& err) {
		    LOGWARN("Invalid paramater X: %s,Y: %s, W: %s, H:%s ", parameters["x"].String().c_str(),parameters["y"].String().c_str(),parameters["w"].String().c_str(),parameters["h"].String().c_str());
		    returnResponse(false);
                }

                result = setVideoRectangle(x, y, w, h);
                if (false == result) {
                  LOGWARN("HdmiInputService::setVideoRectangle Failed");
                  response["message"] = "failed to set scale";
                }
            }

            returnResponse(result);

        }

        bool HdmiInput::setVideoRectangle(int x, int y, int width, int height)
        {
            bool ret = true;

            try
            {
                device::HdmiInput::getInstance().scaleVideo(x, y, width, height);
            }
            catch (const device::Exception& err)
            {
                ret = false;
            }

            return ret;
        }

        uint32_t HdmiInput::getHDMIInputDevicesWrapper(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();

            response["devices"] = getHDMIInputDevices();

            returnResponse(true);
        }

        uint32_t HdmiInput::writeEDIDWrapper(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();

            int deviceId;
            std::string message;

            if (parameters.HasLabel("deviceId") && parameters.HasLabel("message"))
            {
                getNumberParameter("deviceId", deviceId);
                message = parameters["message"].String();
            }
            else
            {
                LOGWARN("Required parameters are not passed");
                returnResponse(false);
            }


            writeEDID(deviceId, message);
            returnResponse(true);

        }

        uint32_t HdmiInput::readEDIDWrapper(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();

            string sPortId = parameters.HasLabel("deviceId") ? parameters["deviceId"].String() : "0";;
            int portId = 0;
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
		    returnResponse(false);
            }

            string edid = readEDID (portId);
            response["EDID"] = edid;
            if (edid.empty()) {
                returnResponse(false);
            }
            else {
                returnResponse(true);
            }
        }

        JsonArray HdmiInput::getHDMIInputDevices()
        {
            JsonArray list;
            try
            {
                int num = device::HdmiInput::getInstance().getNumberOfInputs();
                if (num > 0) {
                    int i = 0;
                    for (i = 0; i < num; i++) {
                        //Input ID is aleays 0-indexed, continuous number starting 0
                        JsonObject hash;
                        hash["id"] = i;
                        std::stringstream locator;
                        locator << "hdmiin://localhost/deviceid/" << i;
                        hash["locator"] = locator.str();
                        hash["connected"] = device::HdmiInput::getInstance().isPortConnected(i) ? "true" : "false";
                        LOGWARN("HdmiInputService::getHDMIInputDevices id %d, locator=[%s], connected=[%s]", i, hash["locator"].String().c_str(), hash["connected"].String().c_str());
                        list.Add(hash);
                    }
                }
            }
            catch (const std::exception& e)  {
                LOGWARN("HdmiInputService::getHDMIInputDevices Failed");
            }

            return list;
        }

        void HdmiInput::writeEDID(int deviceId, std::string message)
        {

        }

        std::string HdmiInput::readEDID(int iPort)
        {
            vector<uint8_t> edidVec({'u','n','k','n','o','w','n' });
            string edidbase64 = "";
            try
            {
                vector<uint8_t> edidVec2;
                device::HdmiInput::getInstance().getEDIDBytesInfo (iPort, edidVec2);
                edidVec = edidVec2;//edidVec must be "unknown" unless we successfully get to this line

                //convert to base64
                uint16_t size = min(edidVec.size(), (size_t)numeric_limits<uint16_t>::max());

                LOGWARN("HdmiInput::readEDID size:%u edidVec.size:%d", size, (int)edidVec.size());

                if(edidVec.size() > (size_t)numeric_limits<uint16_t>::max()) {
                    LOGERR("Size too large to use ToString base64 wpe api");
                    return edidbase64;
                }

                Core::ToString((uint8_t*)&edidVec[0], size, true, edidbase64);

            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
            }
            return edidbase64;
        }

        /**
         * @brief This function is used to translate HDMI input hotplug to 
         * deviceChanged event.
         *
         * @param[in] input Number of input port integer.
         * @param[in] connection status of input port integer.
         */
        void HdmiInput::hdmiInputHotplug( int input , int connect)
        {
            LOGWARN("hdmiInputHotplug [%d, %d]", input, connect);

            JsonObject params;
            params["devices"] = getHDMIInputDevices();
            sendNotify(HDMIINPUT_EVENT_ON_DEVICES_CHANGED, params);
        }

        /**
         * @brief This function is used to translate HDMI input signal change to
         * signalChanged event.
         *
         * @param[in] port HDMI In port id.
         * @param[in] signalStatus signal status of HDMI In port.
         */
        void HdmiInput::hdmiInputSignalChange( int port , int signalStatus)
        {
            LOGWARN("hdmiInputSignalStatus [%d, %d]", port, signalStatus);

            JsonObject params;
            params["id"] = port;
            std::stringstream locator;
            locator << "hdmiin://localhost/deviceid/" << port;
            params["locator"] = locator.str();

	    switch (signalStatus) {
		    case dsHDMI_IN_SIGNAL_STATUS_NOSIGNAL:
			    params["signalStatus"] = "noSignal";
			    break;

	            case dsHDMI_IN_SIGNAL_STATUS_UNSTABLE:
			    params["signalStatus"] = "unstableSignal";
			    break;

                    case dsHDMI_IN_SIGNAL_STATUS_NOTSUPPORTED:
                            params["signalStatus"] = "notSupportedSignal";
                            break;

                    case dsHDMI_IN_SIGNAL_STATUS_STABLE:
                            params["signalStatus"] = "stableSignal";
                            break;

	            default:
                            params["signalStatus"] = "none";
                            break;
            }

            sendNotify(HDMIINPUT_EVENT_ON_SIGNAL_CHANGED, params);
        }

        /**
         * @brief This function is used to translate HDMI input status change to
         * inputStatusChanged event.
         *
         * @param[in] port HDMI In port id.
         * @param[bool] isPresented HDMI In presentation started/stopped.
         */
        void HdmiInput::hdmiInputStatusChange( int port , bool isPresented)
        {
            LOGWARN("hdmiInputStatus [%d, %d]", port, isPresented);

            JsonObject params;
            params["id"] = port;
            std::stringstream locator;
            locator << "hdmiin://localhost/deviceid/" << port;
            params["locator"] = locator.str();

            if(isPresented) {
                params["status"] = "started";
            }
	    else {
                params["status"] = "stopped";
            }

            sendNotify(HDMIINPUT_EVENT_ON_STATUS_CHANGED, params);
        }

        /**
         * @brief This function is used to translate HDMI input video mode change to
         * videoStreamInfoUpdate event.
         *
         * @param[in] port HDMI In port id.
         * @param[dsVideoPortResolution_t] video resolution data
         */
        void HdmiInput::hdmiInputVideoModeUpdate( int port , dsVideoPortResolution_t resolution)
        {
            LOGWARN("hdmiInputVideoModeUpdate [%d]", port);

            JsonObject params;
            params["id"] = port;
            std::stringstream locator;
            locator << "hdmiin://localhost/deviceid/" << port;
            params["locator"] = locator.str();

	    switch(resolution.pixelResolution) {
		    case dsVIDEO_PIXELRES_720x480:
			params["width"] = 720;
			params["height"] = 480;
			break;

		    case dsVIDEO_PIXELRES_720x576:
                        params["width"] = 720;
                        params["height"] = 576;
                        break;

                    case dsVIDEO_PIXELRES_1280x720:
                        params["width"] = 1280;
                        params["height"] = 720;
                        break;

                    case dsVIDEO_PIXELRES_1920x1080:
                        params["width"] = 1920;
                        params["height"] = 1080;
                        break;

                    case dsVIDEO_PIXELRES_3840x2160:
                        params["width"] = 3840;
                        params["height"] = 2160;
                        break;

                    case dsVIDEO_PIXELRES_4096x2160:
                        params["width"] = 4096;
                        params["height"] = 2160;
                        break;

		    default:
                        params["width"] = 1920;
                        params["height"] = 1080;
                        break;
	    }

	    params["progressive"] = (!resolution.interlaced);

            switch(resolution.frameRate) {
                    case dsVIDEO_FRAMERATE_24:
                        params["frameRateN"] = 24000;
                        params["frameRateD"] = 1000;
                        break;

                    case dsVIDEO_FRAMERATE_25:
                        params["frameRateN"] = 25000;
                        params["frameRateD"] = 1000;
                        break;

                    case dsVIDEO_FRAMERATE_30:
                        params["frameRateN"] = 30000;
                        params["frameRateD"] = 1000;
                        break;

                    case dsVIDEO_FRAMERATE_50:
                        params["frameRateN"] = 50000;
                        params["frameRateD"] = 1000;
                        break;

                    case dsVIDEO_FRAMERATE_60:
                        params["frameRateN"] = 60000;
                        params["frameRateD"] = 1000;
                        break;

                    case dsVIDEO_FRAMERATE_23dot98:
                        params["frameRateN"] = 24000;
                        params["frameRateD"] = 1001;
                        break;

                    case dsVIDEO_FRAMERATE_29dot97:
                        params["frameRateN"] = 30000;
                        params["frameRateD"] = 1001;
                        break;

                    case dsVIDEO_FRAMERATE_59dot94:
                        params["frameRateN"] = 60000;
                        params["frameRateD"] = 1001;
                        break;

                    default:
                        params["frameRateN"] = 60000;
                        params["frameRateD"] = 1000;
                        break;
            }

            sendNotify(HDMIINPUT_EVENT_ON_VIDEO_MODE_UPDATED, params);
        }

        void HdmiInput::dsHdmiEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
            if(!HdmiInput::_instance)
                return;

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_HOTPLUG == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int hdmiin_hotplug_port = eventData->data.hdmi_in_connect.port;
                int hdmiin_hotplug_conn = eventData->data.hdmi_in_connect.isPortConnected;
                LOGWARN("Received IARM_BUS_DSMGR_EVENT_HDMI_IN_HOTPLUG  event data:%d", hdmiin_hotplug_port);

                HdmiInput::_instance->hdmiInputHotplug(hdmiin_hotplug_port, hdmiin_hotplug_conn ? HDMI_HOT_PLUG_EVENT_CONNECTED : HDMI_HOT_PLUG_EVENT_DISCONNECTED);
            }
        }

        void HdmiInput::dsHdmiSignalStatusEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
            if(!HdmiInput::_instance)
                return;

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_SIGNAL_STATUS == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int hdmi_in_port = eventData->data.hdmi_in_sig_status.port;
                int hdmi_in_signal_status = eventData->data.hdmi_in_sig_status.status;
                LOGWARN("Received IARM_BUS_DSMGR_EVENT_HDMI_IN_SIGNAL_STATUS  event  port: %d, signal status: %d", hdmi_in_port,hdmi_in_signal_status);

                HdmiInput::_instance->hdmiInputSignalChange(hdmi_in_port, hdmi_in_signal_status);

            }
        }

        void HdmiInput::dsHdmiStatusEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
            if(!HdmiInput::_instance)
                return;

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_STATUS == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int hdmi_in_port = eventData->data.hdmi_in_status.port;
                bool hdmi_in_status = eventData->data.hdmi_in_status.isPresented;
                LOGWARN("Received IARM_BUS_DSMGR_EVENT_HDMI_IN_STATUS  event  port: %d, started: %d", hdmi_in_port,hdmi_in_status);

                HdmiInput::_instance->hdmiInputStatusChange(hdmi_in_port, hdmi_in_status);

            }
        }

        void HdmiInput::dsHdmiVideoModeEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
            if(!HdmiInput::_instance)
                return;

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_VIDEO_MODE_UPDATE == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int hdmi_in_port = eventData->data.hdmi_in_video_mode.port;
		dsVideoPortResolution_t resolution;
		resolution.pixelResolution =  eventData->data.hdmi_in_video_mode.resolution.pixelResolution;
		resolution.interlaced =  eventData->data.hdmi_in_video_mode.resolution.interlaced;
		resolution.frameRate =  eventData->data.hdmi_in_video_mode.resolution.frameRate;
                LOGWARN("Received IARM_BUS_DSMGR_EVENT_HDMI_IN_VIDEO_MODE_UPDATE  event  port: %d, pixelResolution: %d, interlaced : %d, frameRate: %d \n", hdmi_in_port,resolution.pixelResolution, resolution.interlaced, resolution.frameRate);

                HdmiInput::_instance->hdmiInputVideoModeUpdate(hdmi_in_port, resolution);

            }
        }

        void HdmiInput::dsHdmiGameFeatureStatusEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
            if(!HdmiInput::_instance)
                return;

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_ALLM_STATUS == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int hdmi_in_port = eventData->data.hdmi_in_allm_mode.port;
                bool allm_mode = eventData->data.hdmi_in_allm_mode.allm_mode;
                LOGWARN("Received IARM_BUS_DSMGR_EVENT_HDMI_IN_ALLM_STATUS  event  port: %d, ALLM Mode: %d", hdmi_in_port,allm_mode);

                HdmiInput::_instance->hdmiInputALLMChange(hdmi_in_port, allm_mode);
            }
        }

        void HdmiInput::hdmiInputALLMChange( int port , bool allm_mode)
        {
            JsonObject params;
            params["id"] = port;
            params["gameFeature"] = "ALLM";
            params["mode"] = allm_mode;

            sendNotify(HDMIINPUT_EVENT_ON_GAME_FEATURE_STATUS_CHANGED, params);
        }

        uint32_t HdmiInput::getSupportedGameFeatures(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            vector<string> supportedFeatures;
            try
            {
                device::HdmiInput::getInstance().getSupportedGameFeatures (supportedFeatures);
                for (size_t i = 0; i < supportedFeatures.size(); i++)
                {
                    LOGINFO("Supported Game Feature [%d]:  %s\n",(int)i,supportedFeatures.at(i).c_str());
                }
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION0();
            }

            if (supportedFeatures.empty()) {
                returnResponse(false);
            }
            else {
                setResponseArray(response, "supportedGameFeatures", supportedFeatures);
                returnResponse(true);
            }
        }
	void HdmiInput::tvLowLatencyChange(bool low_latency)
	{

           PluginHost::IShell::state state;
            if ((getServiceState(m_service, HDMICECSINK_CALLSIGN, state) == Core::ERROR_NONE) && (state == PluginHost::IShell::state::ACTIVATED)) {
                LOGINFO("%s is active", HDMICECSINK_CALLSIGN);

                getHdmiCecSinkPlugin();
                if (!m_client) {
                    LOGERR("HdmiCecSink Initialisation failed\n");
                }
                else {
                    JsonObject hdmiCecSinkResult;
                    JsonObject param;

                    param["lowLatencyMode"] = low_latency;
                    m_client->Invoke<JsonObject, JsonObject>(2000, "Report_LowLatencyMode", param, hdmiCecSinkResult);
                    if (!hdmiCecSinkResult["success"].Boolean()) {
                        LOGERR("HdmiCecSink Plugin returned error\n");
                    }
                }
            }
            else {
                LOGERR("HdmiCecSink plugin not ready\n");
            }
	}

        void  HdmiInput::dsHdmiAVLatencyEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
            if(!HdmiInput::_instance)
                return;

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_AV_LATENCY == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int audio_output_delay = eventData->data.hdmi_in_av_latency.audio_output_delay;
                int video_latency= eventData->data.hdmi_in_av_latency.video_latency;

                HdmiInput::_instance->hdmiInAVLatencyChange(audio_output_delay,video_latency);
            }
        }
 
void HdmiInput::hdmiInAVLatencyChange(dsAudioVideoLatency *param)
        {


           PluginHost::IShell::state state;
            if ((getServiceState(m_service, HDMICECSINK_CALLSIGN, state) == Core::ERROR_NONE) && (state == PluginHost::IShell::state::ACTIVATED)) {
                LOGINFO("%s is active", HDMICECSINK_CALLSIGN);

                getHdmiCecSinkPlugin();
                if (!m_client) {
                    LOGERR("HdmiCecSink Initialisation failed\n");
                }
                else {
                    JsonObject hdmiCecSinkResult;
                    JsonObject param;

					param["audio_output_delay"] = param->audio_output_delay;
					param["video_latency"] = param->video_latency;
                    m_client->Invoke<JsonObject, JsonObject>(2000, "Report_AudioVideoLatency", param, hdmiCecSinkResult);
                    if (!hdmiCecSinkResult["success"].Boolean()) {
			success = false;
                        LOGERR("HdmiCecSink Plugin returned error\n");
                    }
                }
            }
	    else {
		success = false;
                LOGERR("HdmiCecSink plugin not ready\n");
            }

            return success;
	}

        void  HdmiInput::tvLowLatencyModeEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
            if(!HdmiInput::_instance)
                return;

            if (IARM_BUS_TVMGR_EVENT_TV_LOW_LATENCY_MODE == eventId)
            {
		
                bool lowLatencyMode =(bool)data;
                HdmiInput::_instance->tvLowLatencyChange(lowLatencyMode);
            }
        }
	    uint32_t HdmiInput::getServiceState(PluginHost::IShell* shell, const string& callsign, PluginHost::IShell::state& state)
            {
                uint32_t result;
                auto interface = shell->QueryInterfaceByCallsign<PluginHost::IShell>(callsign);
                if (interface == nullptr) {
                    result = Core::ERROR_UNAVAILABLE;
                    std::cout << "no IShell for " << callsign << std::endl;
                } else {
                    result = Core::ERROR_NONE;
                    state = interface->State();
                    std::cout << "IShell state " << state << " for " << callsign << std::endl;
                    interface->Release();
                }
                return result;
            }

	 void HdmiInput::getControlSettingsPlugin()
        {
            if(m_tv_client == nullptr)
            {
                string token;

                // TODO: use interfaces and remove token
                auto security = m_service->QueryInterfaceByCallsign<PluginHost::IAuthenticate>("SecurityAgent");
                if (security != nullptr) {
                    string payload = "http://localhost";
                    if (security->CreateToken(
                            static_cast<uint16_t>(payload.length()),
                            reinterpret_cast<const uint8_t*>(payload.c_str()),
                            token)
                        == Core::ERROR_NONE) {
                        std::cout << "HdmiInput got security token" << std::endl;
                    } else {
                        std::cout << "HdmiInput failed to get security token" << std::endl;
                    }
                    security->Release();
                } else {
                    std::cout << "No security agent" << std::endl;
                }

                string query = "token=" + token;
                Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), (_T("127.0.0.1:9998")));
                m_tv_client = new WPEFramework::JSONRPC::LinkType<Core::JSON::IElement>(_T(HDMICECSINK_CALLSIGN_VER), (_T(HDMICECSINK_CALLSIGN_VER)), false, query);
                LOGINFO("HdmiInput getControlSettingsPlugin init m_tv_client\n");
            }
        }


	uint32_t HdmiInput::getAVLatency(const JsonObject& parameters, JsonObject& response)
	{
		int audio_output_delay = 0;
		int video_latency = 0;
		
		try
		{
			device::HdmiInput::getInstance().getHdmiDAL_AudioVideoLatency(audio_output_delay,video_latency);
			LOGWARN("HdmiInput::getHdmiDAL_AudioVideoLatency Audio Latency: %d, Video Latency: %d\n", audio_output_delay,video_latency);
			response["AudioLatency"] = audio_output_delay;
			response["VideoLatency"] = video_latency;
			returnResponse(true);
		}
         	catch(const device::Exception& err)
	        {
			std::string api = "getHdmiDAL_AudioVideoLatency";
                        LOG_DEVICE_EXCEPTION1(std::string(api));
			response["message"] = "Invalid response from getHdmiDAL_AudioVideoLatency";
			returnResponse(false);
            	}

	}
        uint32_t HdmiInput::getTVLowLatencyMode(const JsonObject& parameters, JsonObject& response)
   	{
            PluginHost::IShell::state state;
            LOGINFOMETHOD();
            if ((getServiceState(m_service, TVSETTINGS_CALLSIGN, state) == Core::ERROR_NONE) && (state == PluginHost::IShell::state::ACTIV
			{
                LOGINFO("%s is active", TVSETTINGS_CALLSIGN);
	
		getControlSettingsPlugin();
		if(!m_tv_client)
		{
			LOGERR("TV SEttings Initialisation failed\n");		
		}
		else{

				JsonObject tvsettingsResult;
				JsonObject param;
				
				
				 
				m_tv_client->Invoke<JsonObject, JsonObject>(2000, "getLowLatencyMode", param, tvsettingsResult);

				 if(param["lowLatencyMode"].Boolean())
				 {
					//lowLatencyMode = true;
					response["lowLatencyMode"] = true;
					returnResponse(true);
				 	 LOGINFO("Low Latency Mode is enabled\n");
				 }
				 else{
					 //lowLatencyMode = false;
					  response["lowLatencyMode"] = false;
					  returnResponse(true);
						LOGINFO("Low Latency Mode is disabled\n");
				 }
				 if (!tvsettingsResult["success"].Boolean()) {
                        		LOGERR("control settings Plugin returned error\n");
					returnResponse(false);
                                  }
					
				}
			}		
			else
			{
                    LOGERR("control settings Plugin not ready\n");
		  //  lowLatencyMode = false;
		    returnResponse(false);
			}
	
		returnResponse(true);
	}


        uint32_t HdmiInput::getHdmiGameFeatureStatusWrapper(const JsonObject& parameters, JsonObject& response)
        {
            string sPortId = parameters["portId"].String();
            string sGameFeature = parameters["gameFeature"].String();
            int portId = 0;

            LOGINFOMETHOD();
            returnIfParamNotFound(parameters, "portId");
            returnIfParamNotFound(parameters, "gameFeature");
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
		    returnResponse(false);
            }

	    if (strcmp (sGameFeature.c_str(), "ALLM") == 0)
            {
                bool allm = getHdmiALLMStatus(portId);
                LOGWARN("HdmiInput::getHdmiGameFeatureStatusWrapper ALLM MODE:%d", allm);
                response["mode"] = allm;
            }
	    else
	    {
		LOGWARN("HdmiInput::getHdmiGameFeatureStatusWrapper Mode is not supported. Supported mode: ALLM");
		response["message"] = "Mode is not supported. Supported mode: ALLM";
		returnResponse(false);
	    }
            returnResponse(true);
        }

        bool HdmiInput::getHdmiALLMStatus(int iPort)
        {
            bool allm = false;

            try
            {
                device::HdmiInput::getInstance().getHdmiALLMStatus (iPort, &allm);
                LOGWARN("HdmiInput::getHdmiALLMStatus ALLM MODE: %d", allm);
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
            }
            return allm;
        }

        uint32_t HdmiInput::getRawHDMISPDWrapper(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
           returnIfParamNotFound(parameters, "portId");

           string sPortId = parameters["portId"].String();
            int portId = 0;
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
		    returnResponse(false);
            }

            string spdInfo = getRawHDMISPD (portId);
            response["HDMISPD"] = spdInfo;
            if (spdInfo.empty()) {
                returnResponse(false);
            }
            else {
                returnResponse(true);
            }
        }

        uint32_t HdmiInput::getHDMISPDWrapper(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
           returnIfParamNotFound(parameters, "portId");

           string sPortId = parameters["portId"].String();
            int portId = 0;
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
		    returnResponse(false);
            }

            string spdInfo = getHDMISPD (portId);
            response["HDMISPD"] = spdInfo;
            if (spdInfo.empty()) {
                returnResponse(false);
            }
            else {
                returnResponse(true);
            }
        }

        std::string HdmiInput::getRawHDMISPD(int iPort)
        {
                LOGINFO("HdmiInput::getHDMISPDInfo");
                vector<uint8_t> spdVect({'u','n','k','n','o','w','n' });
                std::string spdbase64 = "";
            try
            {
                LOGWARN("HdmiInput::getHDMISPDInfo");
                vector<uint8_t> spdVect2;
                device::HdmiInput::getInstance().getHDMISPDInfo(iPort, spdVect2);
                spdVect = spdVect2;//edidVec must be "unknown" unless we successfully get to this line

                //convert to base64
                uint16_t size = min(spdVect.size(), (size_t)numeric_limits<uint16_t>::max());

                LOGWARN("HdmiInput::getHDMISPD size:%u spdVec.size:%d", size, (int)spdVect.size());

                if(spdVect.size() > (size_t)numeric_limits<uint16_t>::max()) {
                    LOGERR("Size too large to use ToString base64 wpe api");
                    return spdbase64;
                }

                LOGINFO("------------getHDMISPD: ");
                for (unsigned int itr =0; itr < spdVect.size(); itr++) {
                  LOGINFO("%02X ", spdVect[itr]);
                }
                Core::ToString((uint8_t*)&spdVect[0], size, false, spdbase64);

            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
            }
            return spdbase64;
        }

        std::string HdmiInput::getHDMISPD(int iPort)
        {
                LOGINFO("HdmiInput::getHDMISPDInfo");
                vector<uint8_t> spdVect({'u','n','k','n','o','w','n' });
                std::string spdbase64 = "";
            try
            {
                LOGWARN("HdmiInput::getHDMISPDInfo");
                vector<uint8_t> spdVect2;
                device::HdmiInput::getInstance().getHDMISPDInfo(iPort, spdVect2);
                spdVect = spdVect2;//edidVec must be "unknown" unless we successfully get to this line

                //convert to base64
                uint16_t size = min(spdVect.size(), (size_t)numeric_limits<uint16_t>::max());

                LOGWARN("HdmiInput::getHDMISPD size:%u spdVec.size:%d", size, (int)spdVect.size());

                if(spdVect.size() > (size_t)numeric_limits<uint16_t>::max()) {
                    LOGERR("Size too large to use ToString base64 wpe api");
                    return spdbase64;
                }

                LOGINFO("------------getHDMISPD: ");
                for (unsigned int itr =0; itr < spdVect.size(); itr++) {
                  LOGINFO("%02X ", spdVect[itr]);
                }
               if (spdVect.size() > 0) {
                struct dsSpd_infoframe_st pre;
                memcpy(&pre,spdVect.data(),sizeof(struct dsSpd_infoframe_st));

              char str[200] = {0};
               sprintf(str, "Packet Type:%02X,Version:%u,Length:%u,vendor name:%s,product des:%s,source info:%02X"
,pre.pkttype,pre.version,pre.length,pre.vendor_name,pre.product_des,pre.source_info);
              spdbase64 = str;
               }
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
            }
            return spdbase64;
        }

        uint32_t HdmiInput::setEdidVersionWrapper(const JsonObject& parameters, JsonObject& response)
        {
            int portId = 0;

            LOGINFOMETHOD();
            returnIfParamNotFound(parameters, "portId");
            returnIfParamNotFound(parameters, "edidVersion");
            string sPortId = parameters["portId"].String();
            string sVersion = parameters["edidVersion"].String();
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
                    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
		    returnResponse(false);
            }

            int edidVer = -1;
            if (strcmp (sVersion.c_str(), "HDMI1.4") == 0) {
                edidVer = HDMI_EDID_VER_14;
            }
            else if (strcmp (sVersion.c_str(), "HDMI2.0") == 0) {
                edidVer = HDMI_EDID_VER_20;
            }

            if (edidVer < 0) {
                returnResponse(false);
            }
            bool result = setEdidVersion (portId, edidVer);
            if (result == false) {
                returnResponse(false);
            }
            else {
                returnResponse(true);
            }
        }

        int HdmiInput::setEdidVersion(int iPort, int iEdidVer)
        {
            bool ret = true;
            try
            {
                device::HdmiInput::getInstance().setEdidVersion (iPort, iEdidVer);
                LOGWARN("HdmiInput::setEdidVersion EDID Version:%d", iEdidVer);
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
                ret = false;
            }
            return ret;
        }

        uint32_t HdmiInput::getEdidVersionWrapper(const JsonObject& parameters, JsonObject& response)
        {
            string sPortId = parameters["portId"].String();
            int portId = 0;

            LOGINFOMETHOD();
            returnIfParamNotFound(parameters, "portId");
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
		    returnResponse(false);
            }

            int edidVer = getEdidVersion (portId);
            switch (edidVer)
            {
                case HDMI_EDID_VER_14:
                    response["edidVersion"] = "HDMI1.4";
                    break;
                case HDMI_EDID_VER_20:
                    response["edidVersion"] = "HDMI2.0";
                    break;
            }

            if (edidVer < 0) {
                returnResponse(false);
            }
            else {
                returnResponse(true);
            }
        }

        int HdmiInput::getEdidVersion(int iPort)
        {
            int edidVersion = -1;

            try
            {
                device::HdmiInput::getInstance().getEdidVersion (iPort, &edidVersion);
                LOGWARN("HdmiInput::getEdidVersion EDID Version:%d", edidVersion);
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
            }
            return edidVersion;
        }

    } // namespace Plugin
} // namespace WPEFramework
