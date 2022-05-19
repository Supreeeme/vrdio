#include "pamanager.h"

#include "strs.h"

#include <QDir>
#include <QFile>
#include <iostream>
#include <pulse/pulseaudio.h>
#include <vector>

void PAManager::waitForContext(pa_context* c, void* userdata) {
	if (pa_context_get_state(c) != PA_CONTEXT_READY)
		return;

	// send signal that context is ready and mainloop can stop waiting
	pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop*>(userdata), 0);
}
PAManager::PAManager(const char* appName)
	: mainloop(nullptr), context(nullptr), defaultSinkIndex(-1) {
	// create mainloop
	mainloop = pa_threaded_mainloop_new();

	// create context
	pa_mainloop_api* api = pa_threaded_mainloop_get_api(mainloop);
	context = pa_context_new(api, appName);

	// set context callback. used to escape wait loop below
	// mainloop passed as userdata for signalling
	pa_context_set_state_callback(context, &PAManager::waitForContext, mainloop);

	// connect to context
	pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

	// start the mainloop
	pa_threaded_mainloop_start(mainloop);

	pa_threaded_mainloop_lock(mainloop);
	// wait for context to be ready
	while (pa_context_get_state(context) != PA_CONTEXT_READY)
		pa_threaded_mainloop_wait(mainloop);
	pa_threaded_mainloop_unlock(mainloop);

	loadConfig();
	buildSinkList();
	getDefaultSink();
	buildCardList();
}

PAManager::~PAManager() {
	pa_context_disconnect(context);
	pa_threaded_mainloop_stop(mainloop);
	pa_threaded_mainloop_free(mainloop);
}

void PAManager::waitForOpFinish(pa_operation* o) {
	pa_threaded_mainloop_lock(mainloop);
	while (pa_operation_get_state(o) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(mainloop);
	pa_threaded_mainloop_unlock(mainloop);
	pa_operation_unref(o);
}

// change the volume. newPct is a percentage
void PAManager::changeVol(int newPct) {
	int newVol = PA_VOLUME_NORM * ((double)newPct / 100);
	pa_cvolume_set(&(sinkList[defaultSinkIndex].volume), sinkList[defaultSinkIndex].volume.channels,
			newVol);
	pa_operation* o = pa_context_set_sink_volume_by_index(context, sinkList[defaultSinkIndex].index,
			&(sinkList[defaultSinkIndex].volume), nullptr, nullptr);
	pa_operation_unref(o);
}

// return volume of the default sink as a percentage (0 - 100)
int PAManager::getVolPct() {
	if (defaultSinkIndex == -1)
		return 0;

	// update sink volume
	struct cbStruct {
		std::vector<Sink>& sink_list;
		int& def_index;
		pa_threaded_mainloop* mainloop;
	};

	cbStruct s = {sinkList, defaultSinkIndex, mainloop};

	auto callback = [](pa_context*, const pa_sink_info* sink, int eol, void* data) {
		auto items = static_cast<cbStruct*>(data);
		if (eol) {
			pa_threaded_mainloop_signal(items->mainloop, 0);
			return;
		}
		items->sink_list[items->def_index].volume = sink->volume;
	};

	pa_operation* o = pa_context_get_sink_info_by_name(
			context, sinkList[defaultSinkIndex].name.c_str(), callback, static_cast<void*>(&s));
	waitForOpFinish(o);

	return ceil(((double)sinkList[defaultSinkIndex].volume.values[0] / PA_VOLUME_NORM) * 100);
}

// get sink names into qml
QStringList PAManager::getSinkList() {
	pa_threaded_mainloop_lock(mainloop);
	QStringList list;
	for (const auto& sink : sinkList) {
		list << sink.description.c_str();
	}
	pa_threaded_mainloop_unlock(mainloop);
	return list;
}

void PAManager::changeSink(int sinkIndex) {
	pa_operation* op = pa_context_set_default_sink(
			context, sinkList[sinkIndex].name.c_str(), nullptr, nullptr);
	pa_operation_unref(op);
	defaultSinkIndex = sinkIndex;
	emit newDefaultSink();
}

int PAManager::getDefaultSinkIndex() const { return defaultSinkIndex; }
// get card names into qml
QVariantList PAManager::getCardList() {
	// pa_threaded_mainloop_lock(mainloop);
	QVariantList list;
	for (const auto& card : cardList)
		list << QVariant::fromValue(card.get());

	// pa_threaded_mainloop_unlock(mainloop);
	return list;
}

void PAManager::changeCardProfile(Card* card, const QString& profileName) {
	pa_threaded_mainloop_lock(mainloop);

	std::string oldSinkName = sinkList[defaultSinkIndex].name;

	std::string profile_name{};
	for (int i = 0; i < card->availableProfiles.size(); i++) {
		const auto& profile = card->availableProfiles[i];
		if (profile.description == profileName.toStdString()) {
			profile_name = profile.name;
			card->activeProfileIndex = i;
			break;
		}
	}

	auto callback = [](pa_context*, int success, void* data) {
		auto ml = static_cast<pa_threaded_mainloop*>(data);
		if (!success)
			std::cout << "Failed to switch to profile." << std::endl;
		pa_threaded_mainloop_signal(ml, 0);
	};

	pa_operation* o = pa_context_set_card_profile_by_index(
			context, card->index, profile_name.c_str(), callback, mainloop);
	pa_threaded_mainloop_unlock(mainloop);

	// give time for card profile to be set
	waitForOpFinish(o);

	// rebuild sink list
	buildSinkList();

	// PipeWire likes to change the sink after changing a card - switch back to
	// the current default sink, if it's still available. Otherwise, just guess
	// which sink the user wants
	pa_threaded_mainloop_lock(mainloop);

	bool oldSinkExists = true;
	struct cbStruct {
		bool& b;
		pa_threaded_mainloop* mainloop;
		std::string sink_name;
	} s{oldSinkExists, mainloop, oldSinkName};

	auto cbf = [](pa_context* c, int suc, void* data) {
		auto items = static_cast<cbStruct*>(data);
		if (!suc) {
			items->b = false;
			std::cout << "Failed to set sink to " << items->sink_name << std::endl;
		} else
			std::cout << "Successfully changed to sink " << items->sink_name << std::endl;

		pa_threaded_mainloop_signal(items->mainloop, 0);
	};
	o = pa_context_set_default_sink(context, oldSinkName.c_str(), cbf, static_cast<void*>(&s));
	pa_threaded_mainloop_unlock(mainloop);
	waitForOpFinish(o);

	// guess the name of the newly created sink and try setting it
	if (!oldSinkExists) {
		pa_threaded_mainloop_lock(mainloop);
		// replace alsa_card with alsa_output
		std::string card_name = card->name; // alsa_card.XXX...
		card_name.replace(5, 4, "output");  // alsa_output

		// remove "output:" from profile name
		profile_name.replace(0, 7, "");

		// some profile names seem to have two colons? the stuff after the last
		// one seems to be the sink name
		auto colonLoc = profile_name.find(':');
		if (colonLoc != std::string::npos)
			profile_name.replace(0, colonLoc + 1, "");

		std::string guessedSink = card_name + "." + profile_name;

		s.sink_name = guessedSink;
		o = pa_context_set_default_sink(context, guessedSink.c_str(), cbf, static_cast<void*>(&s));
		pa_threaded_mainloop_unlock(mainloop);
		waitForOpFinish(o);
	}

	// small sleep to give time for default sink to be updated
	pa_msleep(100);
	getDefaultSink();
}

// get card profiles into qml
QStringList Card::getProfileList() const {
	QStringList list;
	for (const auto& profile : availableProfiles)
		list << profile.description.c_str();
	return list;
}

Profile Card::getActiveProfile() {
	emit profilesChanged();
	return availableProfiles[activeProfileIndex];
}

/* helper functions */

void PAManager::getDefaultSink() {
	struct cbStruct {
		std::vector<Sink>& sink_list;
		int& def_index;
		pa_threaded_mainloop* mainloop;
	};
	cbStruct s = {sinkList, defaultSinkIndex, mainloop};

	auto callback = [](pa_context*, const pa_server_info* info, void* data) {
		auto items = static_cast<cbStruct*>(data);
		std::string defaultSinkName = info->default_sink_name;
		for (int i = 0; i < items->sink_list.size(); i++) {
			if (items->sink_list[i].name == defaultSinkName) {
				items->def_index = i;
				break;
			}
		}
		pa_threaded_mainloop_signal(items->mainloop, 0);
	};
	pa_operation* o = pa_context_get_server_info(context, callback, static_cast<void*>(&s));
	waitForOpFinish(o);
	emit newDefaultSink();
}

void PAManager::buildSinkList() {
	pa_threaded_mainloop_lock(mainloop);
	if (!sinkList.empty())
		sinkList.clear();

	struct cbStruct {
		std::vector<Sink>& sink_list;
		pa_threaded_mainloop* mainloop;
	};
	cbStruct s = {sinkList, mainloop};

	auto callback = [](pa_context*, const pa_sink_info* sink, int eol, void* data) {
		auto items = static_cast<cbStruct*>(data);
		if (eol) {
			pa_threaded_mainloop_signal(items->mainloop, 0);
			return;
		}
		Sink s = {sink->name, sink->description, sink->index, sink->volume};
		items->sink_list.push_back(s);
	};

	pa_operation* o = pa_context_get_sink_info_list(context, callback, static_cast<void*>(&s));
	pa_threaded_mainloop_unlock(mainloop);
	waitForOpFinish(o);
	emit sinksChanged();
}

void PAManager::buildCardList() {
	struct cbStruct {
		std::vector<std::unique_ptr<Card>>& card_list;
		pa_threaded_mainloop* mainloop;
	};
	cbStruct s = {cardList, mainloop};

	auto callback = [](pa_context*, const pa_card_info* card, int eol, void* data) {
		auto items = static_cast<cbStruct*>(data);
		if (eol) {
			if (pa_threaded_mainloop_in_thread(items->mainloop))
				pa_threaded_mainloop_signal(items->mainloop, 0);
			return;
		}

		auto c = std::make_unique<Card>();
		c->name = card->name;
		c->index = card->index;
		const char* desc = pa_proplist_gets(card->proplist, "device.description");
		c->description = (desc) ? desc : card->name;

		for (unsigned int i = 0; i < card->n_profiles; i++) {
			if (card->profiles2[i]->available) {
				Profile p;
				p.name = card->profiles2[i]->name;
				p.description = card->profiles2[i]->description;
				p.active = (card->profiles2[i] == card->active_profile2);
				c->availableProfiles.push_back(p);
				if (p.active)
					c->activeProfileIndex = i;
			}
		}
		items->card_list.push_back(std::move(c));
	};

	pa_operation* o = pa_context_get_card_info_list(context, callback, static_cast<void*>(&s));
	waitForOpFinish(o);
}

bool PAManager::saveConfig() {
	// class to make sure mainloop is always unlocked at the end
	class threadChk {
	  private:
		pa_threaded_mainloop* ml;

	  public:
		explicit threadChk(pa_threaded_mainloop* ml_) : ml(ml_) { pa_threaded_mainloop_lock(ml); }
		~threadChk() { pa_threaded_mainloop_unlock(ml); }
	};
	threadChk T(mainloop);

	std::string sink_name = sinkList[defaultSinkIndex].name;

	// card name should be first part of sink name (alsa_card.pci_XXXX)
	std::string card_name = sink_name;

	// change "output" to "card"
	card_name.replace(5, 6, "card");
	auto profile_name_loc = card_name.rfind('.');

	// save profile name from end of sink name
	std::string profile_name = card_name.substr(profile_name_loc + 1);
	profile_name = "output:" + profile_name;
	card_name.replace(card_name.begin() + profile_name_loc, card_name.end(), "");

	QDir config_dir(strings::config_dir_loc);
	if (!config_dir.exists()) {
		if (!config_dir.mkpath(strings::config_dir_loc)) {
			std::cerr << "Could not create config directory!" << std::endl;
			return false;
		}
	}
	QFile config(strings::audioconfig_loc);
	if (!config.open(QFile::WriteOnly)) {
		std::cerr << "Could not open audioconfig.txt for writing!" << std::endl;
		return false;
	}
	QTextStream out(&config);
	out << "Sink: " << sink_name.c_str() << '\n';
	out << "Card: " << card_name.c_str() << '\n';
	out << "Profile: " << profile_name.c_str() << '\n';

	return true;
}

void PAManager::loadConfig() {
	pa_threaded_mainloop_lock(mainloop);
	QFile config(strings::audioconfig_loc);
	if (!config.open(QFile::ReadOnly)) {
		std::cout << "No configuration file found." << std::endl;
		pa_threaded_mainloop_unlock(mainloop);
		return;
	}

	QTextStream in(&config);

	QString sink = in.readLine();
	// get rid of "Sink: "
	sink.replace(0, 6, "");

	QString card = in.readLine();
	// get rid of "Card: "
	card.replace(0, 6, "");

	QString profile = in.readLine();
	// get rid of "Profile: "
	profile.replace(0, 9, "");

	config.close();

	// attempt to set profile
	struct cbStruct {
		QString& cardStr;
		QString& profileStr;
		pa_threaded_mainloop* mainloop;
	} s{card, profile, mainloop};

	auto callback = [](pa_context* c, int success, void* data) {
		auto items = static_cast<cbStruct*>(data);
		if (!success)
			std::clog << "Could not set profile " << items->profileStr.toStdString() << " on card "
					  << items->cardStr.toStdString() << std::endl;
		pa_threaded_mainloop_signal(items->mainloop, 0);
	};

	pa_operation* o = pa_context_set_card_profile_by_name(
			context, card.toUtf8(), profile.toUtf8(), callback, &s);
	pa_threaded_mainloop_unlock(mainloop);
	waitForOpFinish(o);

	pa_threaded_mainloop_lock(mainloop);
	struct cbStruct2 {
		QString& sinkStr;
		pa_threaded_mainloop* mainloop;
	} s2{sink, mainloop};

	auto callback2 = [](pa_context* c, int success, void* data) {
		auto items = static_cast<cbStruct2*>(data);
		if (!success)
			std::clog << "Could not set sink to " << items->sinkStr.toStdString() << std::endl;
		else
			std::clog << "Set sink to " << items->sinkStr.toStdString() << std::endl;
		pa_threaded_mainloop_signal(items->mainloop, 0);
	};
	o = pa_context_set_default_sink(context, sink.toUtf8(), callback2, &s2);
	pa_threaded_mainloop_unlock(mainloop);
	waitForOpFinish(o);
	// small sleep to give time for sink to be updated (seems to be a PipeWire issue?)
	pa_msleep(20);
}

bool PAManager::Sink::operator==(const Sink& other) const {
	return (name == other.name && description == other.description && index == other.index);
}
