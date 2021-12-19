#ifndef VRMANAGER_H
#define VRMANAGER_H

#include <QObject>
#include <QQuickView>
#include <QVulkanInstance>
#include <QQuickGraphicsDevice>
#include <memory>
#include <QTemporaryDir>
#include "openvr.h"

class VRManager : public QObject
{
    Q_OBJECT
public:
    VRManager(QQuickView* window, QQuickRenderControl* rc);
    ~VRManager();
    static void uninstall();
    void buildOverlay();

public slots:
    void prepareSceneGraph();
    void checkRender();

private:
    // initialization
    static void initVR(bool uninstall = false);
    void initVulkan();
    void getPhysicalDevice();
    void createLogicalDevice();
    void createImage();


    // rendering
    void createCommandPool();
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer b);
    void transitionImageLayout(VkImageLayout newLayout);
    void render();

    void pollEvents();

    QTemporaryDir temp_dir; // for icon

    QQuickView *window;
    QQuickRenderControl *renderCtrl;
    QVulkanInstance instance;
    QVulkanFunctions *vkFuncs;

    VkPhysicalDevice physicalDevice;
    VkDevice device;

    VkQueue graphicsQueue;
    QVulkanDeviceFunctions *devFuncs;
    uint32_t graphicsFamily;

    VkImage image;
    VkImageLayout curLayout;
    VkDeviceMemory imageMem;

    VkCommandPool commandPool;

    vr::VROverlayHandle_t overlay, icon;
    int overlayWidth, overlayHeight;
    std::unique_ptr<QTimer> checkTimer;

    Qt::MouseButtons activeMouseButtons;
    QPointF lastPos;
};



#endif // VRMANAGER_H
