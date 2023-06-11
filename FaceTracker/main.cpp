// Copyright (c) 2023-2023, A3
//
// License: MIT

#include <iostream>
#include <fstream>

#include <openxr/openxr.h>

#include <osc\OscOutboundPacketStream.h>
#include <ip\UdpSocket.h>

#define CHK_XR(r,e) if(XR_FAILED(r)) { std::cout << e << ":" << r << "\n";return -1;}

//UselessGUI
#include "hello_xr/common.h"
#include "hello_xr/options.h"
#include "hello_xr/pch.h"
#include "hello_xr/platformdata.h"
#include "hello_xr/platformplugin.h"
#include "hello_xr/graphicsplugin.h"

int main(int argc, char** argv)
{
	std::cout << "Face Tracker OSC\n(c)A3\nhttps://twitter.com/A3_yuu\n";

	//arg
	const char *ip = "127.0.0.1";
	int port = 9000;
	int sleepTime = 33;

	switch (argc > 4 ? 4 : argc) {
	case 4:
		sleepTime = atoi(argv[3]);
	case 3:
		port = atoi(argv[2]);
	case 2:
		ip = argv[1];
	default:
		break;
	}

	//file
	float rate[XR_FACE_EXPRESSION_COUNT_FB];
	std::string address[XR_FACE_EXPRESSION_COUNT_FB];
	{
		std::ifstream ifs("rate.txt");
		for (int i = 0; i < XR_FACE_EXPRESSION_COUNT_FB; i++) {
			std::string str;
			if (!std::getline(ifs, str)) {
				std::cout << "No enable.txt";
				return -1;
			}
			rate[i] = atof(str.c_str());
		}
	}
	{
		std::ifstream ifs("address.txt");
		for (int i = 0; i < XR_FACE_EXPRESSION_COUNT_FB; i++) {
			if (!std::getline(ifs, address[i])) {
				std::cout << "No address.txt";
				return -1;
			}
		}
	}

	//OpenXR
	//Create instance
	XrApplicationInfo applicationInfo{
		"A3 Face Tracker",
		1,//app ver
		"",
		0,
		XR_CURRENT_API_VERSION
	};
	const int extensionsCount = 2;
	const char* extensions[] = {
	  "XR_FB_face_tracking",
	  "XR_KHR_D3D11_enable",//UselessGUI
	};
	XrInstanceCreateInfo instanceCreateInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
	instanceCreateInfo.next = NULL;
	instanceCreateInfo.createFlags = 0;
	instanceCreateInfo.applicationInfo = applicationInfo;
	instanceCreateInfo.enabledApiLayerCount = 0;
	instanceCreateInfo.enabledApiLayerNames = NULL;
	instanceCreateInfo.enabledExtensionCount = extensionsCount;
	instanceCreateInfo.enabledExtensionNames = extensions;

	XrInstance instance;
	CHK_XR(xrCreateInstance(&instanceCreateInfo, &instance), "xrCreateInstance");

	//Get systemId
	XrSystemGetInfo systemGetInfo = { XR_TYPE_SYSTEM_GET_INFO };
	systemGetInfo.next = NULL;
	systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	XrSystemId systemId;
	CHK_XR(xrGetSystem(instance, &systemGetInfo, &systemId), "xrGetSystem");

	//Create session
	XrSessionCreateInfo sessionCreateInfo = { XR_TYPE_SESSION_CREATE_INFO };
	//UselessGUI
	std::shared_ptr<Options> options = std::make_shared<Options>();
	options->GraphicsPlugin = "D3D11";
	std::shared_ptr<PlatformData> data = std::make_shared<PlatformData>();
	std::shared_ptr<IPlatformPlugin> platformPlugin = CreatePlatformPlugin(options, data);
	std::shared_ptr<IGraphicsPlugin> graphicsPlugin = CreateGraphicsPlugin(options, platformPlugin);
	graphicsPlugin->InitializeDevice(instance, systemId);
	//UselessGUI
	sessionCreateInfo.next = graphicsPlugin->GetGraphicsBinding();
	sessionCreateInfo.createFlags = 0;
	sessionCreateInfo.systemId = systemId;

	XrSession session;
	CHK_XR(xrCreateSession(instance, &sessionCreateInfo, &session), "xrCreateSession");

	//Face
	PFN_xrCreateFaceTrackerFB pfnCreateFaceTrackerFB;
	CHK_XR(xrGetInstanceProcAddr(instance, "xrCreateFaceTrackerFB",reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateFaceTrackerFB)),"xrGetInstanceProcAddr");
	XrFaceTrackerFB faceTracker = {};
	{
		XrFaceTrackerCreateInfoFB createInfo{ XR_TYPE_FACE_TRACKER_CREATE_INFO_FB };
		createInfo.faceExpressionSet = XR_FACE_EXPRESSION_SET_DEFAULT_FB;
		CHK_XR(pfnCreateFaceTrackerFB(session, &createInfo, &faceTracker),"pfnCreateFaceTrackerFB");
	}
	float weights[XR_FACE_EXPRESSION_COUNT_FB];
	float confidences[XR_FACE_CONFIDENCE_COUNT_FB];
	XrFaceExpressionWeightsFB expressionWeights{ XR_TYPE_FACE_EXPRESSION_WEIGHTS_FB };
	expressionWeights.weightCount = XR_FACE_EXPRESSION_COUNT_FB;
	expressionWeights.weights = weights;
	expressionWeights.confidenceCount = XR_FACE_CONFIDENCE_COUNT_FB;
	expressionWeights.confidences = confidences;
	PFN_xrGetFaceExpressionWeightsFB pfnGetFaceExpressionWeights;
	CHK_XR(xrGetInstanceProcAddr(instance, "xrGetFaceExpressionWeightsFB",reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetFaceExpressionWeights)),"xrGetInstanceProcAddr");
	
	//OSC
	UdpTransmitSocket transmitSocket(IpEndpointName(ip, port));

	std::cout << "OK!\n";
	//Loop
	while (1) {
		XrFrameState frameState; // previously returned from xrWaitFrame
		XrFaceExpressionInfoFB expressionInfo{ XR_TYPE_FACE_EXPRESSION_INFO_FB };
		pfnGetFaceExpressionWeights(faceTracker, &expressionInfo, &expressionWeights);
		if (expressionWeights.status.isValid) {
			char buffer[6144];
			osc::OutboundPacketStream p(buffer, 6144);
			p << osc::BeginBundleImmediate;
			for (int i = 0; i < XR_FACE_EXPRESSION_COUNT_FB; i++) {
				p << osc::BeginMessage(address[i].c_str()) << weights[i] * rate[i] << osc::EndMessage;
			}
			p << osc::EndBundle;
			transmitSocket.Send(p.Data(), p.Size());
		}
		Sleep(sleepTime);
	}

	return 0;
}