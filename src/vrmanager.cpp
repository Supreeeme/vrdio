#include "vrmanager.h"

#include "openvr.h"
#include "strs.h"

#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QTemporaryDir>
#include <QTimer>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <iostream>
#include <memory>
using namespace vr;

VRManager::VRManager(QQuickView* w, QQuickRenderControl* rc)
	: window(w), renderCtrl(rc), overlayWidth(2100), overlayHeight(1200) {

	// expose overlay size to QML
	window->engine()->rootContext()->setContextProperty("screenWidth", overlayWidth);
	window->engine()->rootContext()->setContextProperty("screenHeight", overlayHeight);

	// signal to create scene graph
	connect(window, SIGNAL(sceneGraphInitialized()), this, SLOT(prepareSceneGraph()));

	window->setVulkanInstance(&instance);

	initVR();
	initVulkan();
	getPhysicalDevice();
	createLogicalDevice();
}

void VRManager::prepareSceneGraph() {
	createImage();
	createCommandPool();
	window->setRenderTarget(QQuickRenderTarget::fromVulkanImage(
			image, curLayout, QSize(overlayWidth, overlayHeight)));

	// create timer
	checkTimer = std::make_unique<QTimer>(this);
	connect(checkTimer.get(), SIGNAL(timeout()), this, SLOT(checkRender()));
	checkTimer->setInterval(20);
	checkTimer->start();
}

void VRManager::pollEvents() {
	VREvent_t event{};
	// quitting
	while (VRSystem()->PollNextEvent(&event, sizeof(event))) {
		switch (event.eventType) {
		case VREvent_Quit: {
			VRSystem()->AcknowledgeQuit_Exiting();
			std::cout << "Exiting!" << std::endl;
			QGuiApplication::quit();
		} break;
		default:
			break;
		}
	}

	// input
	while (VROverlay()->PollNextOverlayEvent(overlay, &event, sizeof(event))) {
		switch (event.eventType) {

		case (VREvent_MouseMove): {
			// SteamVR (0,0) is bottom left, while Qt (0,0) is top left - invert y
			QPointF mousePos(event.data.mouse.x, overlayHeight - event.data.mouse.y);
			QMouseEvent mouseEvent(QEvent::MouseMove, mousePos, window->mapToGlobal(mousePos),
					Qt::NoButton, activeMouseButtons, Qt::NoModifier);

			QGuiApplication::sendEvent(window, &mouseEvent);
			lastPos = mousePos;
		} break;

		case (VREvent_MouseButtonDown): {
			QPointF mousePos(event.data.mouse.x, overlayHeight - event.data.mouse.y);
			activeMouseButtons |= Qt::LeftButton;

			QMouseEvent mouseEvent(QEvent::MouseButtonPress, mousePos,
					window->mapToGlobal(mousePos), Qt::LeftButton, activeMouseButtons,
					Qt::NoModifier);

			QGuiApplication::sendEvent(window, &mouseEvent);
			lastPos = mousePos;
		} break;

		case (VREvent_MouseButtonUp): {
			QPointF mousePos(event.data.mouse.x, overlayHeight - event.data.mouse.y);
			activeMouseButtons &= ~Qt::LeftButton;

			QMouseEvent mouseEvent(QEvent::MouseButtonRelease, mousePos,
					window->mapToGlobal(mousePos), Qt::LeftButton, activeMouseButtons,
					Qt::NoModifier);

			QGuiApplication::sendEvent(window, &mouseEvent);
			lastPos = mousePos;
		} break;

		case (VREvent_ScrollDiscrete): {
			QPoint scrollData(0, -event.data.scroll.ydelta);
			QWheelEvent wheelEvent(lastPos, window->mapToGlobal(lastPos), QPoint(), scrollData,
					activeMouseButtons, Qt::NoModifier, Qt::NoScrollPhase, false,
					Qt::MouseEventNotSynthesized);
			QGuiApplication::sendEvent(window, &wheelEvent);

		} break;
		}
	}
}

void VRManager::checkRender() {
	// check if overlay is setup
	if (!VROverlay() || overlay == k_ulOverlayHandleInvalid) {
		VROverlay()->SetOverlayTexture(overlay, nullptr);
		return;
	}

	render();
	pollEvents();
}

void VRManager::render() {
	renderCtrl->polishItems();
	renderCtrl->beginFrame();
	renderCtrl->sync();
	renderCtrl->render();
	renderCtrl->endFrame();
	curLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // QQuickRenderControl transitions image

	transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	VRVulkanTextureData_t texData{};
	texData.m_nImage = (uint64_t)image;
	texData.m_pDevice = device;
	texData.m_pPhysicalDevice = physicalDevice;
	texData.m_pInstance = instance.vkInstance();
	texData.m_pQueue = graphicsQueue;
	texData.m_nQueueFamilyIndex = graphicsFamily;
	texData.m_nWidth = overlayWidth;
	texData.m_nHeight = overlayHeight;
	texData.m_nFormat = VK_FORMAT_R8G8B8A8_UNORM;
	texData.m_nSampleCount = 1;

	Texture_t tex{};
	tex.handle = static_cast<void*>(&texData);
	tex.eType = TextureType_Vulkan;
	tex.eColorSpace = ColorSpace_Auto;

	VROverlay()->SetOverlayTexture(overlay, &tex);
}
void VRManager::buildOverlay() {
	VROverlay()->CreateDashboardOverlay(
			strings::app_key, strings::overlay_friendly_name, &overlay, &icon);
	VROverlay()->SetOverlayWidthInMeters(overlay, 2.7f);
	VROverlay()->SetOverlayInputMethod(overlay, VROverlayInputMethod_Mouse);

	// SetOverlayMouseScale basically sets bounds of the overlay
	HmdVector2_t mouseScale = {(float)overlayWidth, (float)overlayHeight};
	VROverlay()->SetOverlayMouseScale(overlay, &mouseScale);

	// enable scroll events
	VROverlay()->SetOverlayFlag(overlay, VROverlayFlags_SendVRDiscreteScrollEvents, true);

	// set up icon
	if (temp_dir.isValid()) {
		const QString temp_file = temp_dir.path() + "/speaker-256.png";
		if (QFile::copy(":/speaker-256.png", temp_file)) {
			VROverlay()->SetOverlayFromFile(icon, temp_file.toUtf8());
		}
	}
}

void VRManager::uninstall() { initVR(true); }
void VRManager::initVR(bool uninstall) {
	EVRInitError peError = EVRInitError::VRInitError_None;
	VR_Init(&peError, EVRApplicationType::VRApplication_Overlay);
	if (peError != EVRInitError::VRInitError_None)
		throw std::runtime_error(std::string("Could not initialize VR: ")
								 + VR_GetVRInitErrorAsEnglishDescription(peError));

	// Manifests seem to need to always exist: install to ~/.config/vrdio
	QDir config_loc(strings::config_dir_loc);
	QFileInfo manifest(strings::vrmanifest_loc);

	if (uninstall) {
		if (VRApplications()->IsApplicationInstalled(strings::app_key)) {
			auto err = VRApplications()->RemoveApplicationManifest(
					manifest.absoluteFilePath().toUtf8());
			if (err != EVRApplicationError::VRApplicationError_None) {
				throw std::runtime_error(std::string("Failed to remove manifest: ")
										 + VRApplications()->GetApplicationsErrorNameFromEnum(err));
			}
			QFile::remove(manifest.absoluteFilePath());

			std::cout << "Manifest uninstalled." << std::endl;
		} else
			std::cout << "No manifest to uninstall." << std::endl;
	} else {
		bool success = true;
		if (!manifest.exists()) { // create ~/.config/vrdio
			if (!config_loc.exists())
				success = config_loc.mkpath(strings::config_dir_loc);

			if (success) {
				// build manifest
				QJsonObject top_level;
				top_level.insert("source", "supreme");
				QJsonObject obj, strings, en_us;
				obj.insert("app_key", strings::app_key);
				obj.insert("launch_type", "binary");
				obj.insert("binary_path_linux", QGuiApplication::applicationFilePath());
				obj.insert("is_dashboard_overlay", true);
				en_us.insert("name", "VRdio");
				en_us.insert("description", "Audio control from VR for Linux");
				strings.insert("en_us", en_us);
				obj.insert("strings", strings);
				top_level.insert("applications", QJsonArray({obj}));

				QJsonDocument doc;
				doc.setObject(top_level);

				// save file
				QFile file(manifest.absoluteFilePath());
				file.open(QFile::WriteOnly);
				file.write(doc.toJson());
			} else {
				std::clog << "Couldn't create config path to install manifest!" << std::endl;
			}
		}
		if (success && !VRApplications()->IsApplicationInstalled(strings::app_key)) {
			// add manifest
			std::clog << "Installing manifest..." << std::endl;
			auto err =
					VRApplications()->AddApplicationManifest(manifest.absoluteFilePath().toUtf8());
			if (err != EVRApplicationError::VRApplicationError_None) {
				std::clog << "Failed to add manifest: "
						  << VRApplications()->GetApplicationsErrorNameFromEnum(err) << std::endl;
			} else
				std::clog << "Manifest installed." << std::endl;

			// set up auto launch
			err = VRApplications()->SetApplicationAutoLaunch(strings::app_key, true);
			if (err != EVRApplicationError::VRApplicationError_None) {
				std::clog << "Failed to enable autostart: "
						  << VRApplications()->GetApplicationsErrorNameFromEnum(err) << std::endl;
			} else
				std::clog << "Autostart enabled." << std::endl;
		}
	}
}

void VRManager::initVulkan() {
	// get extensions required by SteamVR
	uint32_t extSize = VRCompositor()->GetVulkanInstanceExtensionsRequired(nullptr, 0);
	QByteArray extList;
	extList.resize(extSize);
	VRCompositor()->GetVulkanInstanceExtensionsRequired(extList.data(), extSize);
	QByteArrayList reqExts = extList.split(' ');

	instance.setExtensions(reqExts);

	if (!instance.create()) {
		throw std::runtime_error("Failed to create Vulkan instance");
	}
	vkFuncs = instance.functions();
}

void VRManager::getPhysicalDevice() {
	uint32_t devCount = 0;
	vkFuncs->vkEnumeratePhysicalDevices(instance.vkInstance(), &devCount, nullptr);
	VkPhysicalDevice physicalDevices[devCount];
	vkFuncs->vkEnumeratePhysicalDevices(instance.vkInstance(), &devCount, physicalDevices);
	physicalDevice = physicalDevices[0];
}

void VRManager::createLogicalDevice() {
	// get graphics queue family
	uint32_t queueFamilyCount = 0;
	vkFuncs->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
	VkQueueFamilyProperties queueFamilies[queueFamilyCount];
	vkFuncs->vkGetPhysicalDeviceQueueFamilyProperties(
			physicalDevice, &queueFamilyCount, queueFamilies);

	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			graphicsFamily = i;
			break;
		}
		i++;
	}

	// specify queue creation info
	VkDeviceQueueCreateInfo queueCreateInfo{};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = graphicsFamily;
	queueCreateInfo.queueCount = 1;
	float queuePriority = 1.0f;
	queueCreateInfo.pQueuePriorities = &queuePriority;

	// specify device features
	VkPhysicalDeviceFeatures deviceFeatures{};

	// device create info
	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pQueueCreateInfos = &queueCreateInfo;
	createInfo.queueCreateInfoCount = 1;
	createInfo.pEnabledFeatures = &deviceFeatures;

	// get required device extensions
	uint32_t exts2 = 0;
	vkFuncs->vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &exts2, nullptr);
	std::vector<VkExtensionProperties> ext2list(exts2);
	vkFuncs->vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &exts2, ext2list.data());

	uint32_t extSize =
			VRCompositor()->GetVulkanDeviceExtensionsRequired(physicalDevice, nullptr, 0);
	QByteArray extList;
	extList.resize(extSize);
	VRCompositor()->GetVulkanDeviceExtensionsRequired(physicalDevice, extList.data(), extSize);
	QByteArrayList reqExts = extList.split(' ');
	std::vector<const char*> reqExts_vk;
	for (auto& str : reqExts) {
		str.push_back('\0'); // add null terminators
		reqExts_vk.push_back(str.data());
	}

	createInfo.enabledExtensionCount = reqExts.size();
	createInfo.ppEnabledExtensionNames = reqExts_vk.data();
	createInfo.enabledLayerCount = 0;
	vkFuncs->vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);

	devFuncs = instance.deviceFunctions(device);
	// save graphics queue
	devFuncs->vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);

	// set qgraphicsdevice
	window->setGraphicsDevice(
			QQuickGraphicsDevice::fromDeviceObjects(physicalDevice, device, graphicsFamily));
}

void VRManager::createImage() {
	// create vulkan image for rendering
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = overlayWidth;
	imageInfo.extent.height = overlayHeight;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	curLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	devFuncs->vkCreateImage(device, &imageInfo, nullptr, &image);

	// allocate memory for image
	VkMemoryRequirements memReqs;
	devFuncs->vkGetImageMemoryRequirements(device, image, &memReqs);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = UINT32_MAX;

	VkPhysicalDeviceMemoryProperties memProps;
	vkFuncs->vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
		if (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
			allocInfo.memoryTypeIndex = i;
			break;
		}
	}

	if (allocInfo.memoryTypeIndex == UINT32_MAX)
		exit(1);

	devFuncs->vkAllocateMemory(device, &allocInfo, nullptr, &imageMem);

	devFuncs->vkBindImageMemory(device, image, imageMem, 0);
}

void VRManager::transitionImageLayout(VkImageLayout newLayout) {
	if (newLayout == curLayout)
		return; // no transition needed

	VkCommandBuffer cmdBuffer = beginSingleTimeCommands();
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = curLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1};

	VkPipelineStageFlags srcFlags, dstFlags;
	if (curLayout == VK_IMAGE_LAYOUT_UNDEFINED
			&& newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		srcFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	} else if (curLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			   && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		srcFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dstFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if (curLayout == VK_IMAGE_LAYOUT_UNDEFINED
			   && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		srcFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else {
		throw std::runtime_error("Invalid layout transition!");
	}

	devFuncs->vkCmdPipelineBarrier(
			cmdBuffer, srcFlags, dstFlags, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	endSingleTimeCommands(cmdBuffer);
	curLayout = newLayout;
}

void VRManager::createCommandPool() {
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = graphicsFamily;

	devFuncs->vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
}

VkCommandBuffer VRManager::beginSingleTimeCommands() {
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	devFuncs->vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	devFuncs->vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

void VRManager::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
	devFuncs->vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	devFuncs->vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	devFuncs->vkQueueWaitIdle(graphicsQueue);

	devFuncs->vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

VRManager::~VRManager() {
	devFuncs->vkDestroyCommandPool(device, commandPool, nullptr);
	devFuncs->vkDestroyImage(device, image, nullptr);
	devFuncs->vkFreeMemory(device, imageMem, nullptr);
	VR_Shutdown();
}
