#include <QGuiApplication>
#include <QCommandLineParser>
#include <QtQuick>
#include <QVulkanInstance>
#include <QQuickRenderControl>
#include <iostream>
#include "openvr.h"
#include "vrmanager.h"
#include "pamanager.h"

using namespace vr;
int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    QGuiApplication a(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("VR Audio Controls for Linux");
    parser.addHelpOption();
    parser.addOption({"uninstall", "Uninstalls the manifest from Steam."});
    parser.process(a);

    if (parser.isSet("uninstall")){
	VRManager::uninstall();
	return 0;
    }

    QQuickRenderControl renderCtrl;
    // need to use qquickrendercontrol constructor, but also can't set url yet
    QQuickView w(QUrl(),&renderCtrl);
    QVulkanInstance instance;

    // setup vulkan
    VRManager ctrl(&w, &renderCtrl);

    // setup pulseaudio
    PAManager pulse("VR Audio Control");


    // expose PAManager to QML
    // have to call setContextProperty before setting source, otherwise you get annoying errors
    w.rootContext()->setContextProperty("pulse", &pulse);
    w.setSource(QUrl("qrc:///main.qml"));

    if (!renderCtrl.initialize()){
	throw std::runtime_error("Failed to initialize QQuickRenderControl!");
    }

    ctrl.buildOverlay();

    return a.exec();
}
