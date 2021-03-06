#pragma once
#include "envision/envpch.h"

namespace env
{
	struct InstanceBufferElementData
	{
		Float3 Position;
		UINT ID;
		Float3 ForwardDirection;
		UINT MaterialIndex;
		Float3 UpDirection;
		float Pad;
		Float4x4 WorldMatrix;
	};

	struct CameraBufferData {
		Float3 Position;
		float FieldOfView;
		Float3 ForwardDirection;
		float DistanceNearPlane;
		Float3 UpDirection;
		float DistanceFarPlane;
		Float4x4 ViewMatrix;
		Float4x4 ProjectionMatrix;
		Float4x4 ViewProjectionMatrix;
	};

	struct MaterialBufferInstanceData
	{
		Float3 AmbientFactor;
		int AmbientMapIndex; // -1 if no map exist
		Float3 DiffuseFactor;
		int DiffuseMapIndex; // -1 if no map exist
		Float3 SpecularFactor;
		int SpecularMapIndex; // -1 if no map exist
		float Shininess;
		int MaterialID;
		Float2 Padding;
	};
}