#include "envision/envpch.h"
#include "envision/graphics/Renderer.h"
#include "envision/graphics/AssetManager.h"
#include "envision/resource/ResourceManager.h"

#include "DirectXMath.h"

env::Renderer* env::Renderer::s_instance = nullptr;

env::Renderer* env::Renderer::Initialize(IDGenerator& commonIDGenerator)
{
	if (!s_instance)
		s_instance = new Renderer(commonIDGenerator);
	return s_instance;
}

env::Renderer* env::Renderer::Get()
{
	assert(s_instance);
	return s_instance;
}

void env::Renderer::Finalize()
{
	delete s_instance;
	s_instance = nullptr;
}

env::Renderer::Renderer(env::IDGenerator& commonIDGenerator) :
	m_commonIDGenerator(commonIDGenerator)
{
	m_directList = GPU::CreateDirectCommandList();

	m_pipelineState = ResourceManager::Get()->CreatePipelineState("PipelineState",
		{
			{ ShaderStage::Vertex, ShaderModel::V5_0, "shader.hlsl", "VS_main" },
			{ ShaderStage::Pixel, ShaderModel::V5_0, "shader.hlsl", "PS_main" }
		},
		true,
		{
			{ ParameterType::CBV, ShaderStage::Vertex, D3D12_ROOT_DESCRIPTOR{0, 0} },
			{ ParameterType::CBV, ShaderStage::Vertex, D3D12_ROOT_DESCRIPTOR{1, 0} },
		});

	m_intermediateTarget = ResourceManager::Get()->CreateTexture2D("IntermediateTarget",
		1200,
		800,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		TextureBindType::RenderTarget);

	m_depthStencil = ResourceManager::Get()->CreateTexture2D("DepthStencil",
		1200,
		800,
		DXGI_FORMAT_D32_FLOAT,
		TextureBindType::DepthStencil);

	using namespace DirectX;

	{		
		float aspect = 1200.f / 800.f;	

		

		m_frameInfo.CameraBufferInfo;
		m_frameInfo.CameraBufferInfo.Position = { 0.0f, 250.f, -500.0 };
		m_frameInfo.CameraBufferInfo.FieldOfView = 3.14f / 2.0f;
		m_frameInfo.CameraBufferInfo.ForwardDirection = { 0.0f, 0.0f, 1.0f };
		m_frameInfo.CameraBufferInfo.DistanceNearPlane = 10.0f;
		m_frameInfo.CameraBufferInfo.UpDirection = { 0.0f, 1.0f, 0.0f };
		m_frameInfo.CameraBufferInfo.DistanceFarPlane = 1000.0f;

		XMMATRIX view = XMMatrixLookToLH(
			XMLoadFloat3(&m_frameInfo.CameraBufferInfo.Position),
			XMLoadFloat3(&m_frameInfo.CameraBufferInfo.ForwardDirection),
			XMLoadFloat3(&m_frameInfo.CameraBufferInfo.UpDirection));
		XMMATRIX projection = XMMatrixPerspectiveFovLH(
			m_frameInfo.CameraBufferInfo.FieldOfView,
			aspect,
			m_frameInfo.CameraBufferInfo.DistanceNearPlane,
			m_frameInfo.CameraBufferInfo.DistanceFarPlane);
		XMMATRIX viewProjection = view * projection;

		view = XMMatrixTranspose(view);
		projection = XMMatrixTranspose(projection);
		viewProjection = XMMatrixTranspose(viewProjection);

		XMStoreFloat4x4(&m_frameInfo.CameraBufferInfo.ViewMatrix, view);
		XMStoreFloat4x4(&m_frameInfo.CameraBufferInfo.ProjectionMatrix, projection);
		XMStoreFloat4x4(&m_frameInfo.CameraBufferInfo.ViewProjectionMatrix, viewProjection);

		m_cameraBuffer = ResourceManager::Get()->CreateBuffer("CameraBuffer", {
			{ "Position", ShaderDataType::Float3 },
			{ "FieldOfView", ShaderDataType::Float },
			{ "ForwardDirection", ShaderDataType::Float3 },
			{ "DistanceNearPlane", ShaderDataType::Float },
			{ "UpDirection", ShaderDataType::Float3 },
			{ "DistanceFarPlane", ShaderDataType::Float },
			{ "ViewMatrix", ShaderDataType::Float4x4 },
			{ "ProjectionMatrix", ShaderDataType::Float4x4 },
			{ "ViewProjectionMatrix", ShaderDataType::Float4x4 } },
			BufferBindType::Constant,
			&m_frameInfo.CameraBufferInfo);
	}

	{
		XMMATRIX translation = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
		XMMATRIX rotation = XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f);
		XMMATRIX scale = XMMatrixScaling(1.0f, 1.0f, 1.0f);

		XMFLOAT4X4 transform;
		//XMStoreFloat4x4(&transform, XMMatrixTranspose(scale * rotation * translation));
		XMStoreFloat4x4(&transform, XMMatrixIdentity());

		ObjectBufferData data;
		data.Position = { 0.0f, 0.0f, 0.0f };
		data.ID = 0;
		data.ForwardDirection = { 0.0f, 0.0f, 1.0f };
		data.MaterialID = 0;
		data.UpDirection = { 0.0f, 1.0f, 0.0f };
		data.Pad = 0;
		data.WorldMatrix = transform;

		m_objectBuffer = ResourceManager::Get()->CreateBuffer("ObjectBuffer", {
			{ "Position", ShaderDataType::Float3 },
			{ "ID", ShaderDataType::Float },
			{ "ForwardDirection", ShaderDataType::Float3 },
			{ "MaterialID", ShaderDataType::Float },
			{ "UpDirection", ShaderDataType::Float3 },
			{ "Pad", ShaderDataType::Float },
			{ "WorldMatrix", ShaderDataType::Float4x4 } },
			BufferBindType::Constant,
			&data);
	}

	//m_phongBuffer = ResourceManager::Get()->CreateConstantBuffer("PhongBuffer", {
	//	{ "DiffuseFactor", ShaderDataType::Float3 },
	//	{ "Padding1", ShaderDataType::Float },
	//	{ "AmbientFactor", ShaderDataType::Float3 },
	//	{ "Padding2", ShaderDataType::Float },
	//	{ "SpecularFactor", ShaderDataType::Float3 },
	//	{ "SpecularExponent", ShaderDataType::Float } });

	m_frameInfo.FrameDescriptorAllocator.Initialize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 32, true);
}

env::Renderer::~Renderer()
{
	//
}

void env::Renderer::Initialize()
{
}

void env::Renderer::BeginFrame(const CameraSettings& cameraSettings, Transform& cameraTransform, ID target)
{
	WindowTarget* targetResource = ResourceManager::Get()->GetTarget(target);
	PipelineState* pipeline = ResourceManager::Get()->GetPipelineState(m_pipelineState);

	float aspectRatio = targetResource->Viewport.Width / targetResource->Viewport.Height;

	m_frameInfo.WindowTarget = targetResource;
	m_frameInfo.FrameDescriptorAllocator.Clear();

	m_directList->Reset();

	Texture2D* depthStencil = ResourceManager::Get()->GetTexture2D(m_depthStencil);

	m_directList->SetTarget(targetResource, depthStencil);
	m_directList->ClearRenderTarget(targetResource->Views.RenderTarget, 0.2f, 0.2f, 0.2f);
	m_directList->ClearDepthStencil(depthStencil->Views.DepthStencil, true, false, 1.0f, 0);

	m_directList->SetPipelineState(pipeline);

	ID3D12DescriptorHeap* descriptorHeap = m_frameInfo.FrameDescriptorAllocator.GetHeap();
	m_directList->SetDescriptorHeaps(1, &descriptorHeap);

	{ // Set up camera
		
		using namespace DirectX;

		Float4x4 cameraView = XMMatrixLookToLH(
			cameraTransform.GetPosition(),
			cameraTransform.GetForward(),
			cameraTransform.GetUp());

		Float4x4 cameraProjection = XMMatrixPerspectiveFovLH(
			cameraSettings.FieldOfView,
			aspectRatio,
			cameraSettings.DistanceNearPlane,
			cameraSettings.DistanceFarPlane);

		Float4x4 cameraViewProjection = cameraView * cameraProjection;
		
		cameraView = cameraView.Transpose();
		cameraProjection = cameraProjection.Transpose();
		cameraViewProjection = cameraViewProjection.Transpose();

		m_frameInfo.CameraBufferInfo.Position = cameraTransform.GetPosition();
		m_frameInfo.CameraBufferInfo.ForwardDirection = cameraTransform.GetForward();
		m_frameInfo.CameraBufferInfo.UpDirection = cameraTransform.GetUp();
		m_frameInfo.CameraBufferInfo.ViewMatrix = cameraView;
		m_frameInfo.CameraBufferInfo.ProjectionMatrix = cameraProjection;
		m_frameInfo.CameraBufferInfo.ViewProjectionMatrix = cameraViewProjection;

		m_frameInfo.CameraBufferInfo.FieldOfView = cameraSettings.FieldOfView;
		m_frameInfo.CameraBufferInfo.DistanceNearPlane = cameraSettings.DistanceNearPlane;
		m_frameInfo.CameraBufferInfo.DistanceFarPlane = cameraSettings.DistanceFarPlane;
		
		Buffer* cameraBuffer = ResourceManager::Get()->GetBuffer(m_cameraBuffer);

		ResourceManager::Get()->UploadBufferData(m_cameraBuffer,
			&m_frameInfo.CameraBufferInfo,
			sizeof(m_frameInfo.CameraBufferInfo));

		D3D12_CPU_DESCRIPTOR_HANDLE cameraBufferSource = cameraBuffer->Views.Constant;
		D3D12_CPU_DESCRIPTOR_HANDLE cameraBufferDest = m_frameInfo.FrameDescriptorAllocator.Allocate();
		GPU::GetDevice()->CopyDescriptorsSimple(1,
			cameraBufferDest,
			cameraBufferSource,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		m_directList->GetNative()->SetGraphicsRootConstantBufferView(0, cameraBuffer->Native->GetGPUVirtualAddress());
	}

	{ // Set up object
		Buffer* objectBuffer = ResourceManager::Get()->GetBuffer(m_objectBuffer);

		D3D12_CPU_DESCRIPTOR_HANDLE objectBufferSource = objectBuffer->Views.Constant;
		D3D12_CPU_DESCRIPTOR_HANDLE objectBufferDest = m_frameInfo.FrameDescriptorAllocator.Allocate();
		GPU::GetDevice()->CopyDescriptorsSimple(1,
			objectBufferDest,
			objectBufferSource,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		m_directList->GetNative()->SetGraphicsRootConstantBufferView(1, objectBuffer->Native->GetGPUVirtualAddress());
	}
}

void env::Renderer::Submit(Transform& transform, ID mesh, ID material)
{
	const Mesh* meshAsset = AssetManager::Get()->GetMesh(mesh);
	const Material* materialAsset = AssetManager::Get()->GetMaterial(material);

	Buffer* vertexBuffer = ResourceManager::Get()->GetBuffer(meshAsset->VertexBuffer);
	Buffer* indexBuffer = ResourceManager::Get()->GetBuffer(meshAsset->IndexBuffer);

	m_directList->SetVertexBuffer(vertexBuffer, 0);
	m_directList->SetIndexBuffer(indexBuffer);

	ObjectBufferData objectData;
	objectData.Position = transform.GetPosition();
	objectData.ID = mesh;
	objectData.ForwardDirection = transform.GetForward();
	objectData.MaterialID = material;
	objectData.UpDirection = transform.GetUp();
	objectData.Pad = 0;
	objectData.WorldMatrix = transform.GetMatrixTransposed();

	Buffer* objectBuffer = ResourceManager::Get()->GetBuffer(m_objectBuffer);
	ResourceManager::Get()->UploadBufferData(m_objectBuffer, &objectData, sizeof(objectData));

	m_directList->DrawIndexed(indexBuffer->Layout.GetNumRepetitions(), 0, 0);
	//m_directList->DrawIndexed(3132, 0, 0);
}

void env::Renderer::EndFrame()
{
	m_directList->Close();

	CommandQueue& queue = GPU::GetPresentQueue();
	queue.QueueList(m_directList);
	queue.Execute();
	queue.WaitForIdle();

	if (m_frameInfo.WindowTarget->GetType() == ResourceType::WindowTarget)
	{
		WindowTarget* target = (WindowTarget*)m_frameInfo.WindowTarget;

		m_directList->Reset();
		m_directList->TransitionResource(target, D3D12_RESOURCE_STATE_PRESENT);
		m_directList->Close();
		queue.QueueList(m_directList);
		queue.Execute();
		queue.WaitForIdle();
	}
}
