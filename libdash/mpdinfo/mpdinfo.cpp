#include "libdash.h"

using namespace dash;
using namespace dash::mpd;

bool isContainedInMimeType(dash::mpd::IAdaptationSet *adaptationSet, std::string value) {
	std::string topMimeType = adaptationSet->GetMimeType();
	if (!topMimeType.empty())
		if (topMimeType.find(value) != std::string::npos)
			return true;

	for (size_t i = 0; i < adaptationSet->GetRepresentation().size(); i++) {
		std::string mimeType = adaptationSet->GetRepresentation().at(i)->GetMimeType();
		if (!mimeType.empty())
			if (mimeType.find(value) != std::string::npos)
				return true;
	}

	return false;
}

std::string showProperty(const char* name, std::string value) {
	if (value.empty()) return std::string("");
	std::ostringstream os;
	os << "  " << name << ": " << value << std::endl;
	return os.str();
}

std::string showVector(const std::vector<std::string>& v) {
	std::ostringstream os;
	for (size_t i = 0; i < v.size(); i++) {
		os << v[i];
		if (i < v.size()-1) {
			os << ",";
		}
	}
	return os.str();
}

std::string dumpRepresentationBaseInfo(IRepresentationBase *r) {
	std::ostringstream os;
	os << "  Resolution: " << r->GetWidth() << "x" << r->GetHeight() << std::endl;
	os << showProperty("SAR", r->GetSar());
	os << showProperty("Frame Rate", r->GetFrameRate());
	os << showProperty("Audio Sample Rate", r->GetAudioSamplingRate());
	os << showProperty("MimeType", r->GetMimeType());
	os << showProperty("Codecs", showVector(r->GetCodecs()));
	os << showProperty("Profiles", showVector(r->GetProfiles()));
	os << showProperty("Segment Profiles", showVector(r->GetSegmentProfiles()));
	return os.str();
}

void dumpRepresentationInfo(IRepresentation *r) {
	std::cout << "Representation " << r->GetId() << std::endl;
	std::cout << "  Bandwidth: " << r->GetBandwidth() << "bps" << std::endl;
	std::cout << dumpRepresentationBaseInfo(r);
	const std::vector<IBaseUrl *> baseURLs = r->GetBaseURLs();
	if (!baseURLs.empty()) {
		std::cout << "  Base URLs:" << std::endl;
		for (size_t i = 0; i < baseURLs.size(); i++) {
			std::cout << "    " + baseURLs[i]->GetUrl();
		}
	}
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		std::cerr << "Usage: mpdinfo.exe <MPD URL>" << std::endl;
		return 1;
	}

	IDASHManager* dashManager = CreateDashManager();
	std::cout << argv[1] << std::endl;
	IMPD* mpd = dashManager->Open(argv[1]);

	if (mpd == NULL) {
		std::cerr << "Failed to read MPD" << std::endl;
		return 1;
	}

	if (mpd->GetPeriods().size() > 0) {
		IPeriod *period = mpd->GetPeriods().at(0);

		std::vector<IAdaptationSet *> adaptationSets = period->GetAdaptationSets();

		for (size_t i = 0; i < adaptationSets.size(); i++) {
			IAdaptationSet* adaptationSet = adaptationSets.at(i);
			if (isContainedInMimeType(adaptationSet, "video")) {
				std::cout << "Adaptation set " << i << std::endl;
				const std::vector<IBaseUrl *> baseURLs = adaptationSet->GetBaseURLs();
				if (!baseURLs.empty()) {
					std::cout << "  Base URLs:" << std::endl;
					for (size_t i = 0; i < baseURLs.size(); i++) {
						std::cout << "    " + baseURLs[i]->GetUrl();
					}
				}
				std::vector<IRepresentation *> representations = adaptationSet->GetRepresentation();

				for (size_t i = 0; i < representations.size(); i++) {
					dumpRepresentationInfo(representations.at(i));
					const std::vector<ISubRepresentation *> subRepresentations = representations.at(i)->GetSubRepresentations();
					for (size_t i = 0; i < subRepresentations.size(); i++) {
						std::cout << "SubRepresentation " << i << std::endl;
						std::cout << dumpRepresentationBaseInfo(subRepresentations.at(i));
					}
				}
			}
		}
	}

	return 0;
}
