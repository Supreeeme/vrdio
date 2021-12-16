#ifndef PAMANAGER_H
#define PAMANAGER_H

#include <pulse/pulseaudio.h>
#include <string>
#include <QObject>
#include <QQmlListProperty>
#include <QMetaType>
#include <memory>
// pamanager.h: holds all pulseaudio related actions

struct Profile {
    std::string name;
    std::string description;
    bool active;
};
class Card : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString description MEMBER description CONSTANT)
    Q_PROPERTY(QStringList profiles READ getProfileList CONSTANT)
    Q_PROPERTY(Profile activeProfile READ getActiveProfile)
    Q_PROPERTY(int activeProfileIndex MEMBER activeProfileIndex)
public:
    std::string name; // alsa_card_XXXXX...
    uint32_t index;
    QString description;
    std::vector<Profile> availableProfiles;

    unsigned int activeProfileIndex;
    Profile getActiveProfile();
    QStringList getProfileList() const;
signals:
    void profilesChanged();
};

class PAManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList sinks READ getSinkList NOTIFY sinksChanged)
    Q_PROPERTY(QVariantList cards READ getCardList NOTIFY cardsChanged)
    Q_PROPERTY(int sinkIndex READ getDefaultSinkIndex NOTIFY newDefaultSink)
public:
    explicit PAManager(const char* appName);
    ~PAManager();

public slots:
    int getVolPct();
    QStringList getSinkList();
    QVariantList getCardList();
    void changeVol(int newPct);
    void changeSink(int sinkIndex);
    void changeCardProfile(Card *card, const QString& profileName);
    int getDefaultSinkIndex() const;
    bool saveConfig();
    void loadConfig();

signals:
    void sinksChanged();
    void cardsChanged();
    void newDefaultSink();

private:
    struct Sink {
        std::string name;
        std::string description; // user friendly name
        uint32_t index;
        pa_cvolume volume;
        bool operator==(const Sink& other) const;
    };

    std::vector<Sink> sinkList;

    // cardList is a vector of pointers because QObjects have no copy constructors.
    std::vector<std::unique_ptr<Card>> cardList;

    pa_threaded_mainloop *mainloop;
    pa_context *context;
    int defaultSinkIndex;

    static void waitForContext(pa_context *c, void *userdata);

    // (Re)builds sink list
    void buildSinkList();

    void getDefaultSink();

    void buildCardList();

    void waitForOpFinish(pa_operation *);

};

#endif //PAMANAGER_H
