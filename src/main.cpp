// Copyright (c) 2023-2023, A3
//
// License: MIT

#include <stdio.h>
#include <iostream>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <osc\OscReceivedElements.h>
#include <osc\OscPacketListener.h>
#include <osc\OscOutboundPacketStream.h>
#include <ip\UdpSocket.h>
#include <ip\IpEndpointName.h>

#define CHK_XR(r,e) if(XR_FAILED(r)) { printf("%s:%d",e,r);}
const float PI = 3.14159265358979;

//UselessGUI
#include "hello_xr/common.h"
#include "hello_xr/options.h"
#include "hello_xr/pch.h"
#include "hello_xr/platformdata.h"
#include "hello_xr/platformplugin.h"
#include "hello_xr/graphicsplugin.h"


XrVector3f euler(XrQuaternionf q) {
	auto sy = 2 * q.x * q.z + 2 * q.y * q.w;
	auto unlocked = std::abs(sy) < 0.99999f;
	return XrVector3f{
		unlocked ? std::atan2(-(2 * q.y * q.z - 2 * q.x * q.w), 2 * q.w * q.w + 2 * q.z * q.z - 1)
		: std::atan2(2 * q.y * q.z + 2 * q.x * q.w, 2 * q.w * q.w + 2 * q.y * q.y - 1),
		std::asin(sy),
		unlocked ? std::atan2(-(2 * q.x * q.y - 2 * q.z * q.w), 2 * q.w * q.w + 2 * q.x * q.x - 1) : 0
	};
}

int main()
{
	std::cout << "Eye Tracker OSC\n(c)A3\nhttps://twitter.com/A3_yuu\n";

	XrApplicationInfo t_ApplicationInfo{
		"A3 Eye Tracker",
		1,//app ver
		"",
		0,
		XR_CURRENT_API_VERSION
	};
	const int extensionsCount = 3;
	const char* extensions[] = {
	  "XR_FB_eye_tracking_social",
	  "XR_FB_face_tracking",
	  "XR_KHR_D3D11_enable",//UselessGUI
	};

	XrInstanceCreateInfo t_InstanceCreateInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
	t_InstanceCreateInfo.next = NULL;
	t_InstanceCreateInfo.createFlags = 0;
	t_InstanceCreateInfo.applicationInfo = t_ApplicationInfo;
	t_InstanceCreateInfo.enabledApiLayerCount = 0;
	t_InstanceCreateInfo.enabledApiLayerNames = NULL;
	t_InstanceCreateInfo.enabledExtensionCount = extensionsCount;
	t_InstanceCreateInfo.enabledExtensionNames = extensions;

	XrInstance instance;
	CHK_XR(xrCreateInstance(&t_InstanceCreateInfo, &instance), "xrCreateInstance");

	XrSystemGetInfo systemGetInfo = { XR_TYPE_SYSTEM_GET_INFO };
	systemGetInfo.next = NULL;
	systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	XrSystemId systemId;

	CHK_XR(xrGetSystem(instance, &systemGetInfo, &systemId), "xrGetSystem");

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


	//Eye
	PFN_xrCreateEyeTrackerFB pfnCreateEyeTrackerFB;
	xrGetInstanceProcAddr(instance, "xrCreateEyeTrackerFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateEyeTrackerFB));
	XrEyeTrackerFB eyeTracker{};
	{
		XrEyeTrackerCreateInfoFB createInfo{ XR_TYPE_EYE_TRACKER_CREATE_INFO_FB };
		CHK_XR(pfnCreateEyeTrackerFB(session, &createInfo, &eyeTracker), "pfnCreateEyeTrackerFB");
	}
	XrEyeGazesFB eyeGazes{ XR_TYPE_EYE_GAZES_FB };
	eyeGazes.next = nullptr;
	PFN_xrGetEyeGazesFB pfnGetEyeGazesFB;
	CHK_XR(xrGetInstanceProcAddr(instance, "xrGetEyeGazesFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetEyeGazesFB)),"xrGetInstanceProcAddr");
	XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	XrPosef t{};
	t.orientation.w = 1;
	referenceSpaceCreateInfo.poseInReferenceSpace = t;
	referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	XrSpace xrSpace;
	xrCreateReferenceSpace(session, &referenceSpaceCreateInfo, &xrSpace);

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
	
	//
	const std::string ipAddress = "127.0.0.1";
	const int port = 9000;
	UdpTransmitSocket transmitSocket(IpEndpointName(ipAddress.c_str(), port));
	//Loop
	while (1) {
		float dataEyesClosedAmountL, dataEyesClosedAmountR;
		float dataLeftPitch, dataLeftYaw, dataRightPitch, dataRightYaw;
		//Eye
		XrEyeGazesInfoFB gazesInfo{ XR_TYPE_EYE_GAZES_INFO_FB };
		gazesInfo.baseSpace = xrSpace;
		pfnGetEyeGazesFB(eyeTracker, &gazesInfo, &eyeGazes);
		if (eyeGazes.gaze[XR_EYE_POSITION_LEFT_FB].isValid) {
			XrVector3f orientationL = euler(eyeGazes.gaze[XR_EYE_POSITION_LEFT_FB].gazePose.orientation);
			XrVector3f orientationR = euler(eyeGazes.gaze[XR_EYE_POSITION_RIGHT_FB].gazePose.orientation);
			dataLeftPitch = -orientationL.x / PI * 180;
			dataLeftYaw = -orientationL.y / PI * 180;
			dataRightPitch = -orientationR.x / PI * 180;
			dataRightYaw = -orientationR.y / PI * 180;
		}
		//Face
		XrFaceExpressionInfoFB expressionInfo{ XR_TYPE_FACE_EXPRESSION_INFO_FB };
		pfnGetFaceExpressionWeights(faceTracker, &expressionInfo, &expressionWeights);
		if (expressionWeights.status.isValid) {
			dataEyesClosedAmountL = weights[XR_FACE_EXPRESSION_EYES_CLOSED_L_FB];
			dataEyesClosedAmountR = weights[XR_FACE_EXPRESSION_EYES_CLOSED_R_FB];
		}
		//OSC
		char buffer[6144];
		osc::OutboundPacketStream p(buffer, 6144);
		p << osc::BeginBundleImmediate
			<< osc::BeginMessage("/tracking/eye/EyesClosedAmount") 
			<< dataEyesClosedAmountL + dataEyesClosedAmountR
			<< osc::EndMessage
			<< osc::BeginMessage("/tracking/eye/LeftRightPitchYaw")
			<< dataLeftPitch
			<< dataLeftYaw
			<< dataRightPitch
			<< dataRightYaw
			<< osc::EndMessage
			<< osc::EndBundle;
		transmitSocket.Send(p.Data(), p.Size());
		Sleep(33);
	}

	return 0;
}