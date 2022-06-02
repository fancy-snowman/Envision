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

	using namespace DirectX;

	{
		XMMATRIX view = XMMatrixLookAtLH({ 0.0f, 0.0f, -2.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
		XMMATRIX projection = XMMatrixPerspectiveFovLH(3.14f / 2.0f, 1200.f / 800.f, 1.0f, 10.f);

		XMFLOAT4X4 viewProjection;
		XMStoreFloat4x4(&viewProjection, XMMatrixTranspose(view * projection));

		m_cameraBuffer = ResourceManager::Get()->CreateBuffer("CameraBuffer", {
			{ "ViewProjectionMatrix", ShaderDataType::Float4x4} },
			BufferBindType::Constant,
			&viewProjection);
	}

	{
		XMMATRIX translation = XMMatrixTranslation(0.1f, 0.5f, 0.0f);
		XMMATRIX rotation = XMMatrixRotationRollPitchYaw(0.785f, 1.0f, 0.0f);
		XMMATRIX scale = XMMatrixScaling(2.0f, 2.0f, 2.0f);

		XMFLOAT4X4 transform;
		XMStoreFloat4x4(&transform, XMMatrixTranspose(scale * rotation * translation));

		m_objectBuffer = ResourceManager::Get()->CreateBuffer("ObjectBuffer", {
			{ "WorldMatrix", ShaderDataType::Float4x4 } },
			BufferBindType::Constant,
			&transform);

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

void env::Renderer::BeginFrame(ID target)
{
	WindowTarget* targetWindow = ResourceManager::Get()->GetWindowTarget(target);
	PipelineState* pipeline = ResourceManager::Get()->GetPipelineState(m_pipelineState);

	m_frameInfo.Target = targetWindow;
	m_frameInfo.FrameDescriptorAllocator.Clear();

	m_directList->Reset();

	m_directList->SetWindowTarget(targetWindow);
	m_directList->ClearRenderTarget(targetWindow->GetActiveBackbuffer(), 0.2f, 0.2f, 0.2f);

	m_directList->SetPipelineState(pipeline);

	ID3D12DescriptorHeap* descriptorHeap = m_frameInfo.FrameDescriptorAllocator.GetHeap();
	m_directList->SetDescriptorHeaps(1, &descriptorHeap);

	{ // Set up camera
		Buffer* cameraBuffer = ResourceManager::Get()->GetBuffer(m_cameraBuffer);
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

void env::Renderer::Submit(ID mesh, ID material)
{
	const Mesh* meshAsset = AssetManager::Get()->GetMesh(mesh);
	const Material* materialAsset = AssetManager::Get()->GetMaterial(material);

	Buffer* vertexBuffer = ResourceManager::Get()->GetBuffer(meshAsset->VertexBuffer);
	Buffer* indexBuffer = ResourceManager::Get()->GetBuffer(meshAsset->IndexBuffer);

	m_directList->SetVertexBuffer(vertexBuffer, 0);
	m_directList->SetIndexBuffer(indexBuffer);

	m_directList->DrawIndexed(indexBuffer->Layout.GetNumRepetitions(), 0, 0);
}

void env::Renderer::EndFrame()
{
	m_directList->Close();

	CommandQueue& queue = GPU::GetPresentQueue();
	queue.QueueList(m_directList);
	queue.Execute();
	queue.WaitForIdle();

	if (m_frameInfo.Target->GetType() == ResourceType::WindowTarget)
	{
		WindowTarget* window = (WindowTarget*)m_frameInfo.Target;
		Texture2D* backbuffer = window->GetActiveBackbuffer();


		m_directList->Reset();
		m_directList->TransitionResource(backbuffer, D3D12_RESOURCE_STATE_PRESENT);
		m_directList->Close();
		queue.QueueList(m_directList);
		queue.Execute();
		queue.WaitForIdle();

		window->SwapChain->Present(0, 0);
		window->ActiveBackBufferIndex = (window->ActiveBackBufferIndex + 1) % WindowTarget::NUM_BACK_BUFFERS;
	}
}
